# 只构建统一工程内的新共享计算IP，不创建另一套Vivado/Vitis工程，不接板卡。
set root [file normalize [file join [file dirname [info script]] ../../..]]
proc build {root} {
    set src [file join $root fpga hls shared_projection_engine]
    set protocol [file join $root firmware acceleration projection_engine include]
    set out [file normalize [file join $root build hls shared_projection_physical_20261024]]
    file mkdir $out
    cd $out
    # 只清理已核对的本IP生成目录；历史报告保存在它外面的同级证据目录。
    set project [file normalize [file join $out shared_projection]]
    if {[file dirname $project] ne $out} {error "HLS生成目录越界"}
    open_project -reset shared_projection
    set_top m16_shared_projection_engine
    add_files [file join $src src shared_projection_engine.cpp] -cflags "-I$src/src -ffp-contract=off -fno-fast-math"
    add_files -tb [file join $src test shared_projection_test.cpp] -cflags "-I$src/src -I$protocol -ffp-contract=off -fno-fast-math"
    open_solution shared_modes
    set_part {xc7z020clg484-2}
    create_clock -period 8 -name default
    # 描述符检查不需要自动流水，避免给低频控制复制大批比较器和宽地址算术。
    config_compile -pipeline_loops 0
    config_op fadd -impl fabric -latency 8
    config_op fsub -impl fabric
    config_op dadd -impl fabric
    csim_design
    csynth_design
}
if {[catch {build $root} message]} {
    puts stderr "M16_SHARED_ENGINE_BUILD_FAILED: $message"
    exit 1
}
puts "M16_NEW_SHARED_ENGINE_SYNTHESIZED_NOT_FULL_ARCHITECTURE"
exit 0
