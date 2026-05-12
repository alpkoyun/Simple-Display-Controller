
################################################################
# This is a generated script based on design: PCIe
#
# Though there are limitations about the generated script,
# the main purpose of this utility is to make learning
# IP Integrator Tcl commands easier.
################################################################

namespace eval _tcl {
proc get_script_folder {} {
   set script_path [file normalize [info script]]
   set script_folder [file dirname $script_path]
   return $script_folder
}
}
variable script_folder
set script_folder [_tcl::get_script_folder]

################################################################
# Check if script is running in correct Vivado version.
################################################################
set scripts_vivado_version 2023.2
set current_vivado_version [version -short]

if { [string first $scripts_vivado_version $current_vivado_version] == -1 } {
   puts ""
   if { [string compare $scripts_vivado_version $current_vivado_version] > 0 } {
      catch {common::send_gid_msg -ssname BD::TCL -id 2042 -severity "ERROR" " This script was generated using Vivado <$scripts_vivado_version> and is being run in <$current_vivado_version> of Vivado. Sourcing the script failed since it was created with a future version of Vivado."}

   } else {
     catch {common::send_gid_msg -ssname BD::TCL -id 2041 -severity "ERROR" "This script was generated using Vivado <$scripts_vivado_version> and is being run in <$current_vivado_version> of Vivado. Please run the script in Vivado <$scripts_vivado_version> then open the design in Vivado <$current_vivado_version>. Upgrade the design by running \"Tools => Report => Report IP Status...\", then run write_bd_tcl to create an updated script."}

   }

   return 1
}

################################################################
# START
################################################################

# To test this script, run the following commands from Vivado Tcl console:
# source PCIe_script.tcl

# If there is no project opened, this script will create a
# project, but make sure you do not have an existing project
# <./myproj/project_1.xpr> in the current working folder.

set list_projs [get_projects -quiet]
if { $list_projs eq "" } {
   create_project project_1 myproj -part xc7a200tfbg484-2
}


# CHANGE DESIGN NAME HERE
variable design_name
set design_name PCIe

# If you do not already have an existing IP Integrator design open,
# you can create a design using the following command:
#    create_bd_design $design_name

# Creating design if needed
set errMsg ""
set nRet 0

set cur_design [current_bd_design -quiet]
set list_cells [get_bd_cells -quiet]

if { ${design_name} eq "" } {
   # USE CASES:
   #    1) Design_name not set

   set errMsg "Please set the variable <design_name> to a non-empty value."
   set nRet 1

} elseif { ${cur_design} ne "" && ${list_cells} eq "" } {
   # USE CASES:
   #    2): Current design opened AND is empty AND names same.
   #    3): Current design opened AND is empty AND names diff; design_name NOT in project.
   #    4): Current design opened AND is empty AND names diff; design_name exists in project.

   if { $cur_design ne $design_name } {
      common::send_gid_msg -ssname BD::TCL -id 2001 -severity "INFO" "Changing value of <design_name> from <$design_name> to <$cur_design> since current design is empty."
      set design_name [get_property NAME $cur_design]
   }
   common::send_gid_msg -ssname BD::TCL -id 2002 -severity "INFO" "Constructing design in IPI design <$cur_design>..."

} elseif { ${cur_design} ne "" && $list_cells ne "" && $cur_design eq $design_name } {
   # USE CASES:
   #    5) Current design opened AND has components AND same names.

   set errMsg "Design <$design_name> already exists in your project, please set the variable <design_name> to another value."
   set nRet 1
} elseif { [get_files -quiet ${design_name}.bd] ne "" } {
   # USE CASES: 
   #    6) Current opened design, has components, but diff names, design_name exists in project.
   #    7) No opened design, design_name exists in project.

   set errMsg "Design <$design_name> already exists in your project, please set the variable <design_name> to another value."
   set nRet 2

} else {
   # USE CASES:
   #    8) No opened design, design_name not in project.
   #    9) Current opened design, has components, but diff names, design_name not in project.

   common::send_gid_msg -ssname BD::TCL -id 2003 -severity "INFO" "Currently there is no design <$design_name> in project, so creating one..."

   create_bd_design $design_name

   common::send_gid_msg -ssname BD::TCL -id 2004 -severity "INFO" "Making design <$design_name> as current_bd_design."
   current_bd_design $design_name

}

common::send_gid_msg -ssname BD::TCL -id 2005 -severity "INFO" "Currently the variable <design_name> is equal to \"$design_name\"."

if { $nRet != 0 } {
   catch {common::send_gid_msg -ssname BD::TCL -id 2006 -severity "ERROR" $errMsg}
   return $nRet
}

set bCheckIPsPassed 1
##################################################################
# CHECK IPs
##################################################################
set bCheckIPs 1
if { $bCheckIPs == 1 } {
   set list_check_ips "\ 
xilinx.com:ip:axi_intc:4.1\
xilinx.com:ip:xlconcat:2.1\
xilinx.com:ip:mdm:3.2\
xilinx.com:ip:axi_uartlite:2.0\
xilinx.com:ip:axi_vdma:6.3\
xilinx.com:ip:axi_iic:2.1\
xilinx.com:ip:microblaze:11.0\
xilinx.com:ip:xdma:4.1\
xilinx.com:ip:xlconstant:1.1\
xilinx.com:ip:util_ds_buf:2.2\
xilinx.com:ip:system_ila:1.1\
xilinx.com:user:frame_counter:1.0\
xilinx.com:ip:axis_register_slice:1.1\
xilinx.com:hls:color_convert:1.0\
xilinx.com:hls:pixel_unpack:1.0\
xilinx.com:ip:v_axi4s_vid_out:4.0\
xilinx.com:ip:v_tc:6.2\
xilinx.com:ip:axi_gpio:2.0\
xilinx.com:ip:clk_wiz:6.0\
xilinx.com:ip:proc_sys_reset:5.0\
xilinx.com:ip:lmb_v10:3.0\
xilinx.com:ip:lmb_bram_if_cntlr:4.0\
xilinx.com:ip:blk_mem_gen:8.4\
xilinx.com:ip:mig_7series:4.2\
"

   set list_ips_missing ""
   common::send_gid_msg -ssname BD::TCL -id 2011 -severity "INFO" "Checking if the following IPs exist in the project's IP catalog: $list_check_ips ."

   foreach ip_vlnv $list_check_ips {
      set ip_obj [get_ipdefs -all $ip_vlnv]
      if { $ip_obj eq "" } {
         lappend list_ips_missing $ip_vlnv
      }
   }

   if { $list_ips_missing ne "" } {
      catch {common::send_gid_msg -ssname BD::TCL -id 2012 -severity "ERROR" "The following IPs are not found in the IP Catalog:\n  $list_ips_missing\n\nResolution: Please add the repository containing the IP(s) to the project." }
      set bCheckIPsPassed 0
   }

}

if { $bCheckIPsPassed != 1 } {
  common::send_gid_msg -ssname BD::TCL -id 2023 -severity "WARNING" "Will not continue with creation of design due to the error(s) above."
  return 3
}


##################################################################
# MIG PRJ FILE TCL PROCs
##################################################################

