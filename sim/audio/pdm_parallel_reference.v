/* 仅用于与原16路通路进行同参数面积对照，不进入正式BD。 */
module pdm_parallel_reference(input wire clk_ref,rst_n,enable,pdm_sample_tick,
    input wire[15:0] pdm_data,output wire[511:0] pcm_samples_flat,output wire pcm_valid);
    wire[15:0] valids;
    genvar c;
    generate for(c=0;c<16;c=c+1)begin:CH
        pdm_decimator #(.ACC_WIDTH(32)) original(
            .clk_ref(clk_ref),.rst_n(rst_n),.enable(enable),.pdm_sample_tick(pdm_sample_tick),
            .pdm_data(pdm_data[c]),.pcm_sample(pcm_samples_flat[c*32+:32]),.pcm_valid(valids[c]));
    end endgenerate
    assign pcm_valid=&valids;
endmodule
