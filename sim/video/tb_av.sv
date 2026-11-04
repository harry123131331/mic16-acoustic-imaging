`timescale 1ns/1ps
module tb_av;
    reg axi_clk=0,pix_clk=0,resetn=0;
    always #7 axi_clk=~axi_clk;
    always #50 pix_clk=~pix_clk;
    reg [7:0] awaddr=0,araddr=0;
    reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
    reg [31:0] wdata=0;reg [3:0] wstrb=0;
    wire awready,wready,bvalid,arready,rvalid;
    wire [1:0] bresp,rresp;wire [31:0] rdata;
    reg sof=0,de=0,hs=0,vs=0;reg [11:0] x=0,y=0;reg [23:0] rgb=24'h112233;
    wire ode,ohs,ovs;wire [23:0] orgb;wire [31:0] active_cycle;wire visible;
    integer checks=0,fd,i,j;reg [31:0] value;reg [31:0] fixture[0:31];
    mic16_av_overlay #(.IMAGE_WIDTH(64),.IMAGE_HEIGHT(48),.PIX_CLK_HZ(10000000)) dut(
        .s_axi_aclk(axi_clk),.pix_clk(pix_clk),.resetn(resetn),
        .s_axi_awaddr(awaddr),.s_axi_awprot(3'b000),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
        .s_axi_wdata(wdata),.s_axi_wstrb(wstrb),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
        .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
        .s_axi_araddr(araddr),.s_axi_arprot(3'b000),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
        .s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready),
        .frame_start(sof),.pixel_x(x),.pixel_y(y),.in_de(de),.in_hs(hs),.in_vs(vs),.in_rgb(rgb),
        .out_de(ode),.out_hs(ohs),.out_vs(ovs),.out_rgb(orgb),
        .active_cycle_id(active_cycle),.marker_visible(visible));
    task check(input logic yes,input string why);
        begin checks=checks+1;if(yes!==1'b1)$fatal(1,"AV_FAIL %s at %t",why,$time);end
    endtask
    task send_aw(input [7:0] addr,input integer delay_cycles);
        begin repeat(delay_cycles)@(negedge axi_clk);
            @(negedge axi_clk);awaddr=addr;awvalid=1;
            do @(posedge axi_clk);while(!awready);
            @(negedge axi_clk);awvalid=0;
        end
    endtask
    task send_w(input [31:0] data,input [3:0] strb,input integer delay_cycles);
        begin repeat(delay_cycles)@(negedge axi_clk);
            @(negedge axi_clk);wdata=data;wstrb=strb;wvalid=1;
            do @(posedge axi_clk);while(!wready);
            @(negedge axi_clk);wvalid=0;
        end
    endtask
    task write_reg(input [7:0] addr,input [31:0] data,input [3:0] strb,input integer order,input [1:0] expected);
        begin
            fork send_aw(addr,order==1?6:0);send_w(data,strb,order==2?5:0);join
            wait(bvalid);#1;check(bresp===expected,"write response");
            repeat(3)begin @(posedge axi_clk);#1;check(bvalid && bresp===expected,"B backpressure stable");end
            @(negedge axi_clk);bready=1;@(posedge axi_clk);@(negedge axi_clk);bready=0;
        end
    endtask
    task read_reg(input [7:0] addr,input [1:0] expected,output [31:0] data);
        begin
            @(negedge axi_clk);araddr=addr;arvalid=1;
            do @(posedge axi_clk);while(!arready);
            @(negedge axi_clk);arvalid=0;wait(rvalid);#1;data=rdata;
            check(rresp===expected,"read response");
            repeat(4)begin @(posedge axi_clk);#1;check(rvalid && rdata===data && rresp===expected,"R backpressure stable");end
            @(negedge axi_clk);rready=1;@(posedge axi_clk);@(negedge axi_clk);rready=0;
        end
    endtask
    task wait_free;
        integer k;
        begin value=1;k=0;while(value[0] && k<100)begin read_reg(8'h08,0,value);k=k+1;end
            check(k<100 && value[1],"result channel returns ready");end
    endtask
    task frame;
        begin @(negedge pix_clk);sof=1;@(negedge pix_clk);sof=0;repeat(2)@(posedge pix_clk);#1;end
    endtask
    task pixel(input integer xx,input integer yy,input [23:0] expected);
        begin @(negedge pix_clk);x=xx;y=yy;de=1;hs=1;vs=0;
            @(posedge pix_clk);#1;check(ode && ohs && !ovs && orgb===expected,
                $sformatf("registered RGB/timing x=%0d y=%0d actual=%h expected=%h",xx,yy,orgb,expected));end
    endtask
    task common_reset;
        begin resetn=0;repeat(7)@(negedge pix_clk);resetn=1;repeat(40)@(negedge pix_clk);end
    endtask
    always @(posedge axi_clk)if(resetn && dut.axi_up && dut.fifo_write)
        check(!dut.fifo_full && !dut.wr_reset_busy,"FIFO write only when ready");
    always @(posedge pix_clk)if(resetn && dut.fifo_read)
        check(!dut.fifo_empty && !dut.rd_reset_busy,"FIFO read only when valid");
    initial begin
        #5000000;$fatal(1,"AV simulation timeout");
    end
    initial begin
        $readmemh("av_fixture.hex",fixture);
        common_reset();
        read_reg(0,0,value);check(value==32'h4d415631,"device identity");
        read_reg(4,0,value);check(value==32'h00010000,"protocol version");
        check(!visible && active_cycle==0,"reset hides markers");
        write_reg(8'h10,32'h12345678,4'b1111,1,0);
        write_reg(8'h10,32'haabbccdd,4'b0101,2,0);
        read_reg(8'h10,0,value);check(value==32'h12bb56dd,"partial WSTRB independent AW/W");
        write_reg(8'h10,32'hffffffff,0,0,0);
        read_reg(8'h10,0,value);check(value==32'h12bb56dd,"zero WSTRB no mutation");
        write_reg(8'h11,0,15,0,2);write_reg(8'h80,0,15,0,2);read_reg(8'h80,2,value);
        for(i=4;i<8;i=i+1)write_reg(i*4,fixture[i],15,i%3,0);
        for(i=16;i<32;i=i+1)write_reg(i*4,fixture[i],15,i%3,0);
        write_reg(8'h0c,1,15,1,0);repeat(40)@(negedge pix_clk);
        check(active_cycle==0 && !visible,"result waits for frame boundary");
        write_reg(8'h0c,1,15,2,2);
        write_reg(8'h40,32'h00140014,15,0,0); // 快照传输中修改影子寄存器。
        frame();check(active_cycle==42 && visible,"whole C fixture applied at SOF");wait_free();
        pixel(8,10,24'h20ff60);pixel(20,20,24'h112233);
        pixel(56,32,24'h112233);pixel(61,32,24'hffbf40); // 保持中的ID显示为空心琥珀色标记。
        // 导出真实RTL完整结果，供独立软件光栅检查。
        fd=$fopen("overlay_frame.ppm","w");$fwrite(fd,"P3\n64 48\n255\n");
        for(j=0;j<48;j=j+1)for(i=0;i<64;i=i+1)begin
            @(negedge pix_clk);x=i;y=j;de=1;
            @(posedge pix_clk);#1;$fwrite(fd,"%0d %0d %0d\n",orgb[23:16],orgb[15:8],orgb[7:0]);
        end
        $fclose(fd);
        write_reg(8'h18,43,15,0,0);write_reg(8'h0c,1,15,0,0);
        repeat(30)@(negedge pix_clk);check(active_cycle==42,"midframe coordinates unchanged");
        pixel(8,10,24'h20ff60);frame();check(active_cycle==43,"new result swaps atomically");wait_free();
        pixel(20,20,24'h20ff60);
        write_reg(8'h10,1,15,0,0);write_reg(8'h14,0,15,0,0);write_reg(8'h0c,1,15,0,0);
        repeat(30)@(negedge pix_clk);frame();wait_free();check(!visible,"uncalibrated update hides all");
        write_reg(8'h10,3,15,0,0);write_reg(8'h14,0,15,0,0);write_reg(8'h0c,1,15,0,2);
        write_reg(8'h14,1,15,0,0);write_reg(8'h18,44,15,0,0);write_reg(8'h0c,1,15,0,0);
        repeat(11000)@(negedge pix_clk);wait_free();frame();
        check(!visible && active_cycle==43,"pending expiration cannot resurrect at later SOF");
        write_reg(8'h18,45,15,0,0);write_reg(8'h0c,1,15,0,0);
        repeat(30)@(negedge pix_clk);frame();wait_free();check(visible,"short valid result initially visible");
        repeat(11000)@(negedge pix_clk);check(!visible,"active result expires even without video frames");
        write_reg(8'h14,100,15,0,0);write_reg(8'h18,46,15,0,0);write_reg(8'h0c,1,15,0,0);
        repeat(15)@(negedge pix_clk);common_reset();wait_free();
        frame();check(!visible && active_cycle==0,"common reset cancels pending CDC transaction");
        for(i=4;i<8;i=i+1)write_reg(i*4,fixture[i],15,i%3,0);
        for(i=16;i<32;i=i+1)write_reg(i*4,fixture[i],15,i%3,0);
        write_reg(8'h0c,1,15,0,0);repeat(40)@(negedge pix_clk);frame();wait_free();
        pixel(8,10,24'h20ff60);check(active_cycle==42,"post-reset transfer works");
        @(negedge pix_clk);de=0;hs=0;vs=1;@(posedge pix_clk);#1;
        check(!ode && !ohs && ovs && orgb==0,"blanking/timing stays aligned");
        $display("AV_RTL_PASS checks=%0d source=synthetic_C_geometry clocks=asynchronous",checks);$finish;
    end
endmodule