proc write_mig_file_PCIe_mig_7series_0_0 { str_mig_prj_filepath } {

   file mkdir [ file dirname "$str_mig_prj_filepath" ]
   set mig_prj_file [open $str_mig_prj_filepath  w+]

   puts $mig_prj_file {﻿<?xml version="1.0" encoding="UTF-8" standalone="no" ?>}
   puts $mig_prj_file {<Project NoOfControllers="1">}
   puts $mig_prj_file {  }
   puts $mig_prj_file {<!-- IMPORTANT: This is an internal file that has been generated by the MIG software. Any direct editing or changes made to this file may result in unpredictable behavior or data corruption. It is strongly advised that users do not edit the contents of this file. Re-run the MIG GUI with the required settings if any of the options provided below need to be altered. -->}
   puts $mig_prj_file {  <ModuleName>PCIe_mig_7series_0_0</ModuleName>}
   puts $mig_prj_file {  <dci_inouts_inputs>1</dci_inouts_inputs>}
   puts $mig_prj_file {  <dci_inputs>1</dci_inputs>}
   puts $mig_prj_file {  <Debug_En>OFF</Debug_En>}
   puts $mig_prj_file {  <DataDepth_En>1024</DataDepth_En>}
   puts $mig_prj_file {  <LowPower_En>ON</LowPower_En>}
   puts $mig_prj_file {  <XADC_En>Enabled</XADC_En>}
   puts $mig_prj_file {  <TargetFPGA>xc7a200t-fbg484/-2</TargetFPGA>}
   puts $mig_prj_file {  <Version>4.2</Version>}
   puts $mig_prj_file {  <SystemClock>No Buffer</SystemClock>}
   puts $mig_prj_file {  <ReferenceClock>Use System Clock</ReferenceClock>}
   puts $mig_prj_file {  <SysResetPolarity>ACTIVE LOW</SysResetPolarity>}
   puts $mig_prj_file {  <BankSelectionFlag>FALSE</BankSelectionFlag>}
   puts $mig_prj_file {  <InternalVref>0</InternalVref>}
   puts $mig_prj_file {  <dci_hr_inouts_inputs>50 Ohms</dci_hr_inouts_inputs>}
   puts $mig_prj_file {  <dci_cascade>0</dci_cascade>}
   puts $mig_prj_file {  <Controller number="0">}
   puts $mig_prj_file {    <MemoryDevice>DDR3_SDRAM/Components/MT41J256m16XX-125</MemoryDevice>}
   puts $mig_prj_file {    <TimePeriod>2500</TimePeriod>}
   puts $mig_prj_file {    <VccAuxIO>1.8V</VccAuxIO>}
   puts $mig_prj_file {    <PHYRatio>4:1</PHYRatio>}
   puts $mig_prj_file {    <InputClkFreq>200</InputClkFreq>}
   puts $mig_prj_file {    <UIExtraClocks>0</UIExtraClocks>}
   puts $mig_prj_file {    <MMCM_VCO>800</MMCM_VCO>}
   puts $mig_prj_file {    <MMCMClkOut0> 1.000</MMCMClkOut0>}
   puts $mig_prj_file {    <MMCMClkOut1>1</MMCMClkOut1>}
   puts $mig_prj_file {    <MMCMClkOut2>1</MMCMClkOut2>}
   puts $mig_prj_file {    <MMCMClkOut3>1</MMCMClkOut3>}
   puts $mig_prj_file {    <MMCMClkOut4>1</MMCMClkOut4>}
   puts $mig_prj_file {    <DataWidth>32</DataWidth>}
   puts $mig_prj_file {    <DeepMemory>1</DeepMemory>}
   puts $mig_prj_file {    <DataMask>1</DataMask>}
   puts $mig_prj_file {    <ECC>Disabled</ECC>}
   puts $mig_prj_file {    <Ordering>Normal</Ordering>}
   puts $mig_prj_file {    <BankMachineCnt>4</BankMachineCnt>}
   puts $mig_prj_file {    <CustomPart>FALSE</CustomPart>}
   puts $mig_prj_file {    <NewPartName/>}
   puts $mig_prj_file {    <RowAddress>15</RowAddress>}
   puts $mig_prj_file {    <ColAddress>10</ColAddress>}
   puts $mig_prj_file {    <BankAddress>3</BankAddress>}
   puts $mig_prj_file {    <MemoryVoltage>1.5V</MemoryVoltage>}
   puts $mig_prj_file {    <C0_MEM_SIZE>1073741824</C0_MEM_SIZE>}
   puts $mig_prj_file {    <UserMemoryAddressMap>BANK_ROW_COLUMN</UserMemoryAddressMap>}
   puts $mig_prj_file {    <PinSelection>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="AA4" SLEW="FAST" VCCAUX_IO="" name="ddr3_addr[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="Y1" SLEW="FAST" VCCAUX_IO="" name="ddr3_addr[10]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="W2" SLEW="FAST" VCCAUX_IO="" name="ddr3_addr[11]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="Y2" SLEW="FAST" VCCAUX_IO="" name="ddr3_addr[12]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="U1" SLEW="FAST" VCCAUX_IO="" name="ddr3_addr[13]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="V3" SLEW="FAST" VCCAUX_IO="" name="ddr3_addr[14]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="AB2" SLEW="FAST" VCCAUX_IO="" name="ddr3_addr[1]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="AA5" SLEW="FAST" VCCAUX_IO="" name="ddr3_addr[2]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="AB5" SLEW="FAST" VCCAUX_IO="" name="ddr3_addr[3]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="AB1" SLEW="FAST" VCCAUX_IO="" name="ddr3_addr[4]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="U3" SLEW="FAST" VCCAUX_IO="" name="ddr3_addr[5]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="W1" SLEW="FAST" VCCAUX_IO="" name="ddr3_addr[6]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="T1" SLEW="FAST" VCCAUX_IO="" name="ddr3_addr[7]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="V2" SLEW="FAST" VCCAUX_IO="" name="ddr3_addr[8]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="U2" SLEW="FAST" VCCAUX_IO="" name="ddr3_addr[9]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="AA3" SLEW="FAST" VCCAUX_IO="" name="ddr3_ba[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="Y3" SLEW="FAST" VCCAUX_IO="" name="ddr3_ba[1]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="Y4" SLEW="FAST" VCCAUX_IO="" name="ddr3_ba[2]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="W4" SLEW="FAST" VCCAUX_IO="" name="ddr3_cas_n"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="DIFF_SSTL15" PADName="R2" SLEW="FAST" VCCAUX_IO="" name="ddr3_ck_n[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="DIFF_SSTL15" PADName="R3" SLEW="FAST" VCCAUX_IO="" name="ddr3_ck_p[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="T5" SLEW="FAST" VCCAUX_IO="" name="ddr3_cke[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="AB3" SLEW="FAST" VCCAUX_IO="" name="ddr3_cs_n[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="D2" SLEW="FAST" VCCAUX_IO="" name="ddr3_dm[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="G2" SLEW="FAST" VCCAUX_IO="" name="ddr3_dm[1]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="M2" SLEW="FAST" VCCAUX_IO="" name="ddr3_dm[2]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="M5" SLEW="FAST" VCCAUX_IO="" name="ddr3_dm[3]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="C2" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="H2" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[10]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="H5" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[11]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="J1" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[12]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="J5" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[13]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="K1" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[14]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="H4" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[15]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="L4" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[16]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="M3" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[17]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="L3" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[18]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="J6" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[19]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="G1" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[1]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="K3" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[20]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="K6" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[21]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="J4" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[22]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="L5" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[23]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="P1" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[24]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="N4" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[25]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="R1" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[26]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="N2" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[27]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="M6" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[28]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="N5" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[29]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="A1" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[2]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="P6" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[30]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="P2" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[31]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="F3" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[3]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="B2" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[4]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="F1" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[5]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="B1" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[6]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="E2" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[7]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="H3" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[8]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="SSTL15" PADName="G3" SLEW="FAST" VCCAUX_IO="" name="ddr3_dq[9]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="DIFF_SSTL15" PADName="D1" SLEW="FAST" VCCAUX_IO="" name="ddr3_dqs_n[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="DIFF_SSTL15" PADName="J2" SLEW="FAST" VCCAUX_IO="" name="ddr3_dqs_n[1]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="DIFF_SSTL15" PADName="L1" SLEW="FAST" VCCAUX_IO="" name="ddr3_dqs_n[2]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="DIFF_SSTL15" PADName="P4" SLEW="FAST" VCCAUX_IO="" name="ddr3_dqs_n[3]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="DIFF_SSTL15" PADName="E1" SLEW="FAST" VCCAUX_IO="" name="ddr3_dqs_p[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="DIFF_SSTL15" PADName="K2" SLEW="FAST" VCCAUX_IO="" name="ddr3_dqs_p[1]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="DIFF_SSTL15" PADName="M1" SLEW="FAST" VCCAUX_IO="" name="ddr3_dqs_p[2]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="UNTUNED_SPLIT_50" IOSTANDARD="DIFF_SSTL15" PADName="P5" SLEW="FAST" VCCAUX_IO="" name="ddr3_dqs_p[3]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="U5" SLEW="FAST" VCCAUX_IO="" name="ddr3_odt[0]"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="V4" SLEW="FAST" VCCAUX_IO="" name="ddr3_ras_n"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="LVCMOS15" PADName="W6" SLEW="FAST" VCCAUX_IO="" name="ddr3_reset_n"/>}
   puts $mig_prj_file {      <Pin IN_TERM="" IOSTANDARD="SSTL15" PADName="AA1" SLEW="FAST" VCCAUX_IO="" name="ddr3_we_n"/>}
   puts $mig_prj_file {    </PinSelection>}
   puts $mig_prj_file {    <System_Control>}
   puts $mig_prj_file {      <Pin Bank="15" PADName="J20" name="sys_rst"/>}
   puts $mig_prj_file {      <Pin Bank="Select Bank" PADName="No connect" name="init_calib_complete"/>}
   puts $mig_prj_file {      <Pin Bank="Select Bank" PADName="No connect" name="tg_compare_error"/>}
   puts $mig_prj_file {    </System_Control>}
   puts $mig_prj_file {    <TimingParameters>}
   puts $mig_prj_file {      <Parameters tcke="5" tfaw="40" tras="35" trcd="13.75" trefi="7.8" trfc="260" trp="13.75" trrd="7.5" trtp="7.5" twtr="7.5"/>}
   puts $mig_prj_file {    </TimingParameters>}
   puts $mig_prj_file {    <mrBurstLength name="Burst Length">8 - Fixed</mrBurstLength>}
   puts $mig_prj_file {    <mrBurstType name="Read Burst Type and Length">Sequential</mrBurstType>}
   puts $mig_prj_file {    <mrCasLatency name="CAS Latency">6</mrCasLatency>}
   puts $mig_prj_file {    <mrMode name="Mode">Normal</mrMode>}
   puts $mig_prj_file {    <mrDllReset name="DLL Reset">No</mrDllReset>}
   puts $mig_prj_file {    <mrPdMode name="DLL control for precharge PD">Slow Exit</mrPdMode>}
   puts $mig_prj_file {    <emrDllEnable name="DLL Enable">Enable</emrDllEnable>}
   puts $mig_prj_file {    <emrOutputDriveStrength name="Output Driver Impedance Control">RZQ/7</emrOutputDriveStrength>}
   puts $mig_prj_file {    <emrMirrorSelection name="Address Mirroring">Disable</emrMirrorSelection>}
   puts $mig_prj_file {    <emrCSSelection name="Controller Chip Select Pin">Enable</emrCSSelection>}
   puts $mig_prj_file {    <emrRTT name="RTT (nominal) - On Die Termination (ODT)">RZQ/4</emrRTT>}
   puts $mig_prj_file {    <emrPosted name="Additive Latency (AL)">0</emrPosted>}
   puts $mig_prj_file {    <emrOCD name="Write Leveling Enable">Disabled</emrOCD>}
   puts $mig_prj_file {    <emrDQS name="TDQS enable">Enabled</emrDQS>}
   puts $mig_prj_file {    <emrRDQS name="Qoff">Output Buffer Enabled</emrRDQS>}
   puts $mig_prj_file {    <mr2PartialArraySelfRefresh name="Partial-Array Self Refresh">Full Array</mr2PartialArraySelfRefresh>}
   puts $mig_prj_file {    <mr2CasWriteLatency name="CAS write latency">5</mr2CasWriteLatency>}
   puts $mig_prj_file {    <mr2AutoSelfRefresh name="Auto Self Refresh">Enabled</mr2AutoSelfRefresh>}
   puts $mig_prj_file {    <mr2SelfRefreshTempRange name="High Temparature Self Refresh Rate">Normal</mr2SelfRefreshTempRange>}
   puts $mig_prj_file {    <mr2RTTWR name="RTT_WR - Dynamic On Die Termination (ODT)">Dynamic ODT off</mr2RTTWR>}
   puts $mig_prj_file {    <PortInterface>AXI</PortInterface>}
   puts $mig_prj_file {    <AXIParameters>}
   puts $mig_prj_file {      <C0_C_RD_WR_ARB_ALGORITHM>RD_PRI_REG</C0_C_RD_WR_ARB_ALGORITHM>}
   puts $mig_prj_file {      <C0_S_AXI_ADDR_WIDTH>30</C0_S_AXI_ADDR_WIDTH>}
   puts $mig_prj_file {      <C0_S_AXI_DATA_WIDTH>256</C0_S_AXI_DATA_WIDTH>}
   puts $mig_prj_file {      <C0_S_AXI_ID_WIDTH>2</C0_S_AXI_ID_WIDTH>}
   puts $mig_prj_file {      <C0_S_AXI_SUPPORTS_NARROW_BURST>0</C0_S_AXI_SUPPORTS_NARROW_BURST>}
   puts $mig_prj_file {    </AXIParameters>}
   puts $mig_prj_file {  </Controller>}
   puts $mig_prj_file {</Project>}

   close $mig_prj_file
}
# End of write_mig_file_PCIe_mig_7series_0_0()



##################################################################
# DESIGN PROCs
##################################################################


# Hierarchical cell: clk_and_reset
proc create_hier_cell_clk_and_reset { parentCell nameHier } {

  variable script_folder

  if { $parentCell eq "" || $nameHier eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2092 -severity "ERROR" "create_hier_cell_clk_and_reset() - Empty argument(s)!"}
     return
  }

  # Get object for parentCell
  set parentObj [get_bd_cells $parentCell]
  if { $parentObj == "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2090 -severity "ERROR" "Unable to find parent cell <$parentCell>!"}
     return
  }

  # Make sure parentObj is hier blk
  set parentType [get_property TYPE $parentObj]
  if { $parentType ne "hier" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2091 -severity "ERROR" "Parent <$parentObj> has TYPE = <$parentType>. Expected to be <hier>."}
     return
  }

  # Save current instance; Restore later
  set oldCurInst [current_bd_instance .]

  # Set parent object as current
  current_bd_instance $parentObj

  # Create cell and set as current instance
  set hier_obj [create_bd_cell -type hier $nameHier]
  current_bd_instance $hier_obj

  # Create interface pins
  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:diff_clock_rtl:1.0 sys_ddr


  # Create pins
  create_bd_pin -dir O -type clk clk_200mhz
  create_bd_pin -dir I -type rst ext_reset_in
  create_bd_pin -dir I dcm_locked
  create_bd_pin -dir O -from 0 -to 0 -type rst arstn_200mhz
  create_bd_pin -dir I reset_n
  create_bd_pin -dir I -type rst mb_debug_sys_rst
  create_bd_pin -dir O locked
  create_bd_pin -dir O -from 0 -to 0 -type rst peripheral_reset
  create_bd_pin -dir O -type rst mb_reset1

  # Create instance: reset_200mhz, and set properties
  set reset_200mhz [ create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 reset_200mhz ]

  # Create instance: clk_wiz_0, and set properties
  set clk_wiz_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:clk_wiz:6.0 clk_wiz_0 ]
  set_property -dict [list \
    CONFIG.CLKIN1_JITTER_PS {50.0} \
    CONFIG.CLKOUT1_JITTER {98.146} \
    CONFIG.CLKOUT1_PHASE_ERROR {89.971} \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {200.000} \
    CONFIG.CLKOUT2_JITTER {112.316} \
    CONFIG.CLKOUT2_PHASE_ERROR {89.971} \
    CONFIG.CLKOUT2_USED {false} \
    CONFIG.CLKOUT3_JITTER {102.207} \
    CONFIG.CLKOUT3_PHASE_ERROR {91.235} \
    CONFIG.CLKOUT3_REQUESTED_OUT_FREQ {148.5} \
    CONFIG.CLKOUT3_USED {false} \
    CONFIG.MMCM_CLKFBOUT_MULT_F {5.000} \
    CONFIG.MMCM_CLKIN1_PERIOD {5.000} \
    CONFIG.MMCM_CLKIN2_PERIOD {10.0} \
    CONFIG.MMCM_CLKOUT0_DIVIDE_F {5.000} \
    CONFIG.MMCM_CLKOUT1_DIVIDE {1} \
    CONFIG.MMCM_CLKOUT2_DIVIDE {1} \
    CONFIG.MMCM_DIVCLK_DIVIDE {1} \
    CONFIG.NUM_OUT_CLKS {1} \
    CONFIG.PRIM_IN_FREQ {200.000} \
    CONFIG.PRIM_SOURCE {Differential_clock_capable_pin} \
    CONFIG.RESET_PORT {resetn} \
    CONFIG.RESET_TYPE {ACTIVE_LOW} \
  ] $clk_wiz_0


  # Create interface connections
  connect_bd_intf_net -intf_net CLK_IN1_D_1 [get_bd_intf_pins sys_ddr] [get_bd_intf_pins clk_wiz_0/CLK_IN1_D]

  # Create port connections
  connect_bd_net -net M00_ARESETN_1 [get_bd_pins reset_200mhz/peripheral_aresetn] [get_bd_pins arstn_200mhz]
  connect_bd_net -net clk_wiz_0_clk_out1 [get_bd_pins clk_wiz_0/clk_out1] [get_bd_pins clk_200mhz] [get_bd_pins reset_200mhz/slowest_sync_clk]
  connect_bd_net -net clk_wiz_0_locked [get_bd_pins clk_wiz_0/locked] [get_bd_pins locked]
  connect_bd_net -net mig_7series_0_mmcm_locked [get_bd_pins dcm_locked] [get_bd_pins reset_200mhz/dcm_locked]
  connect_bd_net -net mig_7series_0_ui_clk_sync_rst [get_bd_pins ext_reset_in] [get_bd_pins reset_200mhz/ext_reset_in]
  connect_bd_net -net reset_200mhz_mb_reset [get_bd_pins reset_200mhz/mb_reset] [get_bd_pins mb_reset1]
  connect_bd_net -net reset_200mhz_peripheral_reset [get_bd_pins reset_200mhz/peripheral_reset] [get_bd_pins peripheral_reset]
  connect_bd_net -net reset_n_1 [get_bd_pins reset_n] [get_bd_pins clk_wiz_0/resetn]

  # Restore current instance
  current_bd_instance $oldCurInst
}

# Hierarchical cell: DDR_HIER
proc create_hier_cell_DDR_HIER { parentCell nameHier } {

  variable script_folder

  if { $parentCell eq "" || $nameHier eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2092 -severity "ERROR" "create_hier_cell_DDR_HIER() - Empty argument(s)!"}
     return
  }

  # Get object for parentCell
  set parentObj [get_bd_cells $parentCell]
  if { $parentObj == "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2090 -severity "ERROR" "Unable to find parent cell <$parentCell>!"}
     return
  }

  # Make sure parentObj is hier blk
  set parentType [get_property TYPE $parentObj]
  if { $parentType ne "hier" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2091 -severity "ERROR" "Parent <$parentObj> has TYPE = <$parentType>. Expected to be <hier>."}
     return
  }

  # Save current instance; Restore later
  set oldCurInst [current_bd_instance .]

  # Set parent object as current
  current_bd_instance $parentObj

  # Create cell and set as current instance
  set hier_obj [create_bd_cell -type hier $nameHier]
  current_bd_instance $hier_obj

  # Create interface pins
  create_bd_intf_pin -mode Master -vlnv xilinx.com:interface:ddrx_rtl:1.0 DDR3

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:aximm_rtl:1.0 S_AXI


  # Create pins
  create_bd_pin -dir O -type clk ui_clk
  create_bd_pin -dir O -type rst ui_clk_sync_rst
  create_bd_pin -dir O mmcm_locked
  create_bd_pin -dir O -from 0 -to 0 -type rst aresetn
  create_bd_pin -dir I -type rst sys_rst
  create_bd_pin -dir I -type clk sys_clk_i

  # Create instance: ddr_ui_reset, and set properties
  set ddr_ui_reset [ create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 ddr_ui_reset ]

  # Create instance: mig_7series_0, and set properties
  set mig_7series_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:mig_7series:4.2 mig_7series_0 ]

  # Generate the PRJ File for MIG
  set str_mig_folder [get_property IP_DIR [ get_ips [ get_property CONFIG.Component_Name $mig_7series_0 ] ] ]
  set str_mig_file_name mig_b.prj
  set str_mig_file_path ${str_mig_folder}/${str_mig_file_name}
  write_mig_file_PCIe_mig_7series_0_0 $str_mig_file_path

  set_property -dict [list \
    CONFIG.BOARD_MIG_PARAM {Custom} \
    CONFIG.RESET_BOARD_INTERFACE {Custom} \
    CONFIG.XML_INPUT_FILE {mig_b.prj} \
  ] $mig_7series_0


  # Create interface connections
  connect_bd_intf_net -intf_net axi_interconnect_0_M00_AXI [get_bd_intf_pins S_AXI] [get_bd_intf_pins mig_7series_0/S_AXI]
  connect_bd_intf_net -intf_net mig_7series_0_DDR3 [get_bd_intf_pins DDR3] [get_bd_intf_pins mig_7series_0/DDR3]

  # Create port connections
  connect_bd_net -net M00_ACLK_1 [get_bd_pins mig_7series_0/ui_clk] [get_bd_pins ui_clk] [get_bd_pins ddr_ui_reset/slowest_sync_clk]
  connect_bd_net -net M00_ARESETN_2 [get_bd_pins ddr_ui_reset/peripheral_aresetn] [get_bd_pins aresetn] [get_bd_pins mig_7series_0/aresetn]
  connect_bd_net -net clk_wiz_0_clk_out1 [get_bd_pins sys_clk_i] [get_bd_pins mig_7series_0/sys_clk_i]
  connect_bd_net -net clk_wiz_0_locked [get_bd_pins sys_rst] [get_bd_pins mig_7series_0/sys_rst]
  connect_bd_net -net mig_7series_0_mmcm_locked [get_bd_pins mig_7series_0/mmcm_locked] [get_bd_pins mmcm_locked] [get_bd_pins ddr_ui_reset/dcm_locked]
  connect_bd_net -net mig_7series_0_ui_clk_sync_rst [get_bd_pins mig_7series_0/ui_clk_sync_rst] [get_bd_pins ui_clk_sync_rst] [get_bd_pins ddr_ui_reset/ext_reset_in]

  # Restore current instance
  current_bd_instance $oldCurInst
}

