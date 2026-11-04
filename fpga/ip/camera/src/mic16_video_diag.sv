`timescale 1ns/1ps
// Read-only diagnostic sub-bank. Request/response FIFOs take each 32-bit
// snapshot in the video clock domain; no asynchronous multi-bit counter reads.
module mic16_video_diag(
 input wire reset,input wire ctrl_clk,input wire video_clk,
 input wire [7:0] araddr,input wire arvalid,output wire arready,
 output reg [31:0] rdata,output reg [1:0] rresp,output reg rvalid,input wire rready,
 output wire busy,
 input wire [31:0] status,input wire [31:0] config_status,input wire [31:0] complete_count,input wire [31:0] reject_count,
 input wire [31:0] repeat_count,input wire [31:0] supersede_count,input wire [31:0] underflow_count,
 input wire [31:0] error_count,input wire [31:0] display_seq
);
 wire ctrl_reset;
 xpm_cdc_async_rst #(.DEST_SYNC_FF(3),.INIT_SYNC_FF(1),.RST_ACTIVE_HIGH(1)) ctrl_reset_sync(
 .src_arst(reset),.dest_clk(ctrl_clk),.dest_arst(ctrl_reset));
 reg pending;wire qready,qvalid,reply_ready,reply_valid;wire [7:0] query;wire [33:0] reply;
 reg [31:0] value;reg [1:0] response;
 assign busy=ctrl_reset||pending||rvalid;assign arready=!busy&&qready;
 mic16_video_queue #(.WIDTH(8)) req(reset,ctrl_clk,video_clk,araddr,arvalid&&arready,qready,query,qvalid,reply_ready);
 always @* begin
  value=0;response=0;
  case(query)
   8'h80:value=status;8'h84:value=complete_count;8'h88:value=reject_count;8'h8c:value=repeat_count;
   8'h90:value=supersede_count;8'h94:value=underflow_count;8'h98:value=error_count;8'h9c:value=display_seq;8'ha0:value=config_status;
   default:response=2;
  endcase
 end
 mic16_video_queue #(.WIDTH(34)) res(reset,video_clk,ctrl_clk,{response,value},qvalid,reply_ready,reply,reply_valid,pending&&!rvalid);
 always @(posedge ctrl_clk)begin
  if(ctrl_reset)begin pending<=0;rvalid<=0;rdata<=0;rresp<=0;end
  else begin
   if(rvalid&&rready)rvalid<=0;
   if(arvalid&&arready)pending<=1;
   if(pending&&!rvalid&&reply_valid)begin rvalid<=1;rdata<=reply[31:0];rresp<=reply[33:32];pending<=0;end
  end
 end
endmodule
