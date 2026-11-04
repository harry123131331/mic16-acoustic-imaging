#ifndef MIC16_PAIRWISE4_STREAM_H
#define MIC16_PAIRWISE4_STREAM_H
/* 在声明v4、add4、dup4之后包含。最多1088项，固定成对加法树不变。
 * count的第k位表示partial[k]持有此前连续2^k项的归约值。
 * 相邻项先配对，最后从小块向大块合并，等价于逐层相邻配对、奇数尾项直通。
 * 不改成顺序FP32累加，不补零，不改变FTZ或舍入设置。 */
typedef struct {v4 partial[11];unsigned count;} m16_pairwise4_stream;
static inline void m16_pairwise4_init(m16_pairwise4_stream *s){s->count=0;}
/* 调用方保证总项数<=1088；活动位对应的partial已经写入，不读取未初始化项。 */
static inline void m16_pairwise4_push(m16_pairwise4_stream *s,v4 value){
    unsigned level=0,occupied=s->count;
    while(occupied&1u){value=add4(s->partial[level],value);occupied>>=1;level++;}
    s->partial[level]=value;s->count++;
}
static inline v4 m16_pairwise4_finish(const m16_pairwise4_stream *s){
    unsigned level,have=0;v4 value=dup4(0.f);
    for(level=0;level<11;level++)if(s->count&(1u<<level)){
        value=have?add4(s->partial[level],value):s->partial[level];have=1;
    }
    return value;
}
/* 16项小块先按原树归约，再对块根做同一树。避免每项执行进位分支。
 * 最后的不足16项仅直通提升到块级，不补0，不增加浮点加法。 */
typedef struct {v4 partial[7],block[16];unsigned blocks,pending;} m16_pairwise4_block_stream;
static inline void m16_pairwise4_block_init(m16_pairwise4_block_stream *s){s->blocks=s->pending=0;}
static inline v4 m16_pairwise4_small_tree(v4 *p,unsigned n){
    unsigned j;
    while(n>1){unsigned pairs=n/2;for(j=0;j<pairs;j++)p[j]=add4(p[2*j],p[2*j+1]);if(n&1u)p[pairs]=p[n-1];n=pairs+(n&1u);}
    return p[0];
}
static inline void m16_pairwise4_block_root(m16_pairwise4_block_stream *s,v4 value){
    unsigned level=0,occupied=s->blocks;
    while(occupied&1u){value=add4(s->partial[level],value);occupied>>=1;level++;}
    s->partial[level]=value;s->blocks++;
}
static inline void m16_pairwise4_block_push(m16_pairwise4_block_stream *s,v4 value){
    s->block[s->pending++]=value;
    if(s->pending==16){m16_pairwise4_block_root(s,m16_pairwise4_small_tree(s->block,16));s->pending=0;}
}
static inline v4 m16_pairwise4_block_finish(m16_pairwise4_block_stream *s){
    unsigned level,have=0;v4 value=dup4(0.f);
    if(s->pending){m16_pairwise4_block_root(s,m16_pairwise4_small_tree(s->block,s->pending));s->pending=0;}
    for(level=0;level<7;level++)if(s->blocks&(1u<<level)){
        value=have?add4(s->partial[level],value):s->partial[level];have=1;
    }
    return value;
}
#endif