# Hierarchical cell: microblaze_0_local_memory
proc create_hier_cell_microblaze_0_local_memory { parentCell nameHier } {

  variable script_folder

  if { $parentCell eq "" || $nameHier eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2092 -severity "ERROR" "create_hier_cell_microblaze_0_local_memory() - Empty argument(s)!"}
     return
  }

  # Get object for parentCell
  set parentObj [get_bd_cells $parentCell]
  if { $parentObj == "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2090 -severity "ERROR" "Unable to find parent cell <$parentCell>!"}
     return
  }

  # Make sure parentObj is hier blk
  set parentType [get_property TYPE $parentObj]
  if { $parentType ne "hier" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2091 -severity "ERROR" "Parent <$parentObj> has TYPE = <$parentType>. Expected to be <hier>."}
     return
  }

  # Save current instance; Restore later
  set oldCurInst [current_bd_instance .]

  # Set parent object as current
  current_bd_instance $parentObj

  # Create cell and set as current instance
  set hier_obj [create_bd_cell -type hier $nameHier]
  current_bd_instance $hier_obj

  # Create interface pins
  create_bd_intf_pin -mode MirroredMaster -vlnv xilinx.com:interface:lmb_rtl:1.0 DLMB

  create_bd_intf_pin -mode MirroredMaster -vlnv xilinx.com:interface:lmb_rtl:1.0 ILMB


  # Create pins
  create_bd_pin -dir I -type clk LMB_Clk
  create_bd_pin -dir I -type rst SYS_Rst

  # Create instance: dlmb_v10, and set properties
  set dlmb_v10 [ create_bd_cell -type ip -vlnv xilinx.com:ip:lmb_v10:3.0 dlmb_v10 ]

  # Create instance: ilmb_v10, and set properties
  set ilmb_v10 [ create_bd_cell -type ip -vlnv xilinx.com:ip:lmb_v10:3.0 ilmb_v10 ]

  # Create instance: dlmb_bram_if_cntlr, and set properties
  set dlmb_bram_if_cntlr [ create_bd_cell -type ip -vlnv xilinx.com:ip:lmb_bram_if_cntlr:4.0 dlmb_bram_if_cntlr ]
  set_property CONFIG.C_ECC {0} $dlmb_bram_if_cntlr


  # Create instance: ilmb_bram_if_cntlr, and set properties
  set ilmb_bram_if_cntlr [ create_bd_cell -type ip -vlnv xilinx.com:ip:lmb_bram_if_cntlr:4.0 ilmb_bram_if_cntlr ]
  set_property CONFIG.C_ECC {0} $ilmb_bram_if_cntlr


  # Create instance: lmb_bram, and set properties
  set lmb_bram [ create_bd_cell -type ip -vlnv xilinx.com:ip:blk_mem_gen:8.4 lmb_bram ]
  set_property -dict [list \
    CONFIG.Memory_Type {True_Dual_Port_RAM} \
    CONFIG.use_bram_block {BRAM_Controller} \
  ] $lmb_bram


  # Create interface connections
  connect_bd_intf_net -intf_net microblaze_0_dlmb [get_bd_intf_pins dlmb_v10/LMB_M] [get_bd_intf_pins DLMB]
  connect_bd_intf_net -intf_net microblaze_0_dlmb_bus [get_bd_intf_pins dlmb_v10/LMB_Sl_0] [get_bd_intf_pins dlmb_bram_if_cntlr/SLMB]
  connect_bd_intf_net -intf_net microblaze_0_dlmb_cntlr [get_bd_intf_pins dlmb_bram_if_cntlr/BRAM_PORT] [get_bd_intf_pins lmb_bram/BRAM_PORTA]
  connect_bd_intf_net -intf_net microblaze_0_ilmb [get_bd_intf_pins ilmb_v10/LMB_M] [get_bd_intf_pins ILMB]
  connect_bd_intf_net -intf_net microblaze_0_ilmb_bus [get_bd_intf_pins ilmb_v10/LMB_Sl_0] [get_bd_intf_pins ilmb_bram_if_cntlr/SLMB]
  connect_bd_intf_net -intf_net microblaze_0_ilmb_cntlr [get_bd_intf_pins ilmb_bram_if_cntlr/BRAM_PORT] [get_bd_intf_pins lmb_bram/BRAM_PORTB]

  # Create port connections
  connect_bd_net -net SYS_Rst_1 [get_bd_pins SYS_Rst] [get_bd_pins dlmb_v10/SYS_Rst] [get_bd_pins dlmb_bram_if_cntlr/LMB_Rst] [get_bd_pins ilmb_v10/SYS_Rst] [get_bd_pins ilmb_bram_if_cntlr/LMB_Rst]
  connect_bd_net -net microblaze_0_Clk [get_bd_pins LMB_Clk] [get_bd_pins dlmb_v10/LMB_Clk] [get_bd_pins dlmb_bram_if_cntlr/LMB_Clk] [get_bd_pins ilmb_v10/LMB_Clk] [get_bd_pins ilmb_bram_if_cntlr/LMB_Clk]

  # Restore current instance
  current_bd_instance $oldCurInst
}

