#ifndef MIC16_PHYSICAL_PROJECTION_TASK_H
#define MIC16_PHYSICAL_PROJECTION_TASK_H
#include "mic16_projection_runtime.h"

enum {M16_PP_IDLE=0,M16_PP_GEOMETRY=1,M16_PP_OBSERVATIONS=2,M16_PP_COEFFICIENTS=3,
      M16_PP_DESCRIPTORS=4,M16_PP_SUBMIT=5,M16_PP_WAIT=6,M16_PP_SCATTER=7,
      M16_PP_COMPLETE=8,M16_PP_STOPPED=9,M16_PP_FAILED=10,M16_PP_CACHE=11};
enum {M16_PP_INVALID=-400,M16_PP_BUSY=-401,M16_PP_INPUT=-402,M16_PP_RESULT=-403,
      M16_PP_OWNERSHIP=-404,M16_PP_FLOAT_ENV=-405};
#if M16_PROJECTION_VERSION>=4u
#define M16_PP_JOB_COUNT 1u
#else
#define M16_PP_JOB_COUNT 7u
#endif
#define M16_PP_ARENA_WORDS 40000u
#define M16_PP_PHAT_VALUES (20u*807u*32u*2u)
#define M16_PP_FEATURES_PER_CANDIDATE (20u*88u)
#define M16_PP_MAX_STEP_ITEMS 64u

/* 每个节点独占一个runtime和arena。调度器从begin到终态持有输入PHAT读租约、
 * 输出写租约及该节点租约；不得把同一runtime/arena交给另一节点。共享的设备
 * 由平台仲裁：提交前可准备数据，submit返回BUSY时等待；设备活动时不能改arena。
 * 输入PHAT保持原[20,807,32,复数]，输出仍为[C,20,88]；只写第0～23列。
 * 其余列由独立节点填写，整个特征窗只有所有节点完成后才允许发布。 */
typedef int (*m16_pp_signature_provider)(void *,unsigned,unsigned,unsigned,float *);
typedef int (*m16_pp_row_provider)(void *,unsigned,unsigned,const uint64_t **,uint64_t *);
/* 只缓存固定网格的精算值；epoch/绝对帧号对应真实前端历史，几何绑定后锁定。
 * lookup为1命中/0缺失/负数错误；store为0已保存/1槽已淘汰/负数错误。 */
typedef struct {
    void *context;
    int (*lookup)(void *,uint64_t,uint64_t,unsigned,unsigned,float *);
    int (*store)(void *,uint64_t,uint64_t,unsigned,unsigned,float);
    const void *geometry_owner;
} m16_pp_grid_cache;
typedef struct {
    uint32_t magic,phase,zone,bins,count,cursor,obs,coef,out,receipts,used_words;
    uint32_t stop_requested,last_step_items,max_step_items,transactions,results,kind,zone_begin,zone_end;
    int error;
    uint64_t epoch,first_tag,reserved_tag_high,steps;
    m16_projection_runtime *runtime;
    const float *phat;
    float *features;
    double directions[32];
    float lags[16*32];
    unsigned grids[16];
    /* 只在显式读租约范围内复用；运行标签变化会使准备好的观测失效。 */
    unsigned reuse_active,reuse_valid,reuse_zone;
    uint64_t reuse_epoch,reuse_scope,reuse_runtime_tag,reuse_revision,observation_words,observation_reuses;
    const float *reuse_phat;
    m16_pp_signature_provider signature_provider;
    void *signature_context;
    m16_pp_row_provider row_provider;
    void *row_context;
    const uint64_t *rows[16];
    uint64_t row_sign[16];
    unsigned row_valid,row_pair;
    unsigned active_count,active_frames[20],active_mask,reuse_frame_mask,cache_locked;
    uint64_t cache_first_frame,cached_results;
    m16_pp_grid_cache *grid_cache;
} m16_physical_projection_task;
#ifdef __cplusplus
extern "C" {
#endif
/* task首次须为零对象，runtime须已用版本3核及真实平台IO初始化。
 * 本模块不配置或复位设备，不修改浮点环境：要求RNE且关闭FTZ/DAZ。
 * 用途是新任务图中的物理响应节点，不调用原整窗特征/检测函数。 */
int m16_physical_projection_init(m16_physical_projection_task *,m16_projection_runtime *);
/* 在无活动窗且runtime空闲时绑定冻结几何查表；默认仍为原函数。
 * provider必须保持F32位语义，未准备时拒绝；context在节点生命周期内有效。 */
int m16_physical_projection_bind_signatures(m16_physical_projection_task *,m16_pp_signature_provider,void *);
int m16_physical_projection_bind_rows(m16_physical_projection_task *,m16_pp_row_provider,void *);
/* 上层持有PHAT读租约时开启本窗复用范围，scope为本次精算的唯一首标签。
 * 同窗同频带、相同runtime最后提交标签才允许复用；下一窗必须结束后重开。
 * 该范围不获取/释放DMA租约、不伪造硬件idle，不能用相同指针推断输入没变。 */
int m16_physical_projection_reuse_begin(m16_physical_projection_task *,const float *,unsigned,uint64_t epoch,uint64_t scope);
void m16_physical_projection_reuse_end(m16_physical_projection_task *);
/* 提供实际缓冲长度；begin仅验证尺寸、租约、方向并复制至多32个方向数值。
 * epoch非零；168个标签预留给24频带×7任务，失败或取消后也不能复用该范围。
 * 不把网络舍入后的方向作为几何输入，传入原始FP64候选方向。 */
int m16_physical_projection_begin(m16_physical_projection_task *,const float *phat,unsigned phat_values,
    const double *directions,unsigned candidates,float *features,unsigned feature_values,
    uint64_t epoch,uint64_t first_tag);
/* 复用同一FP64顺序归约通路，精算指定频带的1～16个固定扫描网格。
 * signature仍调用冻结mic16_spatial_signature（可由已核验字典提供），不以候选
 * 几何近似网格。输出maps[20,24,325]中仅写指定频带/网格，其余保持。
 * 本次仍预留168标签，实际用前7个；只有显式reuse范围内可跨批复用观测。 */
/* 仅在reuse_begin后、尚未启动本窗任务时接入；旧V3不接受缓存压缩帧包。 */
int m16_physical_projection_use_grid_cache(m16_physical_projection_task *,m16_pp_grid_cache *,uint64_t first_frame);
int m16_physical_projection_begin_grids(m16_physical_projection_task *,const float *,unsigned,
    const unsigned *grids,unsigned count,unsigned zone,float *maps,unsigned map_values,
    uint64_t epoch,uint64_t first_tag);
/* 1进行中，0本节点全部24频带完成，2已停止，负数失败。每次最多64个数据项，
 * 系数最多16项（32次三角函数），或一个异步驱动poll；不轮询等待硬件。
 * COMPLETE仅表示响应24列完成，不表示130特征、整窗检测或实时验收完成。 */
int m16_physical_projection_step(m16_physical_projection_task *);
/* 1已安全停止，0等待设备排空，负数错误；返回0后继续step，不能直接重用工作区。
 * 取消的部分输出无效。故障隔离保留runtime所有权，不伪造idle来归还内存。 */
int m16_physical_projection_stop(m16_physical_projection_task *);
#ifdef __cplusplus
}
#endif
#endif
