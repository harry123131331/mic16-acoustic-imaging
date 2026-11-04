# 新音频XSA的BSP软件仓库

驱动C/头及版权来自同一已核验XSA，未改算法。仅将生成Makefile的制表符后#echo修为@echo，兼容Windows GNU make；否则Vitis每次重新生成都会恢复错误命令。audio_system.mss对应新共享FFT与三模式投影，清除原工作区旧前端驱动引用。原XSA不修改。
