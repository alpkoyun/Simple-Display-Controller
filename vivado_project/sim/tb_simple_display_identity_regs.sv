`timescale 1ns / 1ps

module tb_simple_display_identity_regs;
  localparam [31:0] IDENTITY_BASE = 32'h3c08_0000;

  reg clk = 1'b0;
  reg resetn = 1'b0;
  always #4 clk = ~clk;

  reg [3:0] awid = 0;
  reg [31:0] awaddr = 0;
  reg [7:0] awlen = 0;
  reg [2:0] awsize = 3'd2;
  reg [1:0] awburst = 2'b01;
  reg awvalid = 0;
  wire awready;
  reg [31:0] wdata = 0;
  reg [3:0] wstrb = 4'hf;
  reg wlast = 0;
  reg wvalid = 0;
  wire wready;
  wire [3:0] bid;
  wire [1:0] bresp;
  wire bvalid;
  reg bready = 0;

  reg [3:0] arid = 0;
  reg [31:0] araddr = 0;
  reg [7:0] arlen = 0;
  reg [2:0] arsize = 3'd2;
  reg [1:0] arburst = 2'b01;
  reg arvalid = 0;
  wire arready;
  wire [3:0] rid;
  wire [31:0] rdata;
  wire [1:0] rresp;
  wire rlast;
  wire rvalid;
  reg rready = 0;

  simple_display_identity_regs dut (
    .s_axi_aclk(clk),
    .s_axi_aresetn(resetn),
    .s_axi_awid(awid),
    .s_axi_awaddr(awaddr),
    .s_axi_awlen(awlen),
    .s_axi_awsize(awsize),
    .s_axi_awburst(awburst),
    .s_axi_awvalid(awvalid),
    .s_axi_awready(awready),
    .s_axi_wdata(wdata),
    .s_axi_wstrb(wstrb),
    .s_axi_wlast(wlast),
    .s_axi_wvalid(wvalid),
    .s_axi_wready(wready),
    .s_axi_bid(bid),
    .s_axi_bresp(bresp),
    .s_axi_bvalid(bvalid),
    .s_axi_bready(bready),
    .s_axi_arid(arid),
    .s_axi_araddr(araddr),
    .s_axi_arlen(arlen),
    .s_axi_arsize(arsize),
    .s_axi_arburst(arburst),
    .s_axi_arvalid(arvalid),
    .s_axi_arready(arready),
    .s_axi_rid(rid),
    .s_axi_rdata(rdata),
    .s_axi_rresp(rresp),
    .s_axi_rlast(rlast),
    .s_axi_rvalid(rvalid),
    .s_axi_rready(rready)
  );

  task automatic issue_read;
    input [31:0] address;
    input [7:0] length;
    input [3:0] id;
    begin
      @(negedge clk);
      araddr = address;
      arlen = length;
      arid = id;
      arsize = 3'd2;
      arburst = 2'b01;
      arvalid = 1'b1;
      while (!arready)
        @(negedge clk);
      @(negedge clk);
      arvalid = 1'b0;
    end
  endtask

  task automatic accept_read_beat;
    input [31:0] expected_data;
    input [3:0] expected_id;
    input expected_last;
    begin
      rready = 1'b1;
      while (!rvalid)
        @(negedge clk);
      if (rdata !== expected_data)
        $fatal(1, "read data mismatch: got=%08x expected=%08x", rdata, expected_data);
      if (rid !== expected_id)
        $fatal(1, "read ID mismatch: got=%x expected=%x", rid, expected_id);
      if (rresp !== 2'b00)
        $fatal(1, "unexpected read response: %x", rresp);
      if (rlast !== expected_last)
        $fatal(1, "RLAST mismatch: got=%x expected=%x", rlast, expected_last);
      @(negedge clk);
      rready = 1'b0;
    end
  endtask

  task automatic read_identity_burst;
    input [3:0] id;
    begin
      issue_read(IDENTITY_BASE, 3, id);
      accept_read_beat(32'h3143_4453, id, 1'b0);
      accept_read_beat(32'h0001_0000, id, 1'b0);
      accept_read_beat(32'h0000_0003, id, 1'b0);
      accept_read_beat(32'd32768, id, 1'b1);
    end
  endtask

  task automatic write_two_beats;
    input [3:0] id;
    begin
      @(negedge clk);
      awid = id;
      awaddr = IDENTITY_BASE;
      awlen = 1;
      awvalid = 1'b1;
      while (!awready)
        @(negedge clk);
      @(negedge clk);
      awvalid = 1'b0;

      wdata = 32'hdead_beef;
      wlast = 1'b0;
      wvalid = 1'b1;
      while (!wready)
        @(negedge clk);
      @(negedge clk);
      wdata = 32'hcafe_f00d;
      wlast = 1'b1;
      while (!wready)
        @(negedge clk);
      @(negedge clk);
      wvalid = 1'b0;
      wlast = 1'b0;

      while (!bvalid)
        @(negedge clk);
      repeat (3) begin
        @(negedge clk);
        if (!bvalid || bid !== id || bresp !== 2'b00)
          $fatal(1, "write response changed under backpressure");
      end
      bready = 1'b1;
      @(negedge clk);
      bready = 1'b0;
    end
  endtask

  reg [31:0] stalled_data;
  reg [3:0] stalled_id;
  reg stalled_last;
  initial begin
    repeat (5) @(posedge clk);
    resetn <= 1'b1;
    repeat (2) @(posedge clk);

    issue_read(IDENTITY_BASE, 3, 4'h9);
    while (!rvalid)
      @(negedge clk);
    stalled_data = rdata;
    stalled_id = rid;
    stalled_last = rlast;
    repeat (4) begin
      @(negedge clk);
      if (!rvalid || rdata !== stalled_data || rid !== stalled_id ||
          rlast !== stalled_last)
        $fatal(1, "read response changed while backpressured");
    end
    accept_read_beat(32'h3143_4453, 4'h9, 1'b0);
    accept_read_beat(32'h0001_0000, 4'h9, 1'b0);
    accept_read_beat(32'h0000_0003, 4'h9, 1'b0);
    accept_read_beat(32'd32768, 4'h9, 1'b1);

    write_two_beats(4'h6);
    read_identity_burst(4'hb);

    issue_read(IDENTITY_BASE + 32'h10, 0, 4'h2);
    accept_read_beat(32'h0000_0000, 4'h2, 1'b1);

    $display("SIMPLE_DISPLAY_SDC1_AXI_UNIT_PASS");
    $finish;
  end
endmodule
