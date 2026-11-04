#ifndef MIC16_NETWORK_SITE_H
#define MIC16_NETWORK_SITE_H
/* 仅候选程序使用的现场参数；启用前确认电脑实际以太网网卡。该开关独立于麦克风幅值、通道及声像标定。修改后重建候选，不修改旧BSP。 */
#define M16_NETWORK_SITE_VERIFIED 1
#define M16_NETWORK_SITE_CONFIG {M16_NETWORK_SITE_VERIFIED, \
    {192,168,1,10},{192,168,1,100},{255,255,255,0},{192,168,1,1}, \
    {2,0,0,22,0,7},5001,250}
#endif
