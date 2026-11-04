`timescale 1ns/1ps
// 音频结果元数据映射为完整帧像素叠加，本模块不生成传感器或HDMI时序。
// 共同复位时两个时钟必须运行，所有时钟域一起复位。
// 多位跨时钟数据由原厂XPM FIFO承载；一份更新在帧起点应用或过期前
// 保持在途，期间仍可写影子字段。
module mic16_av_overlay #(
    parameter integer IMAGE_WIDTH=800,
    parameter integer IMAGE_HEIGHT=480,
    parameter integer PIX_CLK_HZ=25000000
)(
    (* X_INTERFACE_PARAMETER="ASSOCIATED_BUSIF S_AXI, ASSOCIATED_RESET resetn" *)
    input wire s_axi_aclk,
    (* X_INTERFACE_PARAMETER="ASSOCIATED_RESET resetn" *) input wire pix_clk,
    (* X_INTERFACE_PARAMETER="POLARITY ACTIVE_LOW" *) input wire resetn,
    input wire [7:0] s_axi_awaddr, input wire [2:0] s_axi_awprot,
    input wire s_axi_awvalid, output wire s_axi_awready,
    input wire [31:0] s_axi_wdata, input wire [3:0] s_axi_wstrb,
    input wire s_axi_wvalid, output wire s_axi_wready,
    output reg [1:0] s_axi_bresp, output reg s_axi_bvalid, input wire s_axi_bready,
    input wire [7:0] s_axi_araddr, input wire [2:0] s_axi_arprot,
    input wire s_axi_arvalid, output wire s_axi_arready,
    output reg [31:0] s_axi_rdata, output reg [1:0] s_axi_rresp,
    output reg s_axi_rvalid, input wire s_axi_rready,
    // RGB时序、坐标与frame_start全部同步于pix_clk。
    // frame_start在有效像素0之前的垂直消隐中仅持续一周期。
    input wire frame_start, input wire [11:0] pixel_x, input wire [11:0] pixel_y,
    input wire in_de, input wire in_hs, input wire in_vs, input wire [23:0] in_rgb,
    output reg out_de, output reg out_hs, output reg out_vs, output reg [23:0] out_rgb,
    output wire [31:0] active_cycle_id, output wire marker_visible
);
    localparam integer WIDTH=640;
    (* ASYNC_REG="TRUE" *) reg [1:0] axi_reset_pipe=0, pix_reset_pipe=0;
    wire axi_up=axi_reset_pipe[1], pix_up=pix_reset_pipe[1];
    always @(posedge s_axi_aclk or negedge resetn)
        if(!resetn) axi_reset_pipe<=0; else axi_reset_pipe<={axi_reset_pipe[0],1'b1};
    always @(posedge pix_clk or negedge resetn)
        if(!resetn) pix_reset_pipe<=0; else pix_reset_pipe<={pix_reset_pipe[0],1'b1};

    reg [31:0] shadow[0:31];
    reg aw_pending,w_pending;
    reg [7:0] aw_saved;
    reg [31:0] w_saved;
    reg [3:0] strobe_saved;
    reg [WIDTH-1:0] snapshot;
    reg fifo_write, source_busy;
    wire fifo_full,wr_reset_busy,rd_reset_busy,fifo_empty;
    wire [WIDTH-1:0] fifo_data;
    wire fifo_read;
    reg destination_ack;
    (* ASYNC_REG="TRUE" *) reg ack_meta,ack_sync;
    reg ack_seen;
    wire write_ready=axi_up&&!fifo_full&&!wr_reset_busy;
    assign s_axi_awready=axi_up&&!aw_pending&&!s_axi_bvalid;
    assign s_axi_wready=axi_up&&!w_pending&&!s_axi_bvalid;
    assign s_axi_arready=axi_up&&!s_axi_rvalid;
    integer i,b;
    always @(posedge s_axi_aclk or negedge axi_up) begin
        if(!axi_up) begin ack_meta<=0;ack_sync<=0;end
        else begin ack_meta<=destination_ack;ack_sync<=ack_meta;end
    end
    always @(posedge s_axi_aclk) begin
        if(!axi_up) begin
            aw_pending<=0;w_pending<=0;s_axi_bvalid<=0;s_axi_bresp<=0;
            s_axi_rvalid<=0;s_axi_rdata<=0;s_axi_rresp<=0;
            fifo_write<=0;source_busy<=0;ack_seen<=0;snapshot<=0;
            aw_saved<=0;w_saved<=0;strobe_saved<=0;
            for(i=0;i<32;i=i+1)shadow[i]<=0;
        end else begin
            fifo_write<=0;
            if(source_busy && ack_sync!=ack_seen)begin source_busy<=0;ack_seen<=ack_sync;end
            if(s_axi_bvalid && s_axi_bready)s_axi_bvalid<=0;
            if(s_axi_rvalid && s_axi_rready)s_axi_rvalid<=0;
            if(s_axi_awvalid && s_axi_awready)begin aw_pending<=1;aw_saved<=s_axi_awaddr;end
            if(s_axi_wvalid && s_axi_wready)begin w_pending<=1;w_saved<=s_axi_wdata;strobe_saved<=s_axi_wstrb;end
            if(aw_pending && w_pending && !s_axi_bvalid)begin
                aw_pending<=0;w_pending<=0;s_axi_bvalid<=1;s_axi_bresp<=0;
                if(aw_saved[1:0]!=0)s_axi_bresp<=2'b10;
                else if(aw_saved==8'h0c)begin
                    if(strobe_saved[0] && w_saved[0])begin
                        if(source_busy || !write_ready || (shadow[4]&32'hfffffffc)!=0 ||
                           ((shadow[4]&3)==3 && (shadow[5]==0 || shadow[5]>5000 || shadow[7]==0)))
                            s_axi_bresp<=2'b10;
                        else begin
                            snapshot[31:0]<=shadow[4];snapshot[63:32]<=shadow[5];
                            snapshot[95:64]<=shadow[6];snapshot[127:96]<=shadow[7];
                            for(i=0;i<16;i=i+1)snapshot[128+32*i+:32]<=shadow[16+i];
                            source_busy<=1;fifo_write<=1;
                        end
                    end
                end else if((aw_saved>=8'h10 && aw_saved<=8'h1c) ||
                            (aw_saved>=8'h40 && aw_saved<=8'h7c))begin
                    for(b=0;b<4;b=b+1)if(strobe_saved[b])shadow[aw_saved[7:2]][b*8+:8]<=w_saved[b*8+:8];
                end else s_axi_bresp<=2'b10;
            end
            if(s_axi_arvalid && s_axi_arready)begin
                s_axi_rvalid<=1;s_axi_rresp<=0;s_axi_rdata<=0;
                if(s_axi_araddr[1:0]!=0)s_axi_rresp<=2'b10;
                else case(s_axi_araddr)
                    8'h00:s_axi_rdata<=32'h4d415631;
                    8'h04:s_axi_rdata<=32'h00010000;
                    8'h08:s_axi_rdata<={30'd0,write_ready,source_busy};
                    8'h0c:s_axi_rdata<=0;
                    default:begin
                        if((s_axi_araddr>=8'h10 && s_axi_araddr<=8'h1c) ||
                           (s_axi_araddr>=8'h40 && s_axi_araddr<=8'h7c))
                            s_axi_rdata<=shadow[s_axi_araddr[7:2]];
                        else s_axi_rresp<=2'b10;
                    end
                endcase
            end
        end
    end
    // 完整640位结果作为一个FIFO字跨域，不对每一位分别同步。
    xpm_fifo_async #(
        .FIFO_MEMORY_TYPE("distributed"),.FIFO_WRITE_DEPTH(16),
        .WRITE_DATA_WIDTH(WIDTH),.READ_DATA_WIDTH(WIDTH),.READ_MODE("fwft"),
        .FIFO_READ_LATENCY(0),.CDC_SYNC_STAGES(2),.USE_ADV_FEATURES("0000"),
        // Vivado 2020.2 XPM的可选rd_pntr断言假设异步指针单周期返回，
        // 本设计改由测试平台显式检查接口行为。
        .WR_DATA_COUNT_WIDTH(5),.RD_DATA_COUNT_WIDTH(5),.SIM_ASSERT_CHK(0)
    ) result_fifo (
        .rst(!axi_up),.sleep(1'b0),.wr_clk(s_axi_aclk),.wr_en(fifo_write&&axi_up&&!wr_reset_busy),.din(snapshot),
        .full(fifo_full),.wr_rst_busy(wr_reset_busy),.overflow(),.prog_full(),.almost_full(),.wr_ack(),.wr_data_count(),
        .rd_clk(pix_clk),.rd_en(fifo_read),.dout(fifo_data),.empty(fifo_empty),.rd_rst_busy(rd_reset_busy),
        .underflow(),.prog_empty(),.almost_empty(),.data_valid(),.rd_data_count(),
        .injectsbiterr(1'b0),.injectdbiterr(1'b0),.sbiterr(),.dbiterr()
    );
    reg pending_valid;
    reg [WIDTH-1:0] pending,active;
    reg [31:0] pending_ms,active_ms,ms_divider;
    // 小数相位累加器也支持原厂32.727273MHz模式。
    wire ms_tick=(ms_divider>=PIX_CLK_HZ-1000);
    assign fifo_read=pix_up&&!rd_reset_busy&&!fifo_empty&&!pending_valid;
    assign active_cycle_id=active[95:64];
    assign marker_visible=pix_up&&(active[1:0]==3)&&active_ms!=0;
    always @(posedge pix_clk or negedge pix_up)begin
        if(!pix_up)begin
            pending_valid<=0;pending<=0;active<=0;pending_ms<=0;active_ms<=0;
            ms_divider<=0;destination_ack<=0;
        end else begin
            if(ms_tick)begin
                ms_divider<=ms_divider+1000-PIX_CLK_HZ;
                if(pending_ms!=0)pending_ms<=pending_ms-1;
                if(active_ms!=0)active_ms<=active_ms-1;
            end else ms_divider<=ms_divider+1000;
            if(fifo_read)begin pending<=fifo_data;pending_ms<=fifo_data[63:32];pending_valid<=1;end
            if(pending_valid && frame_start)begin
                active<=pending;
                active_ms<=((pending[1:0]==3) && !(ms_tick&&pending_ms==1))?pending_ms:0;
                pending_valid<=0;destination_ack<=~destination_ack;
            end else if(pending_valid && pending[1:0]==3 &&
                        (pending_ms==0 || (ms_tick&&pending_ms==1)))begin
                pending_valid<=0;destination_ack<=~destination_ack;
            end
        end
    end
    // 匿名16位ID用四个十六进制3×5字形显示，按槽位确定优先级。
    function automatic [14:0] hex_glyph(input [3:0] h);
        case(h)
            0:hex_glyph=15'b111101101101111;1:hex_glyph=15'b010110010010111;
            2:hex_glyph=15'b111001111100111;3:hex_glyph=15'b111001111001111;
            4:hex_glyph=15'b101101111001001;5:hex_glyph=15'b111100111001111;
            6:hex_glyph=15'b111100111101111;7:hex_glyph=15'b111001001001001;
            8:hex_glyph=15'b111101111101111;9:hex_glyph=15'b111101111001111;
            10:hex_glyph=15'b111101111101101;11:hex_glyph=15'b110101110101110;
            12:hex_glyph=15'b111100100100111;13:hex_glyph=15'b110101101101110;
            14:hex_glyph=15'b111100110100111;15:hex_glyph=15'b111100110100100;
        endcase
    endfunction
    integer n,dx,dy,xx,yy,lx,ly,glyph_x,glyph_col;
    reg [31:0] xy,attr;
    reg [14:0] glyph;
    reg hit,pixel_hit,label_hit,label_pixel;
    reg [23:0] color,composed,label_color;
    always @* begin
        composed=in_rgb;hit=0;pixel_hit=0;label_hit=0;label_pixel=0;label_color=0;xy=0;attr=0;dx=0;dy=0;xx=0;yy=0;
        lx=0;ly=0;glyph_x=0;glyph_col=0;glyph=0;color=0;
        for(n=0;n<8;n=n+1)begin
            xy=active[128+64*n+:32];attr=active[160+64*n+:32];
            xx=xy[15:0];yy=xy[31:16];dx=$signed({1'b0,pixel_x})-xx;dy=$signed({1'b0,pixel_y})-yy;
            color=attr[17]?24'h20ff60:24'hffbf40;
            pixel_hit=0;label_pixel=0;
            if(marker_visible && in_de && attr[16] && xx<IMAGE_WIDTH && yy<IMAGE_HEIGHT && attr[15:0]!=0)begin
                if(attr[17])pixel_hit=((dx>=-5 && dx<=5 && dy>=-1 && dy<=1) ||
                                      (dy>=-5 && dy<=5 && dx>=-1 && dx<=1));
                else pixel_hit=(((dx==-5 || dx==5) && dy>=-5 && dy<=5) ||
                                ((dy==-5 || dy==5) && dx>=-5 && dx<=5));
                lx=dx-8;ly=dy+2;
                if(lx>=0 && lx<16 && ly>=0 && ly<5)begin
                    glyph_x=lx/4;glyph_col=lx%4;
                    glyph=hex_glyph((attr[15:0] >> ((3-glyph_x)*4)) & 15);
                    if(glyph_col<3 && glyph[14-(ly*3+glyph_col)])label_pixel=1;
                end
                if(pixel_hit && !hit)begin composed=color;hit=1;end
                if(label_pixel && !label_hit)begin label_color=color;label_hit=1;end
            end
        end
        if(!hit && label_hit)composed=label_color;
    end
    always @(posedge pix_clk)begin
        if(!pix_up)begin out_de<=0;out_hs<=0;out_vs<=0;out_rgb<=0;end
        else begin out_de<=in_de;out_hs<=in_hs;out_vs<=in_vs;out_rgb<=in_de?composed:24'd0;end
    end
    initial begin
        if(IMAGE_WIDTH<1 || IMAGE_WIDTH>4096 || IMAGE_HEIGHT<1 || IMAGE_HEIGHT>4096 ||
           PIX_CLK_HZ<1000)$error("Invalid AV dimensions/pixel clock contract");
    end
endmodule
