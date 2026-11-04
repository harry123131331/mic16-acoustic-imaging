# 主函数与各模块源码索引

按build/ps_recipe.json实际编译文件定位；build/inputs为发布快照，同名备用源不代替构建输入。

| 函数 | 提交包内源码 | 行号 |
|---|---|---|
| main | firmware/app/audio_dataflow_main.c | 22 |
| m16_audio_platform_arm_init | build/inputs/4eb2b011ba/mic16_audio_platform_arm_latest.c | 64 |
| m16_audio_platform_arm_step | build/inputs/4eb2b011ba/mic16_audio_platform_arm_latest.c | 96 |
| start | build/inputs/3269688a83/mic16_latest_display_resident.c | 29 |
| raw_copy | build/inputs/3269688a83/mic16_latest_display_resident.c | 40 |
| send_one | build/inputs/3269688a83/mic16_latest_display_resident.c | 53 |
| drain | build/inputs/3269688a83/mic16_latest_display_resident.c | 98 |
| m16_latest_display_resident_step | build/inputs/3269688a83/mic16_latest_display_resident.c | 130 |
| admit | build/inputs/ab8206c563/mic16_latest_window_task.c | 36 |
| snapshot | build/inputs/ab8206c563/mic16_latest_window_task.c | 46 |
| compute | build/inputs/ab8206c563/mic16_latest_window_task.c | 76 |
| m16_latest_window_step | build/inputs/ab8206c563/mic16_latest_window_task.c | 121 |
| m16_latest_window_ack | build/inputs/ab8206c563/mic16_latest_window_task.c | 133 |
| m16_pcm_acquisition_step | build/inputs/6c1c5513d3/mic16_pcm_acquisition.c | 43 |
| m16_pcm_acquisition_frame | build/inputs/6c1c5513d3/mic16_pcm_acquisition.c | 83 |
| m16_public_spectrum_begin | firmware/modules/dataflow/src/mic16_public_spectrum_task.c | 47 |
| run_step | firmware/modules/dataflow/src/mic16_public_spectrum_task.c | 58 |
| m16_public_spectrum_step | firmware/modules/dataflow/src/mic16_public_spectrum_task.c | 171 |
| m16_spatial_evidence_step | firmware/modules/dataflow/src/mic16_spatial_evidence_cache.c | 56 |
| m16_spatial_evidence_coarse | firmware/modules/dataflow/src/mic16_spatial_evidence_cache.c | 94 |
| m16_fast_doa_begin | build/inputs/c6115deead/mic16_fast_doa_task.c | 37 |
| publish | build/inputs/c6115deead/mic16_fast_doa_task.c | 59 |
| m16_fast_doa_step | build/inputs/c6115deead/mic16_fast_doa_task.c | 88 |
| m16_fast_doa_ack | build/inputs/c6115deead/mic16_fast_doa_task.c | 155 |
| m16_latest_display_encode | build/inputs/19d5cf4a39/mic16_latest_display_wire.c | 16 |
| m16_shared_compute_graph_init | firmware/acceleration/projection_engine/src/mic16_shared_compute_graph.c | 16 |
| m16_shared_compute_graph_service | firmware/acceleration/projection_engine/src/mic16_shared_compute_graph.c | 40 |
| m16_fft64_graph_init | firmware/acceleration/projection_engine/src/mic16_fft64_graph.c | 22 |
| m16_fft64_graph_service | firmware/acceleration/projection_engine/src/mic16_fft64_graph.c | 64 |
| run | host/python_tools/mic16_qt/services/source_network.py | 288 |
| _publish | host/python_tools/mic16_qt/services/source_network.py | 275 |
| take_snapshot | host/python_tools/mic16_qt/services/source_network.py | 271 |
| connect_device | host/python_tools/mic16_qt/presentation/compact_window.py | 667 |
| _fast_packet_arrived | host/python_tools/mic16_qt/presentation/compact_window.py | 886 |
| _apply_fast_direction | host/python_tools/mic16_qt/presentation/compact_window.py | 911 |
| _apply_fast_map | host/python_tools/mic16_qt/presentation/compact_window.py | 931 |
| _redraw_source_audio | host/python_tools/mic16_qt/presentation/compact_window.py | 1033 |
| start_usb_video | host/python_tools/mic16_qt/presentation/video_panel.py | 618 |
| _poll | host/python_tools/mic16_qt/presentation/video_panel.py | 665 |
| set_spatial_response | host/python_tools/mic16_qt/presentation/video_panel.py | 401 |
| _update_physical_overlay | host/python_tools/mic16_qt/presentation/video_panel.py | 410 |
| paintEvent | host/python_tools/mic16_qt/presentation/video_panel.py | 56 |
| start | host/python_tools/mic16_qt/presentation/usb_camera_preview.py | 102 |
| _accept_frame | host/python_tools/mic16_qt/presentation/usb_camera_preview.py | 159 |
| take_latest | host/python_tools/mic16_qt/presentation/usb_camera_preview.py | 173 |
| project_direction | host/python_tools/mic16_av_fusion/preview_projection.py | 29 |
| decode_direction | host/python_tools/mic16_av_fusion/fast_display_wire.py | 98 |
| accept_current | host/python_tools/mic16_av_fusion/fast_display_wire.py | 174 |
| same_observation | host/python_tools/mic16_av_fusion/fast_display_wire.py | 72 |

静态调用核对，不代表新的板端计时。当前快速模式不准入C16、增强或连续重建任务。
公共FFT64的provider经m16_fft64_graph绑定到shared_projection；BD中的d4f_tf_front_q24不是该路径的FFT64执行位置。
最新相机叠加为真实响应门控的渐变方向标记，渐变与半径是显示规则。定位页仅小圆点，原始响应未改。
