##-----------------------------------------------------------------------------
##
## (c) Copyright 2010-2011 Xilinx, Inc. All rights reserved.
##
## This file contains confidential and proprietary information
## of Xilinx, Inc. and is protected under U.S. and
## international copyright and other intellectual property
## laws.
##
## DISCLAIMER
## This disclaimer is not a license and does not grant any
## rights to the materials distributed herewith. Except as
## otherwise provided in a valid license issued to you by
## Xilinx, and to the maximum extent permitted by applicable
## law: (1) THESE MATERIALS ARE MADE AVAILABLE "AS IS" AND
## WITH ALL FAULTS, AND XILINX HEREBY DISCLAIMS ALL WARRANTIES
## AND CONDITIONS, EXPRESS, IMPLIED, OR STATUTORY, INCLUDING
## BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, NON-
## INFRINGEMENT, OR FITNESS FOR ANY PARTICULAR PURPOSE; and
## (2) Xilinx shall not be liable (whether in contract or tort,
## including negligence, or under any other theory of
## liability) for any loss or damage of any kind or nature
## related to, arising under or in connection with these
## materials, including for any direct, or any indirect,
## special, incidental, or consequential loss or damage
## (including loss of data, profits, goodwill, or any type of
## loss or damage suffered as a result of any action brought
## by a third party) even if such damage or loss was
## reasonably foreseeable or Xilinx had been advised of the
## possibility of the same.
##
## CRITICAL APPLICATIONS
## Xilinx products are not designed or intended to be fail-
## safe, or for use in any application requiring fail-safe
## performance, such as life-support or safety devices or
## systems, Class III medical devices, nuclear facilities,
## applications related to the deployment of airbags, or any
## other applications that could lead to death, personal
## injury, or severe property or environmental damage
## (individually and collectively, "Critical
## Applications"). Customer assumes the sole risk and
## liability of any use of Xilinx products in Critical
## Applications, subject only to applicable laws and
## regulations governing limitations on product liability.
##
## THIS COPYRIGHT NOTICE AND DISCLAIMER MUST BE RETAINED AS
## PART OF THIS FILE AT ALL TIMES.
##
##-----------------------------------------------------------------------------
## Project    : Series-7 Integrated Block for PCI Express
## File       : xilinx_pcie_7x_ep_x4g2.xdc
## Version    : 3.1
#
###############################################################################
# User Configuration
# Link Width   - x4
# Link Speed   - gen2
# Family       - artix7
# Part         - xc7a100t
# Package      - fgg484
# Speed grade  - -2
# PCIe Block   - X0Y0
###############################################################################
#
###############################################################################
# User Time Names / User Time Groups / Time Specs
###############################################################################

###############################################################################
# User Physical Constraints
###############################################################################


###############################################################################
# Pinout and Related I/O Constraints
###############################################################################

#
# SYS reset (input) signal.  The sys_reset_n signal should be
# obtained from the PCI Express interface if possible.  For
# slot based form factors, a system reset signal is usually
# present on the connector.  For cable based form factors, a
# system reset signal may not be available.  In this case, the
# system reset signal must be generated locally by some form of
# supervisory circuit.  You may change the IOSTANDARD and LOC
# to suit your requirements and VCCO voltage banking rules.
# Some 7 series devices do not have 3.3 V I/Os available.
# Therefore the appropriate level shift is required to operate
# with these devices that contain only 1.8 V banks.
#

#pci

###############################################################################
# Timing Constraints
###############################################################################

create_clock -period 10.000 -name sys_clk [get_ports sys_clk_clk_p]
set_property CONFIG_VOLTAGE 1.8 [current_design]
set_property CFGBVS GND [current_design]
set_property PULLTYPE PULLUP [get_ports reset_n]

set_property IOSTANDARD LVCMOS33 [get_ports sys_rst_n]
set_property PULLTYPE PULLUP [get_ports sys_rst_n]
set_property PACKAGE_PIN J20 [get_ports sys_rst_n]
set_false_path -from [get_ports sys_rst_n]
###############################################################################
# Physical Constraints
###############################################################################
#
# SYS clock 100 MHz (input) signal. The sys_clk_p and sys_clk_n
# signals are the PCI Express reference clock. Virtex-7 GT
# Transceiver architecture requires the use of a dedicated clock
# resources (FPGA input pins) associated with each GT Transceiver.
# To use these pins an IBUFDS primitive (refclk_ibuf) is
# instantiated in user's design.
# Please refer to the Virtex-7 GT Transceiver User Guide
# (UG) for guidelines regarding clock resource selection.
#


#############################################################################
set_property BITSTREAM.CONFIG.SPI_BUSWIDTH 4 [current_design]
set_property CONFIG_MODE SPIx4 [current_design]
set_property BITSTREAM.CONFIG.CONFIGRATE 50 [current_design]
###############################################################################
# Timing Constraints
###############################################################################
#
##############################################################################
# Tandem Configuration Constraints
###############################################################################

set_false_path -from [get_ports reset_n]

###############################################################################
# End
###############################################################################

# PadFunction: IO_L13P_T2_MRCC_34
set_property IOSTANDARD DIFF_SSTL15 [get_ports sys_ddr_clk_p]

# PadFunction: IO_L13N_T2_MRCC_34
set_property IOSTANDARD DIFF_SSTL15 [get_ports sys_ddr_clk_n]
set_property PACKAGE_PIN R4 [get_ports sys_ddr_clk_p]
set_property PACKAGE_PIN T4 [get_ports sys_ddr_clk_n]



