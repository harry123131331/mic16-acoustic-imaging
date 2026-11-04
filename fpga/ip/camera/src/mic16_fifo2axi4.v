`timescale 1ns/1ps
/////////////////////////////////////////////////////////////////////////////////
// Company       : 武汉芯路恒科技有限公司
//                 http://xiaomeige.taobao.com
// Web           : http://www.corecourse.cn
// 
// Create Date   : 2019/05/01 00:00:00
// Module Name   : fifo2axi4
// Description   : fifo接口到AXI4接口的转换模块(不含FIFO)
// 
// Dependencies  : 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
/////////////////////////////////////////////////////////////////////////////////

module mic16_fifo2axi4
#(
  parameter WR_AXI_BYTE_ADDR_BEGIN = 0      ,
  parameter WR_AXI_BYTE_ADDR_END   = 200    ,
  parameter RD_AXI_BYTE_ADDR_BEGIN = 0      ,
  parameter RD_AXI_BYTE_ADDR_END   = 200    ,

  parameter AXI_DATA_WIDTH         = 128    ,
  parameter AXI_ADDR_WIDTH         = 28     ,
  parameter AXI_ID                 = 4'b0000,
  parameter AXI_BURST_LEN          = 8'd31    // 突发长度参数为32。
)
(
  // 时钟及复位。
  input                              clk              ,
  input                              reset            ,
  // 写FIFO写端接口。
  input                              wr_addr_clr      ,// 同步时钟。
  output                             wr_fifo_rdreq    ,
  input      [AXI_DATA_WIDTH-1:0]    wr_fifo_rddata   ,
  input                              wr_fifo_empty    ,
  input      [5:0]                   wr_fifo_rd_cnt   ,
  input                              wr_fifo_rst_busy ,
  // 读FIFO读端接口。
  input                              rd_addr_clr      ,
  output                             rd_fifo_wrreq    ,
  output     [AXI_DATA_WIDTH-1:0]    rd_fifo_wrdata   ,
  input                              rd_fifo_alfull   ,
  input      [5:0]                   rd_fifo_wr_cnt   ,
  input                              rd_fifo_rst_busy ,
  // 主接口写地址通道。
  output     [3:0]                   m_axi_awid       ,
  output reg [AXI_ADDR_WIDTH-1:0]    m_axi_awaddr     ,
  output     [7:0]                   m_axi_awlen      ,
  output     [2:0]                   m_axi_awsize     ,
  output     [1:0]                   m_axi_awburst    ,
  output     [0:0]                   m_axi_awlock     ,
  output     [3:0]                   m_axi_awcache    ,
  output     [2:0]                   m_axi_awprot     ,
  output     [3:0]                   m_axi_awqos      ,
  output     [3:0]                   m_axi_awregion   ,
  output reg                         m_axi_awvalid    ,
  input                              m_axi_awready    ,
  // 主接口写数据通道。
  output     [AXI_DATA_WIDTH-1:0]    m_axi_wdata      ,
  output     [AXI_DATA_WIDTH/8-1:0]  m_axi_wstrb      ,
  output reg                         m_axi_wlast      ,
  output wire                        m_axi_wvalid     ,
  input                              m_axi_wready     ,
  // 主接口写响应通道。
  input      [3:0]                   m_axi_bid        ,
  input      [1:0]                   m_axi_bresp      ,
  input                              m_axi_bvalid     ,
  output                             m_axi_bready     ,
  // 主接口读地址通道。
  output     [3:0]                   m_axi_arid       ,
  output reg [AXI_ADDR_WIDTH-1:0]    m_axi_araddr     ,
  output     [7:0]                   m_axi_arlen      ,
  output     [2:0]                   m_axi_arsize     ,
  output     [1:0]                   m_axi_arburst    ,
  output     [0:0]                   m_axi_arlock     ,
  output     [3:0]                   m_axi_arcache    ,
  output     [2:0]                   m_axi_arprot     ,
  output     [3:0]                   m_axi_arqos      ,
  output     [3:0]                   m_axi_arregion   ,
  output reg                         m_axi_arvalid    ,
  input                              m_axi_arready    ,
  // 主接口读数据通道。
  input      [3:0]                   m_axi_rid        ,
  input      [AXI_DATA_WIDTH-1:0]    m_axi_rdata      ,
  input      [1:0]                   m_axi_rresp      ,
  input                              m_axi_rlast      ,
  input                              m_axi_rvalid     ,
  output                             m_axi_rready     
);

  localparam S_IDLE    = 7'b0000001,
             S_ARB     = 7'b0000010,
             S_WR_ADDR = 7'b0000100,
             S_WR_DATA = 7'b0001000,
             S_WR_RESP = 7'b0010000,
             S_RD_ADDR = 7'b0100000,
             S_RD_RESP = 7'b1000000;

  localparam DATA_SIZE = clogb2(AXI_DATA_WIDTH/8-1);

  wire [7:0] wr_req_cnt_thresh;
  wire [7:0] rd_req_cnt_thresh;
  wire       wr_ddr3_req;
  wire       rd_ddr3_req;
  reg  [6:0] curr_state;
  reg  [6:0] next_state;
  reg        wr_rd_poll;   // 0允许写，1允许读。
  reg  [7:0] wr_data_cnt;
  reg wvalid_int;
  // 首字直通FIFO的数据计数是近似值，实际出队必须受EMPTY控制，
  // 不能猜测加2修正；停顿拍中数据与VALID保持稳定。
  assign m_axi_wvalid = wvalid_int && !wr_fifo_empty && !wr_fifo_rst_busy;
  // 换帧请求是事务边界，已经拉高的AXI VALID不能撤销。
  // 先排空旧事务并丢弃其剩余数据，再重新启动。
  reg rd_flush_pending;
  wire rd_drop = rd_flush_pending || rd_addr_clr || rd_fifo_rst_busy;
  always @(posedge clk or posedge reset) begin
    if(reset) rd_flush_pending <= 1'b1;
    else if(rd_addr_clr || rd_fifo_rst_busy) rd_flush_pending <= 1'b1;
    else if((curr_state == S_ARB) && !rd_addr_clr && !rd_fifo_rst_busy)
      rd_flush_pending <= 1'b0;
  end

  assign m_axi_awid    = AXI_ID[3:0];
  assign m_axi_awsize  = DATA_SIZE;
  assign m_axi_awburst = 2'b01;
  assign m_axi_awlock  = 1'b0;
  assign m_axi_awcache = 4'b0000;
  assign m_axi_awprot  = 3'b000;
  assign m_axi_awqos   = 4'b0000;
  assign m_axi_awregion= 4'b0000;
  assign m_axi_awlen   = AXI_BURST_LEN[7:0];
  
  assign m_axi_wstrb   = 16'hffff;
  assign m_axi_wdata   = wr_fifo_rddata;

  assign m_axi_bready  = 1'b1;

  assign m_axi_arid    = AXI_ID[3:0];
  assign m_axi_arsize  = DATA_SIZE;
  assign m_axi_arburst = 2'b01;
  assign m_axi_arlock  = 1'b0;
  assign m_axi_arcache = 4'b0000;
  assign m_axi_arprot  = 3'b000;
  assign m_axi_arqos   = 4'b0000;
  assign m_axi_arregion= 4'b0000;
  assign m_axi_arlen   = AXI_BURST_LEN[7:0];

  assign m_axi_rready  = rd_drop || ~rd_fifo_alfull;

  assign wr_fifo_rdreq  = m_axi_wvalid && m_axi_wready;
  assign rd_fifo_wrreq  = (curr_state == S_RD_RESP) && m_axi_rvalid && m_axi_rready && !rd_drop;
  assign rd_fifo_wrdata = m_axi_rdata;

  assign wr_req_cnt_thresh = (m_axi_awlen == 'd0)? 'd0 : (AXI_BURST_LEN[7:0]-1'b1);
  assign rd_req_cnt_thresh = AXI_BURST_LEN[7:0];
  assign wr_ddr3_req = (wr_fifo_rst_busy == 1'b0) && (!wr_fifo_empty) && (wr_fifo_rd_cnt >= wr_req_cnt_thresh) ? 1'b1:1'b0;
  assign rd_ddr3_req = !rd_drop && (rd_fifo_wr_cnt <= rd_req_cnt_thresh) ? 1'b1:1'b0;

  // 写地址信号m_axi_awaddr。
  always@(posedge clk or posedge reset)
  begin
    if(reset)
      m_axi_awaddr <= WR_AXI_BYTE_ADDR_BEGIN;
    else if(wr_addr_clr)
      m_axi_awaddr <= WR_AXI_BYTE_ADDR_BEGIN;
    else if(m_axi_awaddr >= WR_AXI_BYTE_ADDR_END)
      m_axi_awaddr <= WR_AXI_BYTE_ADDR_BEGIN;
    else if((curr_state == S_WR_RESP) && m_axi_bready && m_axi_bvalid && (m_axi_bresp == 2'b00) && (m_axi_bid == AXI_ID))
      m_axi_awaddr <= m_axi_awaddr + ((m_axi_awlen + 1'b1)*(AXI_DATA_WIDTH/8));
    else
      m_axi_awaddr <= m_axi_awaddr;
  end

  // 写地址有效m_axi_awvalid。
  always@(posedge clk or posedge reset)
  begin
    if(reset)
      m_axi_awvalid <= 1'b0;
    else if((curr_state == S_WR_ADDR) && m_axi_awready && m_axi_awvalid)
      m_axi_awvalid <= 1'b0;
    else if(curr_state == S_WR_ADDR)
      m_axi_awvalid <= 1'b1;
    else
      m_axi_awvalid <= m_axi_awvalid;
  end

  // 写地址有效m_axi_awvalid。
  always@(posedge clk or posedge reset)
  begin
    if(reset)
      wvalid_int <= 1'b0;
    else if((curr_state == S_WR_DATA) && m_axi_wready && m_axi_wvalid && m_axi_wlast)
      wvalid_int <= 1'b0;
    else if(curr_state == S_WR_DATA)
      wvalid_int <= 1'b1;
    else
      wvalid_int <= wvalid_int;
  end

  // 写数据计数wr_data_cnt。
  always@(posedge clk or posedge reset)
  begin
    if(reset)
      wr_data_cnt <= 1'b0;
    else if(curr_state == S_ARB)
      wr_data_cnt <= 1'b0;
    else if(curr_state == S_WR_DATA && m_axi_wready && m_axi_wvalid)
      wr_data_cnt <= wr_data_cnt + 1'b1;
    else
      wr_data_cnt <= wr_data_cnt;
  end

  // 写末拍m_axi_wlast。
  always@(posedge clk or posedge reset)
  begin
    if(reset)
      m_axi_wlast <= 1'b0;
    else if(curr_state == S_WR_DATA && m_axi_wready && m_axi_wvalid && m_axi_wlast)
      m_axi_wlast <= 1'b0;
    else if(curr_state == S_WR_DATA && m_axi_awlen == 8'd0)
      m_axi_wlast <= 1'b1;
    else if(curr_state == S_WR_DATA && m_axi_wready && m_axi_wvalid && (wr_data_cnt == m_axi_awlen -1'b1))
      m_axi_wlast <= 1'b1;
    else
      m_axi_wlast <= m_axi_wlast;
  end

  // 读地址m_axi_araddr。
  always@(posedge clk or posedge reset)
  begin
    if(reset)
      m_axi_araddr <= RD_AXI_BYTE_ADDR_BEGIN;
    else if(rd_drop && (curr_state == S_ARB) && !rd_addr_clr && !rd_fifo_rst_busy)
      m_axi_araddr <= RD_AXI_BYTE_ADDR_BEGIN;
    else if(m_axi_araddr >= RD_AXI_BYTE_ADDR_END)
      m_axi_araddr <= RD_AXI_BYTE_ADDR_BEGIN;
    else if((curr_state == S_RD_RESP) && m_axi_rready && m_axi_rvalid && m_axi_rlast && (m_axi_rresp == 2'b00) && (m_axi_rid == AXI_ID))
      m_axi_araddr <= rd_drop ? RD_AXI_BYTE_ADDR_BEGIN : m_axi_araddr + ((m_axi_arlen + 1'b1)*(AXI_DATA_WIDTH/8));
    else
      m_axi_araddr <= m_axi_araddr;
  end

  // 读地址有效m_axi_arvalid。
  always@(posedge clk or posedge reset)
  begin
    if(reset)
      m_axi_arvalid <= 1'b0;
    else if((curr_state == S_RD_ADDR) && m_axi_arready && m_axi_arvalid)
      m_axi_arvalid <= 1'b0;
    else if(curr_state == S_RD_ADDR)
      m_axi_arvalid <= 1'b1;
    else
      m_axi_arvalid <= m_axi_arvalid;
  end

  always@(posedge clk or posedge reset)
  begin
    if(reset)
      wr_rd_poll <= 1'b0;
    else if(curr_state == S_ARB)
      wr_rd_poll <= ~wr_rd_poll;
    else
      wr_rd_poll <= wr_rd_poll;
  end

  //**********************************
  // 状态机。
  //**********************************
  always@(posedge clk or posedge reset)
  begin
    if(reset)
      curr_state <= S_IDLE;
    else
      curr_state <= next_state;
  end

  always@(*)
  begin
    case(curr_state)
      S_IDLE:
      begin
        next_state = S_ARB;
      end

      S_ARB:
      begin
        if((wr_ddr3_req == 1'b1) && (wr_rd_poll == 1'b0))
          next_state = S_WR_ADDR;
        else if((rd_ddr3_req == 1'b1) && (wr_rd_poll == 1'b1))
          next_state = S_RD_ADDR;
        else
          next_state = S_ARB;
      end

      S_WR_ADDR:
      begin
        if(m_axi_awready && m_axi_awvalid)
          next_state = S_WR_DATA;
        else
          next_state = S_WR_ADDR;
      end

      S_WR_DATA:
      begin
        if(m_axi_wready && m_axi_wvalid && m_axi_wlast)
          next_state = S_WR_RESP;
        else
          next_state = S_WR_DATA;
      end

      S_WR_RESP:
      begin
        if(m_axi_bready && m_axi_bvalid && (m_axi_bresp == 2'b00) && (m_axi_bid == AXI_ID))
          next_state = S_ARB;
        else if(m_axi_bready && m_axi_bvalid)
          next_state = S_IDLE;
        else
          next_state = S_WR_RESP;
      end

      S_RD_ADDR:
      begin
        if(m_axi_arready && m_axi_arvalid)
          next_state = S_RD_RESP;
        else
          next_state = S_RD_ADDR;
      end

      S_RD_RESP:
      begin
        if(m_axi_rready && m_axi_rvalid && m_axi_rlast && (m_axi_rresp == 2'b00) && (m_axi_rid == AXI_ID))
          next_state = S_ARB;
        else if(m_axi_rready && m_axi_rvalid && m_axi_rlast)
          next_state = S_IDLE;
        else
          next_state = S_RD_RESP;
      end

      default: next_state = S_IDLE;
    endcase
  end

  // 根据AXI_DATA_WIDTH计算awsize/arsize宽度。
  function integer clogb2;
    input integer axi_data_byte;
      for (clogb2=0; axi_data_byte>0; clogb2=clogb2+1)
        axi_data_byte = axi_data_byte >> 1;
  endfunction

endmodule