# Hierarchical cell: hdmi_out
proc create_hier_cell_hdmi_out { parentCell nameHier } {

  variable script_folder

  if { $parentCell eq "" || $nameHier eq "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2092 -severity "ERROR" "create_hier_cell_hdmi_out() - Empty argument(s)!"}
     return
  }

  # Get object for parentCell
  set parentObj [get_bd_cells $parentCell]
  if { $parentObj == "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2090 -severity "ERROR" "Unable to find parent cell <$parentCell>!"}
     return
  }

  # Make sure parentObj is hier blk
  set parentType [get_property TYPE $parentObj]
  if { $parentType ne "hier" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2091 -severity "ERROR" "Parent <$parentObj> has TYPE = <$parentType>. Expected to be <hier>."}
     return
  }

  # Save current instance; Restore later
  set oldCurInst [current_bd_instance .]

  # Set parent object as current
  current_bd_instance $parentObj

  # Create cell and set as current instance
  set hier_obj [create_bd_cell -type hier $nameHier]
  current_bd_instance $hier_obj

  # Create interface pins
  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:aximm_rtl:1.0 S01_AXILite

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:aximm_rtl:1.0 S03_AXILite

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:axis_rtl:1.0 in_stream

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:aximm_rtl:1.0 ctrl

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:aximm_rtl:1.0 S_AXI

  create_bd_intf_pin -mode Slave -vlnv xilinx.com:interface:aximm_rtl:1.0 s_axi_lite

  create_bd_intf_pin -mode Monitor -vlnv xilinx.com:interface:axis_rtl:1.0 SLOT_1_AXIS


  # Create pins
  create_bd_pin -dir I -type clk s_axi_aclk
  create_bd_pin -dir I -type rst s_axi_aresetn
  create_bd_pin -dir I -type clk clk_in1
  create_bd_pin -dir I -type rst s_axi_aresetn1
  create_bd_pin -dir O -type clk clk_out1
  create_bd_pin -dir I -type rst ext_reset_in
  create_bd_pin -dir O vid_active_video_0
  create_bd_pin -dir O -from 23 -to 0 vid_data_0
  create_bd_pin -dir O vid_hsync_0
  create_bd_pin -dir O vid_vsync_0
  create_bd_pin -dir I -from 5 -to 0 probe4
  create_bd_pin -dir I -from 5 -to 0 probe5

  # Create instance: axis_register_slice_0, and set properties
  set axis_register_slice_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axis_register_slice:1.1 axis_register_slice_0 ]

  # Create instance: color_convert, and set properties
  set color_convert [ create_bd_cell -type ip -vlnv xilinx.com:hls:color_convert:1.0 color_convert ]

  # Create instance: pixel_unpack, and set properties
  set pixel_unpack [ create_bd_cell -type ip -vlnv xilinx.com:hls:pixel_unpack:1.0 pixel_unpack ]

  # Create instance: v_axi4s_vid_out_0, and set properties
  set v_axi4s_vid_out_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:v_axi4s_vid_out:4.0 v_axi4s_vid_out_0 ]
  set_property -dict [list \
    CONFIG.C_ADDR_WIDTH {12} \
    CONFIG.C_HAS_ASYNC_CLK {1} \
    CONFIG.C_HYSTERESIS_LEVEL {2048} \
    CONFIG.C_PIXELS_PER_CLOCK {1} \
    CONFIG.C_VTG_MASTER_SLAVE {1} \
  ] $v_axi4s_vid_out_0


  # Create instance: v_tc_0, and set properties
  set v_tc_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:v_tc:6.2 v_tc_0 ]
  set_property -dict [list \
    CONFIG.SYNC_EN {false} \
    CONFIG.VIDEO_MODE {720p} \
    CONFIG.enable_detection {false} \
    CONFIG.max_clocks_per_line {8192} \
  ] $v_tc_0


  # Create instance: video_lock_monitor, and set properties
  set video_lock_monitor [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio:2.0 video_lock_monitor ]
  set_property -dict [list \
    CONFIG.C_ALL_INPUTS {1} \
    CONFIG.C_ALL_INPUTS_2 {1} \
    CONFIG.C_GPIO_WIDTH {4} \
    CONFIG.C_IS_DUAL {1} \
  ] $video_lock_monitor


  # Create instance: video_clk_wiz, and set properties
  set video_clk_wiz [ create_bd_cell -type ip -vlnv xilinx.com:ip:clk_wiz:6.0 video_clk_wiz ]
  set_property -dict [list \
    CONFIG.CLKOUT1_JITTER {242.214} \
    CONFIG.CLKOUT1_PHASE_ERROR {245.344} \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {74.25} \
    CONFIG.MMCM_CLKFBOUT_MULT_F {37.125} \
    CONFIG.MMCM_CLKOUT0_DIVIDE_F {12.500} \
    CONFIG.MMCM_DIVCLK_DIVIDE {8} \
    CONFIG.PRIM_SOURCE {No_buffer} \
    CONFIG.USE_DYN_RECONFIG {true} \
  ] $video_clk_wiz


  # Create instance: video_out_ila, and set properties
  set video_out_ila [ create_bd_cell -type ip -vlnv xilinx.com:ip:system_ila:1.1 video_out_ila ]
  set_property -dict [list \
    CONFIG.C_MON_TYPE {NATIVE} \
    CONFIG.C_NUM_MONITOR_SLOTS {1} \
    CONFIG.C_NUM_OF_PROBES {6} \
    CONFIG.C_SLOT {0} \
    CONFIG.C_SLOT_0_INTF_TYPE {xilinx.com:interface:axis_rtl:1.0} \
    CONFIG.C_SLOT_1_INTF_TYPE {xilinx.com:interface:axis_rtl:1.0} \
    CONFIG.C_SLOT_2_INTF_TYPE {xilinx.com:interface:video_timing_rtl:2.0} \
    CONFIG.C_SLOT_3_INTF_TYPE {xilinx.com:interface:vid_io_rtl:1.0} \
  ] $video_out_ila


  # Create instance: vid_clk_reset, and set properties
  set vid_clk_reset [ create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 vid_clk_reset ]

  # Create instance: xlconcat_0, and set properties
  set xlconcat_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconcat:2.1 xlconcat_0 ]
  set_property CONFIG.NUM_PORTS {4} $xlconcat_0


  # Create instance: video_stream_ila, and set properties
  set video_stream_ila [ create_bd_cell -type ip -vlnv xilinx.com:ip:system_ila:1.1 video_stream_ila ]
  set_property -dict [list \
    CONFIG.C_MON_TYPE {MIX} \
    CONFIG.C_NUM_MONITOR_SLOTS {1} \
    CONFIG.C_NUM_OF_PROBES {2} \
    CONFIG.C_SLOT {0} \
    CONFIG.C_SLOT_0_INTF_TYPE {xilinx.com:interface:axis_rtl:1.0} \
    CONFIG.C_SLOT_1_INTF_TYPE {xilinx.com:interface:axis_rtl:1.0} \
  ] $video_stream_ila


  # Create instance: frame_counter_0, and set properties
  set frame_counter_0 [ create_bd_cell -type ip -vlnv xilinx.com:user:frame_counter:1.0 frame_counter_0 ]

  # Create interface connections
  connect_bd_intf_net -intf_net Conn1 [get_bd_intf_pins video_lock_monitor/S_AXI] [get_bd_intf_pins S_AXI]
  connect_bd_intf_net -intf_net Conn2 [get_bd_intf_pins v_tc_0/ctrl] [get_bd_intf_pins ctrl]
  connect_bd_intf_net -intf_net Conn3 [get_bd_intf_pins video_clk_wiz/s_axi_lite] [get_bd_intf_pins s_axi_lite]
  connect_bd_intf_net -intf_net Conn7 [get_bd_intf_pins S03_AXILite] [get_bd_intf_pins color_convert/s_axi_control]
  connect_bd_intf_net -intf_net Conn8 [get_bd_intf_pins S01_AXILite] [get_bd_intf_pins pixel_unpack/s_axi_control]
  connect_bd_intf_net -intf_net axis_register_slice_0_M_AXIS [get_bd_intf_pins axis_register_slice_0/M_AXIS] [get_bd_intf_pins color_convert/stream_in_24]
  connect_bd_intf_net -intf_net color_convert_stream_out_24 [get_bd_intf_pins v_axi4s_vid_out_0/video_in] [get_bd_intf_pins color_convert/stream_out_24]
  connect_bd_intf_net -intf_net [get_bd_intf_nets color_convert_stream_out_24] [get_bd_intf_pins v_axi4s_vid_out_0/video_in] [get_bd_intf_pins video_stream_ila/SLOT_0_AXIS]
  connect_bd_intf_net -intf_net [get_bd_intf_nets color_convert_stream_out_24] [get_bd_intf_pins v_axi4s_vid_out_0/video_in] [get_bd_intf_pins frame_counter_0/v_axis]
  connect_bd_intf_net -intf_net in_stream_1 [get_bd_intf_pins in_stream] [get_bd_intf_pins pixel_unpack/stream_in_32]
  connect_bd_intf_net -intf_net pixel_unpack_stream_out_24 [get_bd_intf_pins axis_register_slice_0/S_AXIS] [get_bd_intf_pins pixel_unpack/stream_out_24]
  connect_bd_intf_net -intf_net v_tc_0_vtiming_out [get_bd_intf_pins v_axi4s_vid_out_0/vtiming_in] [get_bd_intf_pins v_tc_0/vtiming_out]

  # Create port connections
  connect_bd_net -net clk_1 [get_bd_pins video_clk_wiz/clk_out1] [get_bd_pins v_tc_0/clk] [get_bd_pins v_axi4s_vid_out_0/vid_io_out_clk] [get_bd_pins clk_out1] [get_bd_pins vid_clk_reset/slowest_sync_clk] [get_bd_pins video_out_ila/clk]
  connect_bd_net -net clk_200mhz [get_bd_pins s_axi_aclk] [get_bd_pins v_tc_0/s_axi_aclk] [get_bd_pins v_axi4s_vid_out_0/aclk] [get_bd_pins axis_register_slice_0/aclk] [get_bd_pins color_convert/ap_clk] [get_bd_pins pixel_unpack/ap_clk] [get_bd_pins video_lock_monitor/s_axi_aclk] [get_bd_pins video_stream_ila/clk] [get_bd_pins frame_counter_0/clk]
  connect_bd_net -net clk_in1_1 [get_bd_pins clk_in1] [get_bd_pins video_clk_wiz/clk_in1] [get_bd_pins video_clk_wiz/s_axi_aclk]
  connect_bd_net -net ext_reset_in_1 [get_bd_pins ext_reset_in] [get_bd_pins vid_clk_reset/ext_reset_in]
  connect_bd_net -net frame_counter_0_count_frame [get_bd_pins frame_counter_0/count_frame] [get_bd_pins video_stream_ila/probe0]
  connect_bd_net -net frame_counter_0_count_frame_old [get_bd_pins frame_counter_0/count_frame_old] [get_bd_pins video_stream_ila/probe1]
  connect_bd_net -net probe4_1 [get_bd_pins probe4] [get_bd_pins video_out_ila/probe4]
  connect_bd_net -net probe5_1 [get_bd_pins probe5] [get_bd_pins video_out_ila/probe5]
  connect_bd_net -net s_axi_aresetn1_1 [get_bd_pins s_axi_aresetn1] [get_bd_pins video_clk_wiz/s_axi_aresetn]
  connect_bd_net -net s_axi_aresetn_1 [get_bd_pins s_axi_aresetn] [get_bd_pins v_tc_0/s_axi_aresetn] [get_bd_pins v_axi4s_vid_out_0/aresetn] [get_bd_pins axis_register_slice_0/aresetn] [get_bd_pins color_convert/ap_rst_n] [get_bd_pins pixel_unpack/ap_rst_n] [get_bd_pins video_lock_monitor/s_axi_aresetn] [get_bd_pins video_stream_ila/resetn] [get_bd_pins frame_counter_0/resetn]
  connect_bd_net -net v_axi4s_vid_out_0_locked [get_bd_pins v_axi4s_vid_out_0/locked] [get_bd_pins xlconcat_0/In0]
  connect_bd_net -net v_axi4s_vid_out_0_overflow [get_bd_pins v_axi4s_vid_out_0/overflow] [get_bd_pins xlconcat_0/In1]
  connect_bd_net -net v_axi4s_vid_out_0_sof_state_out [get_bd_pins v_axi4s_vid_out_0/sof_state_out] [get_bd_pins xlconcat_0/In3] [get_bd_pins v_tc_0/sof_state]
  connect_bd_net -net v_axi4s_vid_out_0_status [get_bd_pins v_axi4s_vid_out_0/status] [get_bd_pins video_lock_monitor/gpio2_io_i]
  connect_bd_net -net v_axi4s_vid_out_0_underflow [get_bd_pins v_axi4s_vid_out_0/underflow] [get_bd_pins xlconcat_0/In2]
  connect_bd_net -net v_axi4s_vid_out_0_vid_active_video [get_bd_pins v_axi4s_vid_out_0/vid_active_video] [get_bd_pins vid_active_video_0] [get_bd_pins video_out_ila/probe3]
  connect_bd_net -net v_axi4s_vid_out_0_vid_data [get_bd_pins v_axi4s_vid_out_0/vid_data] [get_bd_pins vid_data_0] [get_bd_pins video_out_ila/probe0]
  connect_bd_net -net v_axi4s_vid_out_0_vid_hsync [get_bd_pins v_axi4s_vid_out_0/vid_hsync] [get_bd_pins vid_hsync_0] [get_bd_pins video_out_ila/probe1]
  connect_bd_net -net v_axi4s_vid_out_0_vid_vsync [get_bd_pins v_axi4s_vid_out_0/vid_vsync] [get_bd_pins vid_vsync_0] [get_bd_pins video_out_ila/probe2]
  connect_bd_net -net v_axi4s_vid_out_0_vtg_ce [get_bd_pins v_axi4s_vid_out_0/vtg_ce] [get_bd_pins v_tc_0/gen_clken]
  connect_bd_net -net vid_clk_reset_peripheral_aresetn [get_bd_pins vid_clk_reset/peripheral_aresetn] [get_bd_pins v_tc_0/resetn]
  connect_bd_net -net vid_clk_reset_peripheral_reset [get_bd_pins vid_clk_reset/peripheral_reset] [get_bd_pins v_axi4s_vid_out_0/vid_io_out_reset]
  connect_bd_net -net video_clk_wiz_locked [get_bd_pins video_clk_wiz/locked] [get_bd_pins vid_clk_reset/dcm_locked]
  connect_bd_net -net xlconcat_0_dout [get_bd_pins xlconcat_0/dout] [get_bd_pins video_lock_monitor/gpio_io_i]

  # Restore current instance
  current_bd_instance $oldCurInst
}