set_property PACKAGE_PIN P20 [get_ports uart_rtl_0_rxd]


set_property PACKAGE_PIN N15 [get_ports uart_rtl_0_txd]



set_property IOSTANDARD LVCMOS33 [get_ports uart_rtl_0_rxd]
set_property IOSTANDARD LVCMOS33 [get_ports uart_rtl_0_txd]
#set_property PACKAGE_PIN C13 [get_ports usr_irq_ack]
#set_property IOSTANDARD LVCMOS33 [get_ports usr_irq_ack]

#set_property PACKAGE_PIN D14 [get_ports {led[2]}]
#set_property IOSTANDARD LVCMOS33 [get_ports {led[2]}]






set_property CLOCK_DEDICATED_ROUTE BACKBONE [get_nets PCIe_i/clk_and_reset/clk_wiz_0/inst/clk_in1_PCIe_clk_wiz_0_0]



set_property PACKAGE_PIN T6 [get_ports reset_n]
set_property IOSTANDARD LVCMOS15 [get_ports reset_n]



set_property PACKAGE_PIN M13 [get_ports vid_aclk]
set_property PACKAGE_PIN V14 [get_ports {vid_data_0[0]}]
set_property PACKAGE_PIN H14 [get_ports {vid_data_0[1]}]
set_property PACKAGE_PIN J14 [get_ports {vid_data_0[2]}]
set_property PACKAGE_PIN K13 [get_ports {vid_data_0[3]}]
set_property PACKAGE_PIN K14 [get_ports {vid_data_0[4]}]
set_property PACKAGE_PIN L13 [get_ports {vid_data_0[5]}]
set_property PACKAGE_PIN L19 [get_ports {vid_data_0[6]}]
set_property PACKAGE_PIN L20 [get_ports {vid_data_0[7]}]
set_property PACKAGE_PIN K17 [get_ports {vid_data_0[8]}]
set_property PACKAGE_PIN J17 [get_ports {vid_data_0[9]}]
set_property PACKAGE_PIN L16 [get_ports {vid_data_0[10]}]
set_property PACKAGE_PIN K16 [get_ports {vid_data_0[11]}]
set_property PACKAGE_PIN L14 [get_ports {vid_data_0[12]}]
set_property PACKAGE_PIN L15 [get_ports {vid_data_0[13]}]
set_property PACKAGE_PIN M15 [get_ports {vid_data_0[14]}]
set_property PACKAGE_PIN M16 [get_ports {vid_data_0[15]}]
set_property PACKAGE_PIN L18 [get_ports {vid_data_0[16]}]
set_property PACKAGE_PIN M18 [get_ports {vid_data_0[17]}]
set_property PACKAGE_PIN N18 [get_ports {vid_data_0[18]}]
set_property PACKAGE_PIN N19 [get_ports {vid_data_0[19]}]
set_property PACKAGE_PIN M20 [get_ports {vid_data_0[20]}]
set_property PACKAGE_PIN N20 [get_ports {vid_data_0[21]}]
set_property PACKAGE_PIN L21 [get_ports {vid_data_0[22]}]
set_property PACKAGE_PIN M21 [get_ports {vid_data_0[23]}]
set_property PACKAGE_PIN V13 [get_ports vid_active_video_0]
set_property PACKAGE_PIN T15 [get_ports vid_hsync_0]
set_property PACKAGE_PIN J19 [get_ports {vid_aresetn[0]}]
set_property PACKAGE_PIN T14 [get_ports vid_vsync_0]
set_property PACKAGE_PIN E16 [get_ports iic_hdmi_scl_io]
set_property PACKAGE_PIN F16 [get_ports iic_hdmi_sda_io]

set_property IOSTANDARD LVCMOS33 [get_ports vid_aclk]
set_property IOSTANDARD LVCMOS33 [get_ports {vid_data_0[*]}]
set_property IOSTANDARD LVCMOS33 [get_ports vid_active_video_0]
set_property IOSTANDARD LVCMOS33 [get_ports vid_hsync_0]
set_property IOSTANDARD LVCMOS33 [get_ports {vid_aresetn[0]}]
set_property IOSTANDARD LVCMOS33 [get_ports vid_vsync_0]

set_property IOSTANDARD LVCMOS33 [get_ports iic_hdmi_scl_io]
set_property IOSTANDARD LVCMOS33 [get_ports iic_hdmi_sda_io]


set_property IOB TRUE [get_ports {vid_data_0[*]}]
set_property IOB TRUE [get_ports vid_active_video_0]
set_property IOB TRUE [get_ports vid_hsync_0]
set_property IOB TRUE [get_ports vid_vsync_0]

set_property SLEW FAST [get_ports {vid_data_0[*]}]
set_property SLEW FAST [get_ports vid_active_video_0]
set_property SLEW FAST [get_ports vid_hsync_0]
set_property SLEW FAST [get_ports vid_vsync_0]



set_property LOC IBUFDS_GTE2_X0Y3 [get_cells {PCIe_i/util_ds_buf_0/U0/USE_IBUFDS_GTE2.GEN_IBUFDS_GTE2[0].IBUFDS_GTE2_I}]
set_property C_CLK_INPUT_FREQ_HZ 300000000 [get_debug_cores dbg_hub]
set_property C_ENABLE_CLK_DIVIDER false [get_debug_cores dbg_hub]
set_property C_USER_SCAN_CHAIN 1 [get_debug_cores dbg_hub]
connect_debug_port dbg_hub/clk [get_nets clk]
