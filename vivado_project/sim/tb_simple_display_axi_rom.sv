`timescale 1ns / 1ps

module tb_simple_display_axi_rom;
  localparam [31:0] ROM_BASE = 32'h0100_0000;
  localparam integer ROM_WORDS = 32768 / 4;

  reg [31:0] expected_rom [0:ROM_WORDS-1];
  initial $readmemh("simple_display_gop_option_rom_32.mem", expected_rom);

  reg clk = 1'b0;
  reg resetn = 1'b0;
  always #4 clk = ~clk;

  reg [3:0] awid = 0;
  reg [31:0] awaddr = 0;
  reg [7:0] awlen = 0;
  reg [2:0] awsize = 3'd2;
  reg [1:0] awburst = 2'b01;
  reg awlock = 0;
  reg [3:0] awcache = 0;
  reg [2:0] awprot = 0;
  reg [3:0] awqos = 0;
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
  reg bready = 1;

  reg [3:0] arid = 0;
  reg [31:0] araddr = 0;
  reg [7:0] arlen = 0;
  reg [2:0] arsize = 3'd2;
  reg [1:0] arburst = 2'b01;
  reg arlock = 0;
  reg [3:0] arcache = 0;
  reg [2:0] arprot = 0;
  reg [3:0] arqos = 0;
  reg arvalid = 0;
  wire arready;
  wire [3:0] rid;
  wire [31:0] rdata;
  wire [1:0] rresp;
  wire rlast;
  wire rvalid;
  reg rready = 0;

  simple_display_axi_rom dut (
    .s_axi_aclk(clk),
    .s_axi_aresetn(resetn),
    .s_axi_awid(awid),
    .s_axi_awaddr(awaddr),
    .s_axi_awlen(awlen),
    .s_axi_awsize(awsize),
    .s_axi_awburst(awburst),
    .s_axi_awlock(awlock),
    .s_axi_awcache(awcache),
    .s_axi_awprot(awprot),
    .s_axi_awqos(awqos),
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
    .s_axi_arlock(arlock),
    .s_axi_arcache(arcache),
    .s_axi_arprot(arprot),
    .s_axi_arqos(arqos),
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

  task automatic read_single;
    input [31:0] address;
    input [31:0] expected_data;
    begin
      issue_read(address, 0, 4'ha);
      accept_read_beat(expected_data, 4'ha, 1'b1);
    end
  endtask

  task automatic write_two_beats;
    input [31:0] address;
    begin
      @(negedge clk);
      awid = 4'h7;
      awaddr = address;
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
      if (bid !== 4'h7 || bresp !== 2'b00)
        $fatal(1, "bad write response BID=%x BRESP=%x", bid, bresp);
      @(negedge clk);
    end
  endtask

  reg [31:0] stalled_data;
  reg stalled_last;
  integer index;
  initial begin
    repeat (5) @(posedge clk);
    resetn <= 1'b1;
    repeat (2) @(posedge clk);

    read_single(ROM_BASE, 32'h0040_aa55);
    read_single(ROM_BASE + 32'h04, 32'h0000_0ef1);
    read_single(ROM_BASE + 32'h08, 32'h8664_000b);
    read_single(ROM_BASE + 32'h1c, 32'h5249_4350);
    read_single(ROM_BASE + 32'h20, 32'h7024_10ee);
    read_single(ROM_BASE + 32'h2c, 32'h0000_0040);
    read_single(ROM_BASE + 32'h30, 32'h0000_8003);
    read_single(ROM_BASE + 32'h200, 32'h0000_5a4d);
    read_single(ROM_BASE + 32'h7ffc, expected_rom[ROM_WORDS-1]);

    issue_read(ROM_BASE + 32'h60, 3, 4'h5);
    while (!rvalid)
      @(negedge clk);
    stalled_data = rdata;
    stalled_last = rlast;
    repeat (4) begin
      @(negedge clk);
      if (!rvalid || rdata !== stalled_data || rlast !== stalled_last)
        $fatal(1, "read response changed while backpressured");
    end
    for (index = 0; index < 4; index = index + 1)
      accept_read_beat(32'hffff_ffff, 4'h5, index == 3);

    write_two_beats(ROM_BASE);
    read_single(ROM_BASE, 32'h0040_aa55);

    issue_read(32'h0200_0000, 0, 4'h3);
    rready = 1'b1;
    while (!rvalid)
      @(negedge clk);
    if (rresp !== 2'b10 || rdata !== 32'hffff_ffff || !rlast)
      $fatal(1, "out-of-range read did not return SLVERR/all ones");
    @(negedge clk);
    rready = 1'b0;

    $display("SIMPLE_DISPLAY_AXI_ROM_UNIT_PASS");
    $finish;
  end
endmodule