# Procedure to create entire design; Provide argument to make
# procedure reusable. If parentCell is "", will use root.
proc create_root_design { parentCell } {

  variable script_folder
  variable design_name

  if { $parentCell eq "" } {
     set parentCell [get_bd_cells /]
  }

  # Get object for parentCell
  set parentObj [get_bd_cells $parentCell]
  if { $parentObj == "" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2090 -severity "ERROR" "Unable to find parent cell <$parentCell>!"}
     return
  }

  # Make sure parentObj is hier blk
  set parentType [get_property TYPE $parentObj]
  if { $parentType ne "hier" } {
     catch {common::send_gid_msg -ssname BD::TCL -id 2091 -severity "ERROR" "Parent <$parentObj> has TYPE = <$parentType>. Expected to be <hier>."}
     return
  }

  # Save current instance; Restore later
  set oldCurInst [current_bd_instance .]

  # Set parent object as current
  current_bd_instance $parentObj


  # Create interface ports
  set sys_ddr [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:diff_clock_rtl:1.0 sys_ddr ]
  set_property -dict [ list \
   CONFIG.FREQ_HZ {200000000} \
   ] $sys_ddr

  set DDR3 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:ddrx_rtl:1.0 DDR3 ]

  set uart_rtl_0 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:uart_rtl:1.0 uart_rtl_0 ]

  set iic_hdmi [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 iic_hdmi ]

  set pcie_mgt_0 [ create_bd_intf_port -mode Master -vlnv xilinx.com:interface:pcie_7x_mgt_rtl:1.0 pcie_mgt_0 ]

  set sys_clk [ create_bd_intf_port -mode Slave -vlnv xilinx.com:interface:diff_clock_rtl:1.0 sys_clk ]


  # Create ports
  set reset_n [ create_bd_port -dir I reset_n ]
  set vid_data_0 [ create_bd_port -dir O -from 23 -to 0 vid_data_0 ]
  set vid_active_video_0 [ create_bd_port -dir O vid_active_video_0 ]
  set vid_hsync_0 [ create_bd_port -dir O vid_hsync_0 ]
  set vid_vsync_0 [ create_bd_port -dir O vid_vsync_0 ]
  set vid_aclk [ create_bd_port -dir O vid_aclk ]
  set vid_aresetn [ create_bd_port -dir O -from 0 -to 0 vid_aresetn ]
  set sys_rst_n [ create_bd_port -dir I -type rst sys_rst_n ]

  # Create instance: microblaze_0_axi_periph, and set properties
  set microblaze_0_axi_periph [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 microblaze_0_axi_periph ]
  set_property CONFIG.NUM_MI {10} $microblaze_0_axi_periph


  # Create instance: microblaze_0_axi_intc, and set properties
  set microblaze_0_axi_intc [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_intc:4.1 microblaze_0_axi_intc ]
  set_property CONFIG.C_HAS_FAST {1} $microblaze_0_axi_intc


  # Create instance: microblaze_0_xlconcat, and set properties
  set microblaze_0_xlconcat [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconcat:2.1 microblaze_0_xlconcat ]

  # Create instance: mdm_1, and set properties
  set mdm_1 [ create_bd_cell -type ip -vlnv xilinx.com:ip:mdm:3.2 mdm_1 ]

  # Create instance: axi_uartlite_0, and set properties
  set axi_uartlite_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_uartlite:2.0 axi_uartlite_0 ]

  # Create instance: axi_vdma_0, and set properties
  set axi_vdma_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_vdma:6.3 axi_vdma_0 ]
  set_property -dict [list \
    CONFIG.c_include_mm2s_dre {0} \
    CONFIG.c_include_s2mm {1} \
    CONFIG.c_m_axi_mm2s_data_width {32} \
    CONFIG.c_m_axis_mm2s_tdata_width {32} \
    CONFIG.c_mm2s_genlock_mode {3} \
    CONFIG.c_mm2s_max_burst_length {32} \
    CONFIG.c_num_fstores {4} \
    CONFIG.c_s2mm_genlock_mode {2} \
    CONFIG.c_use_mm2s_fsync {0} \
    CONFIG.c_use_s2mm_fsync {0} \
  ] $axi_vdma_0


  # Create instance: axi_interconnect_0, and set properties
  set axi_interconnect_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 axi_interconnect_0 ]
  set_property -dict [list \
    CONFIG.NUM_MI {1} \
    CONFIG.NUM_SI {3} \
  ] $axi_interconnect_0


  # Create instance: axi_iic_0, and set properties
  set axi_iic_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:axi_iic:2.1 axi_iic_0 ]

  # Create instance: hdmi_out
  create_hier_cell_hdmi_out [current_bd_instance .] hdmi_out

  # Create instance: microblaze_0, and set properties
  set microblaze_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:microblaze:11.0 microblaze_0 ]
  set_property -dict [list \
    CONFIG.C_AREA_OPTIMIZED {2} \
    CONFIG.C_DEBUG_ENABLED {1} \
    CONFIG.C_D_AXI {1} \
    CONFIG.C_D_LMB {1} \
    CONFIG.C_I_LMB {1} \
    CONFIG.G_TEMPLATE_LIST {8} \
  ] $microblaze_0


  # Create instance: microblaze_0_local_memory
  create_hier_cell_microblaze_0_local_memory [current_bd_instance .] microblaze_0_local_memory

  # Create instance: DDR_HIER
  create_hier_cell_DDR_HIER [current_bd_instance .] DDR_HIER

  # Create instance: clk_and_reset
  create_hier_cell_clk_and_reset [current_bd_instance .] clk_and_reset

  # Create instance: xdma_0, and set properties
  set xdma_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:xdma:4.1 xdma_0 ]
  set_property -dict [list \
    CONFIG.axilite_master_en {false} \
    CONFIG.cfg_mgmt_if {false} \
    CONFIG.pcie_extended_tag {true} \
    CONFIG.pf0_base_class_menu {Display_controller} \
    CONFIG.pf0_msi_enabled {false} \
    CONFIG.pf0_sub_class_interface_menu {Other_display_controller} \
    CONFIG.pl_link_cap_max_link_speed {5.0_GT/s} \
    CONFIG.pl_link_cap_max_link_width {X4} \
    CONFIG.xdma_axi_intf_mm {AXI_Stream} \
  ] $xdma_0


  # Create instance: xlconstant_0, and set properties
  set xlconstant_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 xlconstant_0 ]
  set_property CONFIG.CONST_VAL {0} $xlconstant_0


  # Create instance: util_ds_buf_0, and set properties
  set util_ds_buf_0 [ create_bd_cell -type ip -vlnv xilinx.com:ip:util_ds_buf:2.2 util_ds_buf_0 ]
  set_property CONFIG.C_BUF_TYPE {IBUFDSGTE} $util_ds_buf_0


  # Create instance: xdma_ila, and set properties
  set xdma_ila [ create_bd_cell -type ip -vlnv xilinx.com:ip:system_ila:1.1 xdma_ila ]
  set_property -dict [list \
    CONFIG.C_MON_TYPE {MIX} \
    CONFIG.C_NUM_MONITOR_SLOTS {1} \
    CONFIG.C_NUM_OF_PROBES {3} \
    CONFIG.C_SLOT {0} \
    CONFIG.C_SLOT_0_INTF_TYPE {xilinx.com:interface:axis_rtl:1.0} \
    CONFIG.C_SLOT_1_INTF_TYPE {xilinx.com:interface:axis_rtl:1.0} \
  ] $xdma_ila


  # Create instance: frame_counter_0, and set properties
  set frame_counter_0 [ create_bd_cell -type ip -vlnv xilinx.com:user:frame_counter:1.0 frame_counter_0 ]
  set_property CONFIG.clk_freq {125000000} $frame_counter_0


  # Create interface connections
  connect_bd_intf_net -intf_net CLK_IN1_D_1 [get_bd_intf_ports sys_ddr] [get_bd_intf_pins clk_and_reset/sys_ddr]
  connect_bd_intf_net -intf_net CLK_IN_D_0_1 [get_bd_intf_ports sys_clk] [get_bd_intf_pins util_ds_buf_0/CLK_IN_D]
  connect_bd_intf_net -intf_net S00_AXI_1 [get_bd_intf_pins axi_interconnect_0/S00_AXI] [get_bd_intf_pins microblaze_0_axi_periph/M05_AXI]
  connect_bd_intf_net -intf_net S02_AXI_1 [get_bd_intf_pins axi_interconnect_0/S02_AXI] [get_bd_intf_pins axi_vdma_0/M_AXI_S2MM]
  connect_bd_intf_net -intf_net S03_AXILite_1 [get_bd_intf_pins hdmi_out/S03_AXILite] [get_bd_intf_pins microblaze_0_axi_periph/M09_AXI]
  connect_bd_intf_net -intf_net axi_iic_0_IIC [get_bd_intf_ports iic_hdmi] [get_bd_intf_pins axi_iic_0/IIC]
  connect_bd_intf_net -intf_net axi_interconnect_0_M00_AXI [get_bd_intf_pins axi_interconnect_0/M00_AXI] [get_bd_intf_pins DDR_HIER/S_AXI]
  connect_bd_intf_net -intf_net axi_uartlite_0_UART [get_bd_intf_ports uart_rtl_0] [get_bd_intf_pins axi_uartlite_0/UART]
  connect_bd_intf_net -intf_net axi_vdma_0_M_AXIS_MM2S [get_bd_intf_pins axi_vdma_0/M_AXIS_MM2S] [get_bd_intf_pins hdmi_out/in_stream]
  connect_bd_intf_net -intf_net axi_vdma_0_M_AXI_MM2S [get_bd_intf_pins axi_vdma_0/M_AXI_MM2S] [get_bd_intf_pins axi_interconnect_0/S01_AXI]
  connect_bd_intf_net -intf_net microblaze_0_axi_dp [get_bd_intf_pins microblaze_0_axi_periph/S00_AXI] [get_bd_intf_pins microblaze_0/M_AXI_DP]
  connect_bd_intf_net -intf_net microblaze_0_axi_periph_M01_AXI [get_bd_intf_pins microblaze_0_axi_periph/M01_AXI] [get_bd_intf_pins axi_uartlite_0/S_AXI]
  connect_bd_intf_net -intf_net microblaze_0_axi_periph_M02_AXI [get_bd_intf_pins microblaze_0_axi_periph/M02_AXI] [get_bd_intf_pins axi_vdma_0/S_AXI_LITE]
  connect_bd_intf_net -intf_net microblaze_0_axi_periph_M03_AXI [get_bd_intf_pins microblaze_0_axi_periph/M03_AXI] [get_bd_intf_pins hdmi_out/ctrl]
  connect_bd_intf_net -intf_net microblaze_0_axi_periph_M04_AXI [get_bd_intf_pins microblaze_0_axi_periph/M04_AXI] [get_bd_intf_pins hdmi_out/S_AXI]
  connect_bd_intf_net -intf_net microblaze_0_axi_periph_M06_AXI [get_bd_intf_pins microblaze_0_axi_periph/M06_AXI] [get_bd_intf_pins hdmi_out/s_axi_lite]
  connect_bd_intf_net -intf_net microblaze_0_axi_periph_M07_AXI [get_bd_intf_pins microblaze_0_axi_periph/M07_AXI] [get_bd_intf_pins axi_iic_0/S_AXI]
  connect_bd_intf_net -intf_net microblaze_0_axi_periph_M08_AXI [get_bd_intf_pins microblaze_0_axi_periph/M08_AXI] [get_bd_intf_pins hdmi_out/S01_AXILite]
  connect_bd_intf_net -intf_net microblaze_0_debug [get_bd_intf_pins mdm_1/MBDEBUG_0] [get_bd_intf_pins microblaze_0/DEBUG]
  connect_bd_intf_net -intf_net microblaze_0_dlmb_1 [get_bd_intf_pins microblaze_0/DLMB] [get_bd_intf_pins microblaze_0_local_memory/DLMB]
  connect_bd_intf_net -intf_net microblaze_0_ilmb_1 [get_bd_intf_pins microblaze_0/ILMB] [get_bd_intf_pins microblaze_0_local_memory/ILMB]
  connect_bd_intf_net -intf_net microblaze_0_intc_axi [get_bd_intf_pins microblaze_0_axi_periph/M00_AXI] [get_bd_intf_pins microblaze_0_axi_intc/s_axi]
  connect_bd_intf_net -intf_net microblaze_0_interrupt [get_bd_intf_pins microblaze_0_axi_intc/interrupt] [get_bd_intf_pins microblaze_0/INTERRUPT]
  connect_bd_intf_net -intf_net mig_7series_0_DDR3 [get_bd_intf_ports DDR3] [get_bd_intf_pins DDR_HIER/DDR3]
  connect_bd_intf_net -intf_net xdma_0_M_AXIS_H2C_0 [get_bd_intf_pins xdma_0/M_AXIS_H2C_0] [get_bd_intf_pins axi_vdma_0/S_AXIS_S2MM]
