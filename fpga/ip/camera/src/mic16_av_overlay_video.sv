`timescale 1ns/1ps
// Audio-result metadata -> complete-frame pixel overlay. No sensor/HDMI timing
// generator. Both clocks must run during common reset; reset all domains together.
// The vendor XPM FIFO owns the multi-bit clock crossing. One update is in flight
// until it is applied at SOF or expires; writes to shadow fields remain allowed.
module mic16_av_overlay_video #(
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
    output wire [1:0] s_axi_bresp, output wire s_axi_bvalid, input wire s_axi_bready,
    input wire [7:0] s_axi_araddr, input wire [2:0] s_axi_arprot,
    input wire s_axi_arvalid, output wire s_axi_arready,
    output wire [31:0] s_axi_rdata, output wire [1:0] s_axi_rresp,
    output wire s_axi_rvalid, input wire s_axi_rready,
    // All RGB timing, coordinates and frame_start are synchronous to pix_clk.
    // frame_start is exactly one cycle in vertical blanking before active pixel 0.
    input wire frame_start, input wire [11:0] pixel_x, input wire [11:0] pixel_y,
    input wire in_de, input wire in_hs, input wire in_vs, input wire [23:0] in_rgb,
    output wire out_de, output wire out_hs, output wire out_vs, output wire [23:0] out_rgb,
    output wire [31:0] active_cycle_id, output wire marker_visible,
 input wire video_clk,input wire [31:0] video_status,input wire [31:0] config_status,
 input wire [31:0] video_complete,input wire [31:0] video_reject,input wire [31:0] video_repeat,
 input wire [31:0] video_supersede,input wire [31:0] video_underflow,input wire [31:0] video_errors,input wire [31:0] video_seq
);
 wire overlay_arready,overlay_rvalid;wire [31:0] overlay_rdata;wire [1:0] overlay_rresp;
 wire diag_arready,diag_rvalid,diag_busy;wire [31:0] diag_rdata;wire [1:0] diag_rresp;
 assign s_axi_arready=s_axi_araddr[7]?(!overlay_rvalid&&diag_arready):(!diag_busy&&overlay_arready);
 assign s_axi_rvalid=overlay_rvalid||diag_rvalid;
 assign s_axi_rdata=overlay_rvalid?overlay_rdata:diag_rdata;
 assign s_axi_rresp=overlay_rvalid?overlay_rresp:diag_rresp;
 mic16_video_diag diag(.reset(!resetn),.ctrl_clk(s_axi_aclk),.video_clk(video_clk),
 .araddr(s_axi_araddr),.arvalid(s_axi_arvalid&&s_axi_araddr[7]&&!overlay_rvalid),.arready(diag_arready),
 .rdata(diag_rdata),.rresp(diag_rresp),.rvalid(diag_rvalid),.rready(s_axi_rready),.busy(diag_busy),
 .status(video_status),.config_status(config_status),.complete_count(video_complete),.reject_count(video_reject),.repeat_count(video_repeat),
 .supersede_count(video_supersede),.underflow_count(video_underflow),.error_count(video_errors),.display_seq(video_seq));
 mic16_av_overlay #(.IMAGE_WIDTH(IMAGE_WIDTH),.IMAGE_HEIGHT(IMAGE_HEIGHT),.PIX_CLK_HZ(PIX_CLK_HZ)) overlay(
 .s_axi_aclk(s_axi_aclk),
 .pix_clk(pix_clk),
 .resetn(resetn),
 .s_axi_awaddr(s_axi_awaddr),
 .s_axi_awprot(s_axi_awprot),
 .s_axi_awvalid(s_axi_awvalid),
 .s_axi_awready(s_axi_awready),
 .s_axi_wdata(s_axi_wdata),
 .s_axi_wstrb(s_axi_wstrb),
 .s_axi_wvalid(s_axi_wvalid),
 .s_axi_wready(s_axi_wready),
 .s_axi_bresp(s_axi_bresp),
 .s_axi_bvalid(s_axi_bvalid),
 .s_axi_bready(s_axi_bready),
 .s_axi_araddr(s_axi_araddr),
 .s_axi_arprot(s_axi_arprot),
 .s_axi_arvalid(s_axi_arvalid&&!s_axi_araddr[7]&&!diag_busy),
 .s_axi_arready(overlay_arready),
 .s_axi_rdata(overlay_rdata),
 .s_axi_rresp(overlay_rresp),
 .s_axi_rvalid(overlay_rvalid),
 .s_axi_rready(s_axi_rready),
 .frame_start(frame_start),
 .pixel_x(pixel_x),
 .pixel_y(pixel_y),
 .in_de(in_de),
 .in_hs(in_hs),
 .in_vs(in_vs),
 .in_rgb(in_rgb),
 .out_de(out_de),
 .out_hs(out_hs),
 .out_vs(out_vs),
 .out_rgb(out_rgb),
 .active_cycle_id(active_cycle_id),
 .marker_visible(marker_visible)
 );
endmodule
