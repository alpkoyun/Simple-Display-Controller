set ltx_file [lindex $argv 0]
set out_dir [lindex $argv 1]
set vdma_address_arg [lindex $argv 2]
set ddr_address_arg [lindex $argv 3]

if {$ltx_file eq ""} {
    set ltx_file "fpga_hardware/PCIe_wrapper/PCIe_wrapper.ltx"
}
if {$out_dir eq ""} {
    set out_dir "tmp_vdma_ddr_ila_capture"
}
if {$vdma_address_arg eq ""} {
    set vdma_address_arg "0x3e000000"
}
if {$ddr_address_arg eq ""} {
    set ddr_address_arg "0x00000000"
}

set vdma_address [expr {$vdma_address_arg}]
set ddr_address [expr {$ddr_address_arg}]

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

set vdma_ila ""
set ddr_ila ""
foreach ila [get_hw_ilas -of_objects $device] {
    set cell [get_property CELL_NAME $ila]
    puts "ILA: $ila CELL_NAME=$cell"
    if {[string match "*system_ila_0*" $cell]} {
        set vdma_ila $ila
    }
    if {[string match "*ddr3_ila*" $cell]} {
        set ddr_ila $ila
    }
}
if {$vdma_ila eq ""} {
    fail "VDMA MM2S ILA (system_ila_0) not found"
}
if {$ddr_ila eq ""} {
    fail "DDR3 slave ILA (ddr3_ila) not found"
}

set vdma_ar_ctrl [find_probe $vdma_ila "*axi_ar_ctrl*"]
set vdma_araddr [find_probe $vdma_ila "*axi_araddr*"]
set ddr_ar_ctrl [find_probe $ddr_ila "*axi_ar_ctrl*"]
set ddr_araddr [find_probe $ddr_ila "*axi_araddr*"]

foreach {label probe} [list \
        VDMA_AR_CTRL $vdma_ar_ctrl VDMA_ARADDR $vdma_araddr \
        DDR_AR_CTRL $ddr_ar_ctrl DDR_ARADDR $ddr_araddr] {
    if {$probe eq ""} {
        fail "$label probe not found"
    }
    puts "$label: $probe WIDTH=[get_property WIDTH $probe]"
}

clear_trigger_compares $vdma_ila
clear_trigger_compares $ddr_ila

# System ILA packs ARVALID into bit 0 and ARREADY into bit 1.  Trigger only
# on an accepted read request for the selected scanout buffer.
set_property TRIGGER_COMPARE_VALUE {eq2'b11} $vdma_ar_ctrl
set_property TRIGGER_COMPARE_VALUE [format "eq32'h%08X" $vdma_address] $vdma_araddr

# The caller supplies the corresponding translated offset in the 30-bit MIG
# slave address space (for example, 0x3e000000 maps to offset 0x00000000).
set_property TRIGGER_COMPARE_VALUE {eq2'b11} $ddr_ar_ctrl
set_property TRIGGER_COMPARE_VALUE [format "eq30'h%08X" $ddr_address] $ddr_araddr

foreach ila [list $vdma_ila $ddr_ila] {
    set depth [get_property CONTROL.DATA_DEPTH $ila]
    set trigger_position [expr {$depth / 4}]
    set_property CONTROL.TRIGGER_POSITION $trigger_position $ila
    puts "CAPTURE: $ila DATA_DEPTH=$depth TRIGGER_POSITION=$trigger_position"
}

puts "DEVICE: $device"
puts "PART: [get_property PART $device]"
puts "IDCODE: [get_property IDCODE $device]"
puts "PROBES.FILE: $ltx_file"
puts [format "VDMA_TRIGGER: ARVALID && ARREADY && ARADDR == 0x%08X" $vdma_address]
puts [format "DDR_TRIGGER: ARVALID && ARREADY && ARADDR == 0x%08X" $ddr_address]

run_hw_ila $vdma_ila
run_hw_ila $ddr_ila
puts "ILA_ARMED"
flush stdout

wait_on_hw_ila $vdma_ila
wait_on_hw_ila $ddr_ila

foreach {label ila} [list vdma_mm2s $vdma_ila ddr3_slave $ddr_ila] {
    set data [upload_hw_ila_data $ila]
    set csv_file [file join $out_dir ${label}.csv]
    set wdb_file [file join $out_dir ${label}.wdb]
    write_hw_ila_data -force -csv_file $csv_file $data
    write_hw_ila_data -force $wdb_file $data
    puts "CAPTURE_CSV: $csv_file"
    puts "CAPTURE_WDB: $wdb_file"
}

close_hw_manager
