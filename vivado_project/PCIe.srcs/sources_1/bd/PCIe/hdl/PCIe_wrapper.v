//Copyright 1986-2022 Xilinx, Inc. All Rights Reserved.
//Copyright 2022-2023 Advanced Micro Devices, Inc. All Rights Reserved.
//--------------------------------------------------------------------------------
//Tool Version: Vivado v.2023.2 (win64) Build 4029153 Fri Oct 13 20:14:34 MDT 2023
//Date        : Wed Jun  3 17:00:09 2026
//Host        : DESKTOP-L3C1TEJ running 64-bit major release  (build 9200)
//Command     : generate_target PCIe_wrapper.bd
//Design      : PCIe_wrapper
//Purpose     : IP block netlist
//--------------------------------------------------------------------------------
`timescale 1 ps / 1 ps

module PCIe_wrapper
   (DDR3_addr,
    DDR3_ba,
    DDR3_cas_n,
    DDR3_ck_n,
    DDR3_ck_p,
    DDR3_cke,
    DDR3_cs_n,
    DDR3_dm,
    DDR3_dq,
    DDR3_dqs_n,
    DDR3_dqs_p,
    DDR3_odt,
    DDR3_ras_n,
    DDR3_reset_n,
    DDR3_we_n,
    iic_hdmi_scl_io,
    iic_hdmi_sda_io,
    pcie_mgt_0_rxn,
    pcie_mgt_0_rxp,
    pcie_mgt_0_txn,
    pcie_mgt_0_txp,
    reset_n,
    sys_clk_clk_n,
    sys_clk_clk_p,
    sys_ddr_clk_n,
    sys_ddr_clk_p,
    sys_rst_n,
    uart_rtl_0_rxd,
    uart_rtl_0_txd,
    vid_aclk,
    vid_active_video_0,
    vid_aresetn,
    vid_data_0,
    vid_hsync_0,
    vid_vsync_0);
  output [14:0]DDR3_addr;
  output [2:0]DDR3_ba;
  output DDR3_cas_n;
  output [0:0]DDR3_ck_n;
  output [0:0]DDR3_ck_p;
  output [0:0]DDR3_cke;
  output [0:0]DDR3_cs_n;
  output [3:0]DDR3_dm;
  inout [31:0]DDR3_dq;
  inout [3:0]DDR3_dqs_n;
  inout [3:0]DDR3_dqs_p;
  output [0:0]DDR3_odt;
  output DDR3_ras_n;
  output DDR3_reset_n;
  output DDR3_we_n;
  inout iic_hdmi_scl_io;
  inout iic_hdmi_sda_io;
  input [3:0]pcie_mgt_0_rxn;
  input [3:0]pcie_mgt_0_rxp;
  output [3:0]pcie_mgt_0_txn;
  output [3:0]pcie_mgt_0_txp;
  input reset_n;
  input [0:0]sys_clk_clk_n;
  input [0:0]sys_clk_clk_p;
  input sys_ddr_clk_n;
  input sys_ddr_clk_p;
  input sys_rst_n;
  input uart_rtl_0_rxd;
  output uart_rtl_0_txd;
  output vid_aclk;
  output vid_active_video_0;
  output [0:0]vid_aresetn;
  output [23:0]vid_data_0;
  output vid_hsync_0;
  output vid_vsync_0;

  wire [14:0]DDR3_addr;
  wire [2:0]DDR3_ba;
  wire DDR3_cas_n;
  wire [0:0]DDR3_ck_n;
  wire [0:0]DDR3_ck_p;
  wire [0:0]DDR3_cke;
  wire [0:0]DDR3_cs_n;
  wire [3:0]DDR3_dm;
  wire [31:0]DDR3_dq;
  wire [3:0]DDR3_dqs_n;
  wire [3:0]DDR3_dqs_p;
  wire [0:0]DDR3_odt;
  wire DDR3_ras_n;
  wire DDR3_reset_n;
  wire DDR3_we_n;
  wire iic_hdmi_scl_i;
  wire iic_hdmi_scl_io;
  wire iic_hdmi_scl_o;
  wire iic_hdmi_scl_t;
  wire iic_hdmi_sda_i;
  wire iic_hdmi_sda_io;
  wire iic_hdmi_sda_o;
  wire iic_hdmi_sda_t;
  wire [3:0]pcie_mgt_0_rxn;
  wire [3:0]pcie_mgt_0_rxp;
  wire [3:0]pcie_mgt_0_txn;
  wire [3:0]pcie_mgt_0_txp;
  wire reset_n;
  wire [0:0]sys_clk_clk_n;
  wire [0:0]sys_clk_clk_p;
  wire sys_ddr_clk_n;
  wire sys_ddr_clk_p;
  wire sys_rst_n;
  wire uart_rtl_0_rxd;
  wire uart_rtl_0_txd;
  wire vid_aclk;
  wire vid_active_video_0;
  wire [0:0]vid_aresetn;
  wire [23:0]vid_data_0;
  wire vid_hsync_0;
  wire vid_vsync_0;

  PCIe PCIe_i
       (.DDR3_addr(DDR3_addr),
        .DDR3_ba(DDR3_ba),
        .DDR3_cas_n(DDR3_cas_n),
        .DDR3_ck_n(DDR3_ck_n),
        .DDR3_ck_p(DDR3_ck_p),
        .DDR3_cke(DDR3_cke),
        .DDR3_cs_n(DDR3_cs_n),
        .DDR3_dm(DDR3_dm),
        .DDR3_dq(DDR3_dq),
        .DDR3_dqs_n(DDR3_dqs_n),
        .DDR3_dqs_p(DDR3_dqs_p),
        .DDR3_odt(DDR3_odt),
        .DDR3_ras_n(DDR3_ras_n),
        .DDR3_reset_n(DDR3_reset_n),
        .DDR3_we_n(DDR3_we_n),
        .iic_hdmi_scl_i(iic_hdmi_scl_i),
        .iic_hdmi_scl_o(iic_hdmi_scl_o),
        .iic_hdmi_scl_t(iic_hdmi_scl_t),
        .iic_hdmi_sda_i(iic_hdmi_sda_i),
        .iic_hdmi_sda_o(iic_hdmi_sda_o),
        .iic_hdmi_sda_t(iic_hdmi_sda_t),
        .pcie_mgt_0_rxn(pcie_mgt_0_rxn),
        .pcie_mgt_0_rxp(pcie_mgt_0_rxp),
        .pcie_mgt_0_txn(pcie_mgt_0_txn),
        .pcie_mgt_0_txp(pcie_mgt_0_txp),
        .reset_n(reset_n),
        .sys_clk_clk_n(sys_clk_clk_n),
        .sys_clk_clk_p(sys_clk_clk_p),
        .sys_ddr_clk_n(sys_ddr_clk_n),
        .sys_ddr_clk_p(sys_ddr_clk_p),
        .sys_rst_n(sys_rst_n),
        .uart_rtl_0_rxd(uart_rtl_0_rxd),
        .uart_rtl_0_txd(uart_rtl_0_txd),
        .vid_aclk(vid_aclk),
        .vid_active_video_0(vid_active_video_0),
        .vid_aresetn(vid_aresetn),
        .vid_data_0(vid_data_0),
        .vid_hsync_0(vid_hsync_0),
        .vid_vsync_0(vid_vsync_0));
  IOBUF iic_hdmi_scl_iobuf
       (.I(iic_hdmi_scl_o),
        .IO(iic_hdmi_scl_io),
        .O(iic_hdmi_scl_i),
        .T(iic_hdmi_scl_t));
  IOBUF iic_hdmi_sda_iobuf
       (.I(iic_hdmi_sda_o),
        .IO(iic_hdmi_sda_io),
        .O(iic_hdmi_sda_i),
        .T(iic_hdmi_sda_t));
endmodule