connect_bd_intf_net -intf_net [get_bd_intf_nets xdma_0_M_AXIS_H2C_0] [get_bd_intf_pins xdma_0/M_AXIS_H2C_0] [get_bd_intf_pins xdma_ila/SLOT_0_AXIS]
connect_bd_intf_net -intf_net [get_bd_intf_nets xdma_0_M_AXIS_H2C_0] [get_bd_intf_pins xdma_0/M_AXIS_H2C_0] [get_bd_intf_pins frame_counter_0/v_axis]
  connect_bd_intf_net -intf_net xdma_0_pcie_mgt [get_bd_intf_ports pcie_mgt_0] [get_bd_intf_pins xdma_0/pcie_mgt]

  # Create port connections
  connect_bd_net -net M00_ACLK_1 [get_bd_pins DDR_HIER/ui_clk] [get_bd_pins axi_interconnect_0/M00_ACLK]
  connect_bd_net -net M00_ARESETN_2 [get_bd_pins DDR_HIER/aresetn] [get_bd_pins axi_interconnect_0/M00_ARESETN]
  connect_bd_net -net SYS_Rst_1 [get_bd_pins clk_and_reset/peripheral_reset] [get_bd_pins microblaze_0_local_memory/SYS_Rst]
  connect_bd_net -net arstn_200mhz [get_bd_pins clk_and_reset/arstn_200mhz] [get_bd_pins microblaze_0_axi_periph/M03_ARESETN] [get_bd_pins microblaze_0_axi_periph/M04_ARESETN] [get_bd_pins axi_interconnect_0/ARESETN] [get_bd_pins axi_interconnect_0/S01_ARESETN] [get_bd_ports vid_aresetn] [get_bd_pins hdmi_out/s_axi_aresetn] [get_bd_pins microblaze_0_axi_periph/M09_ARESETN] [get_bd_pins microblaze_0_axi_periph/M08_ARESETN] [get_bd_pins microblaze_0_axi_periph/ARESETN] [get_bd_pins axi_uartlite_0/s_axi_aresetn] [get_bd_pins microblaze_0_axi_periph/M05_ARESETN] [get_bd_pins axi_interconnect_0/S00_ARESETN] [get_bd_pins microblaze_0_axi_periph/M06_ARESETN] [get_bd_pins axi_iic_0/s_axi_aresetn] [get_bd_pins microblaze_0_axi_periph/M07_ARESETN] [get_bd_pins hdmi_out/s_axi_aresetn1] [get_bd_pins microblaze_0_axi_periph/M01_ARESETN] [get_bd_pins microblaze_0_axi_periph/S00_ARESETN] [get_bd_pins microblaze_0_axi_intc/s_axi_aresetn] [get_bd_pins microblaze_0_axi_periph/M00_ARESETN]
  connect_bd_net -net axi_vdma_0_mm2s_frame_ptr_out [get_bd_pins axi_vdma_0/mm2s_frame_ptr_out] [get_bd_pins hdmi_out/probe4]
  connect_bd_net -net axi_vdma_0_mm2s_introut [get_bd_pins axi_vdma_0/mm2s_introut] [get_bd_pins microblaze_0_xlconcat/In0]
  connect_bd_net -net axi_vdma_0_s2mm_frame_ptr_out [get_bd_pins axi_vdma_0/s2mm_frame_ptr_out] [get_bd_pins hdmi_out/probe5]
  connect_bd_net -net clk_200mhz [get_bd_pins clk_and_reset/clk_200mhz] [get_bd_pins axi_vdma_0/m_axi_mm2s_aclk] [get_bd_pins axi_vdma_0/m_axis_mm2s_aclk] [get_bd_pins axi_interconnect_0/ACLK] [get_bd_pins axi_interconnect_0/S01_ACLK] [get_bd_pins hdmi_out/s_axi_aclk] [get_bd_pins axi_uartlite_0/s_axi_aclk] [get_bd_pins axi_interconnect_0/S00_ACLK] [get_bd_pins axi_iic_0/s_axi_aclk] [get_bd_pins hdmi_out/clk_in1] [get_bd_pins microblaze_0_axi_periph/ACLK] [get_bd_pins microblaze_0_axi_periph/M01_ACLK] [get_bd_pins microblaze_0_axi_periph/M03_ACLK] [get_bd_pins microblaze_0_axi_periph/M04_ACLK] [get_bd_pins microblaze_0_axi_periph/M05_ACLK] [get_bd_pins microblaze_0_axi_periph/M06_ACLK] [get_bd_pins microblaze_0_axi_periph/M07_ACLK] [get_bd_pins microblaze_0_axi_periph/M08_ACLK] [get_bd_pins microblaze_0_axi_periph/M09_ACLK] [get_bd_pins DDR_HIER/sys_clk_i] [get_bd_pins microblaze_0/Clk] [get_bd_pins microblaze_0_local_memory/LMB_Clk] [get_bd_pins microblaze_0_axi_periph/S00_ACLK] [get_bd_pins microblaze_0_axi_intc/processor_clk] [get_bd_pins microblaze_0_axi_intc/s_axi_aclk] [get_bd_pins microblaze_0_axi_periph/M00_ACLK]
  connect_bd_net -net clk_wiz_0_locked [get_bd_pins clk_and_reset/locked] [get_bd_pins DDR_HIER/sys_rst]
  connect_bd_net -net frame_counter_0_count_frame [get_bd_pins frame_counter_0/count_frame] [get_bd_pins xdma_ila/probe0]
  connect_bd_net -net frame_counter_0_count_frame_old [get_bd_pins frame_counter_0/count_frame_old] [get_bd_pins xdma_ila/probe1]
  connect_bd_net -net frame_counter_0_count_frame_total [get_bd_pins frame_counter_0/count_frame_total] [get_bd_pins xdma_ila/probe2]
  connect_bd_net -net hdmi_out_vid_active_video_0 [get_bd_pins hdmi_out/vid_active_video_0] [get_bd_ports vid_active_video_0]
  connect_bd_net -net hdmi_out_vid_data_0 [get_bd_pins hdmi_out/vid_data_0] [get_bd_ports vid_data_0]
  connect_bd_net -net hdmi_out_vid_hsync_0 [get_bd_pins hdmi_out/vid_hsync_0] [get_bd_ports vid_hsync_0]
  connect_bd_net -net hdmi_out_vid_vsync_0 [get_bd_pins hdmi_out/vid_vsync_0] [get_bd_ports vid_vsync_0]
  connect_bd_net -net mdm_1_debug_sys_rst [get_bd_pins mdm_1/Debug_SYS_Rst] [get_bd_pins clk_and_reset/mb_debug_sys_rst]
  connect_bd_net -net microblaze_0_intr [get_bd_pins microblaze_0_xlconcat/dout] [get_bd_pins microblaze_0_axi_intc/intr]
  connect_bd_net -net mig_7series_0_mmcm_locked [get_bd_pins DDR_HIER/mmcm_locked] [get_bd_pins clk_and_reset/dcm_locked]
  connect_bd_net -net mig_7series_0_ui_clk_sync_rst [get_bd_pins DDR_HIER/ui_clk_sync_rst] [get_bd_pins hdmi_out/ext_reset_in] [get_bd_pins clk_and_reset/ext_reset_in]
  connect_bd_net -net reset_n_1 [get_bd_ports reset_n] [get_bd_pins clk_and_reset/reset_n]
  connect_bd_net -net rst_clk_wiz_0_100M_mb_reset [get_bd_pins clk_and_reset/mb_reset1] [get_bd_pins microblaze_0/Reset] [get_bd_pins microblaze_0_axi_intc/processor_rst]
  connect_bd_net -net sys_rst_n_0_1 [get_bd_ports sys_rst_n] [get_bd_pins xdma_0/sys_rst_n]
  connect_bd_net -net util_ds_buf_0_IBUF_OUT [get_bd_pins util_ds_buf_0/IBUF_OUT] [get_bd_pins xdma_0/sys_clk]
  connect_bd_net -net video_clk_wiz_clk_out1 [get_bd_pins hdmi_out/clk_out1] [get_bd_ports vid_aclk]
  connect_bd_net -net xdma_0_axi_aclk [get_bd_pins xdma_0/axi_aclk] [get_bd_pins axi_vdma_0/s_axis_s2mm_aclk] [get_bd_pins axi_vdma_0/m_axi_s2mm_aclk] [get_bd_pins axi_interconnect_0/S02_ACLK] [get_bd_pins axi_vdma_0/s_axi_lite_aclk] [get_bd_pins microblaze_0_axi_periph/M02_ACLK] [get_bd_pins xdma_ila/clk] [get_bd_pins frame_counter_0/clk]
  connect_bd_net -net xdma_0_axi_aresetn [get_bd_pins xdma_0/axi_aresetn] [get_bd_pins axi_interconnect_0/S02_ARESETN] [get_bd_pins axi_vdma_0/axi_resetn] [get_bd_pins microblaze_0_axi_periph/M02_ARESETN] [get_bd_pins xdma_ila/resetn] [get_bd_pins frame_counter_0/resetn]
  connect_bd_net -net xlconstant_0_dout [get_bd_pins xlconstant_0/dout] [get_bd_pins xdma_0/usr_irq_req]

  # Create address segments
  assign_bd_address -offset 0x80000000 -range 0x40000000 -target_address_space [get_bd_addr_spaces axi_vdma_0/Data_MM2S] [get_bd_addr_segs DDR_HIER/mig_7series_0/memmap/memaddr] -force
  assign_bd_address -offset 0x80000000 -range 0x40000000 -target_address_space [get_bd_addr_spaces axi_vdma_0/Data_S2MM] [get_bd_addr_segs DDR_HIER/mig_7series_0/memmap/memaddr] -force
  assign_bd_address -offset 0x40800000 -range 0x00010000 -target_address_space [get_bd_addr_spaces microblaze_0/Data] [get_bd_addr_segs axi_iic_0/S_AXI/Reg] -force
  assign_bd_address -offset 0x40600000 -range 0x00010000 -target_address_space [get_bd_addr_spaces microblaze_0/Data] [get_bd_addr_segs axi_uartlite_0/S_AXI/Reg] -force
  assign_bd_address -offset 0x44A00000 -range 0x00010000 -target_address_space [get_bd_addr_spaces microblaze_0/Data] [get_bd_addr_segs axi_vdma_0/S_AXI_LITE/Reg] -force
  assign_bd_address -offset 0x00040000 -range 0x00010000 -target_address_space [get_bd_addr_spaces microblaze_0/Data] [get_bd_addr_segs hdmi_out/color_convert/s_axi_control/Reg] -force
  assign_bd_address -offset 0x00000000 -range 0x00040000 -target_address_space [get_bd_addr_spaces microblaze_0/Data] [get_bd_addr_segs microblaze_0_local_memory/dlmb_bram_if_cntlr/SLMB/Mem] -force
  assign_bd_address -offset 0x41200000 -range 0x00010000 -target_address_space [get_bd_addr_spaces microblaze_0/Data] [get_bd_addr_segs microblaze_0_axi_intc/S_AXI/Reg] -force
  assign_bd_address -offset 0x80000000 -range 0x40000000 -target_address_space [get_bd_addr_spaces microblaze_0/Data] [get_bd_addr_segs DDR_HIER/mig_7series_0/memmap/memaddr] -force
  assign_bd_address -offset 0x00050000 -range 0x00010000 -target_address_space [get_bd_addr_spaces microblaze_0/Data] [get_bd_addr_segs hdmi_out/pixel_unpack/s_axi_control/Reg] -force
  assign_bd_address -offset 0x44A10000 -range 0x00010000 -target_address_space [get_bd_addr_spaces microblaze_0/Data] [get_bd_addr_segs hdmi_out/v_tc_0/ctrl/Reg] -force
  assign_bd_address -offset 0x44A20000 -range 0x00010000 -target_address_space [get_bd_addr_spaces microblaze_0/Data] [get_bd_addr_segs hdmi_out/video_clk_wiz/s_axi_lite/Reg] -force
  assign_bd_address -offset 0x40000000 -range 0x00010000 -target_address_space [get_bd_addr_spaces microblaze_0/Data] [get_bd_addr_segs hdmi_out/video_lock_monitor/S_AXI/Reg] -force
  assign_bd_address -offset 0x00000000 -range 0x00010000 -target_address_space [get_bd_addr_spaces microblaze_0/Instruction] [get_bd_addr_segs microblaze_0_local_memory/ilmb_bram_if_cntlr/SLMB/Mem] -force


  # Restore current instance
  current_bd_instance $oldCurInst

  validate_bd_design
  save_bd_design
}
# End of create_root_design()


##################################################################
# MAIN FLOW
##################################################################

create_root_design ""


