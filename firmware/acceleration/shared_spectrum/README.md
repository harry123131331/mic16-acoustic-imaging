# PS共享FFT候选模块

这是默认关闭的性能候选，不是正式固件入口。pack/batch/runtime/dispatch分别负责布局、16+4预处理和频带、MMIO/cache/隔离、可切换分派。所有说明和验证范围见docs/shared_spectrum_ps_integration.md。对应HLS源码在fpga/hls/shared_spectrum。

必须使用匹配新ABI的BIT；禁止向旧IP发送新DMA寄存器。超时隔离后的arena不能继续写入或重绑复用。原CPU路径、冻结模型与逐源增强保留。
