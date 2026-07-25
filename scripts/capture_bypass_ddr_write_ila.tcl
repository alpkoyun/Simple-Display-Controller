set ltx_file [lindex $argv 0]
set out_dir [lindex $argv 1]

if {$ltx_file eq ""} {
    set ltx_file "fpga_hardware/PCIe_wrapper/PCIe_wrapper.ltx"
}
if {$out_dir eq ""} {
    set out_dir "tmp_bypass_ddr_write_capture"
}

file mkdir $out_dir

proc fail {msg} {
    puts "ERROR: $msg"
    flush stdout
    exit 1
}

proc find_probe {ila pattern} {
    return [lindex [get_hw_probes $pattern -of_objects $ila] 0]
}

proc clear_trigger_compares {ila} {
    foreach probe [get_hw_probes -of_objects $ila] {
        set width [get_property WIDTH $probe]
        set dont_care "eq${width}'b[string repeat X $width]"
        catch {set_property TRIGGER_COMPARE_VALUE $dont_care $probe}
    }
}

open_hw_manager
connect_hw_server -url TCP:127.0.0.1:3121
refresh_hw_server [current_hw_server]
set targets [get_hw_targets *]
if {[llength $targets] == 0} {
    fail "no hardware targets found"
}
open_hw_target [lindex $targets 0]

set devices [get_hw_devices]
if {[llength $devices] == 0} {
    fail "no hardware devices found"
}
set device [lindex $devices 0]
current_hw_device $device
set_property PROBES.FILE $ltx_file $device
refresh_hw_device $device

set bypass_ila ""
set ddr_ila ""
foreach ila [get_hw_ilas -of_objects $device] {
    set cell [get_property CELL_NAME $ila]
    puts "ILA: $ila CELL_NAME=$cell"
    if {[string match "*xdma_ila*" $cell]} {
        set bypass_ila $ila
    }
    if {[string match "*ddr3_ila*" $cell]} {
        set ddr_ila $ila
    }
}
if {$bypass_ila eq ""} {
    fail "XDMA bypass ILA not found"
}
if {$ddr_ila eq ""} {
    fail "DDR3 slave ILA not found"
}

set bypass_aw_ctrl [find_probe $bypass_ila "*slot_1_axi_aw_ctrl*"]
set bypass_awaddr [find_probe $bypass_ila "*slot_1_axi_awaddr*"]
set ddr_aw_ctrl [find_probe $ddr_ila "*axi_aw_ctrl*"]
set ddr_awaddr [find_probe $ddr_ila "*axi_awaddr*"]

foreach {label probe} [list \
        BYPASS_AW_CTRL $bypass_aw_ctrl BYPASS_AWADDR $bypass_awaddr \
        DDR_AW_CTRL $ddr_aw_ctrl DDR_AWADDR $ddr_awaddr] {
    if {$probe eq ""} {
        fail "$label probe not found"
    }
    puts "$label: $probe WIDTH=[get_property WIDTH $probe]"
}

clear_trigger_compares $bypass_ila
clear_trigger_compares $ddr_ila

# The driver writes the diagnostic frame through BAR2+0x02000000.  XDMA's
# bypass translation presents this as AXI address 0x3e000000.
set_property TRIGGER_COMPARE_VALUE {eq2'b11} $bypass_aw_ctrl
set_property TRIGGER_COMPARE_VALUE {eq64'h000000003e000000} $bypass_awaddr

# Capture the address that actually reaches the MIG slave.  If the bypass and
# VDMA mappings alias the same DDR bytes, this would need to be offset zero,
# matching the paired VDMA read capture.
set_property TRIGGER_COMPARE_VALUE {eq2'b11} $ddr_aw_ctrl
set_property TRIGGER_COMPARE_VALUE {eq30'h3e000000} $ddr_awaddr

foreach ila [list $bypass_ila $ddr_ila] {
    set depth [get_property CONTROL.DATA_DEPTH $ila]
    set trigger_position [expr {$depth / 4}]
    set_property CONTROL.TRIGGER_POSITION $trigger_position $ila
    puts "CAPTURE: $ila DATA_DEPTH=$depth TRIGGER_POSITION=$trigger_position"
}

run_hw_ila $bypass_ila
run_hw_ila $ddr_ila
puts "ILA_ARMED"
flush stdout

wait_on_hw_ila $bypass_ila
wait_on_hw_ila $ddr_ila

foreach {label ila} [list xdma_bypass $bypass_ila ddr3_slave_write $ddr_ila] {
    set data [upload_hw_ila_data $ila]
    set csv_file [file join $out_dir ${label}.csv]
    set wdb_file [file join $out_dir ${label}.wdb]
    write_hw_ila_data -force -csv_file $csv_file $data
    write_hw_ila_data -force $wdb_file $data
    puts "CAPTURE_CSV: $csv_file"
    puts "CAPTURE_WDB: $wdb_file"
}

close_hw_manager
