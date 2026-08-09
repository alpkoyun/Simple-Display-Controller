set ltx_file [lindex $argv 0]
set out_dir [lindex $argv 1]

if {$ltx_file eq ""} {
    set ltx_file "fpga_hardware/PCIe_wrapper/PCIe_wrapper.ltx"
}
if {$out_dir eq ""} {
    set out_dir "tmp_bypass_ila_capture"
}

file mkdir $out_dir

proc fail {msg} {
    puts "ERROR: $msg"
    exit 1
}

proc find_probe {ila pattern} {
    return [lindex [get_hw_probes $pattern -of_objects $ila] 0]
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
foreach ila [get_hw_ilas -of_objects $device] {
    set cell [get_property CELL_NAME $ila]
    puts "ILA: $ila CELL_NAME=$cell"
    if {[string match "*xdma_ila*" $cell]} {
        set bypass_ila $ila
    }
}
if {$bypass_ila eq ""} {
    fail "XDMA/bypass ILA not found"
}

current_hw_ila $bypass_ila
set aw_ctrl [find_probe $bypass_ila "*axi_aw_ctrl*"]
set awaddr [find_probe $bypass_ila "*axi_awaddr*"]
set wdata [find_probe $bypass_ila "*axi_wdata*"]
set wstrb [find_probe $bypass_ila "*axi_wstrb*"]
set bresp [find_probe $bypass_ila "*axi_bresp*"]
set araddr [find_probe $bypass_ila "*axi_araddr*"]
set rdata [find_probe $bypass_ila "*axi_rdata*"]
set rresp [find_probe $bypass_ila "*axi_rresp*"]

foreach {label probe} [list \
        AW_CTRL $aw_ctrl AWADDR $awaddr WDATA $wdata WSTRB $wstrb \
        BRESP $bresp ARADDR $araddr RDATA $rdata RRESP $rresp] {
    if {$probe eq ""} {
        fail "$label probe not found"
    }
    puts "$label: $probe WIDTH=[get_property WIDTH $probe]"
}

# Clear any prior trigger comparisons in this hardware-manager session.  The
# System ILA packs VALID and READY into the two-bit AW control probe, so 2'b11
# is an accepted write-address handshake regardless of their bit ordering.
foreach probe [get_hw_probes -of_objects $bypass_ila] {
    set width [get_property WIDTH $probe]
    set dont_care "eq${width}'b[string repeat X $width]"
    catch {set_property TRIGGER_COMPARE_VALUE $dont_care $probe}
}
set_property TRIGGER_COMPARE_VALUE {eq2'b11} $aw_ctrl
# The current 32 MiB bypass-DDR slice ends at 0x3fffffff.  The scratch page at
# BAR offset 0x03fff000 must therefore appear on M_AXI_BYPASS as 0x3ffff000.
set_property TRIGGER_COMPARE_VALUE {eq64'h000000003FFFF000} $awaddr

set depth [get_property CONTROL.DATA_DEPTH $bypass_ila]
set trigger_position [expr {$depth / 4}]
set_property CONTROL.TRIGGER_POSITION $trigger_position $bypass_ila

puts "DEVICE: $device"
puts "PART: [get_property PART $device]"
puts "IDCODE: [get_property IDCODE $device]"
puts "PROBES.FILE: $ltx_file"
puts "BYPASS_ILA: $bypass_ila"
puts "DATA_DEPTH: $depth"
puts "TRIGGER_POSITION: $trigger_position"
puts "TRIGGER: AWVALID && AWREADY && AWADDR == 0x000000003ffff000"
puts "ILA_ARMED"

run_hw_ila $bypass_ila
wait_on_hw_ila $bypass_ila
set data [upload_hw_ila_data $bypass_ila]

set csv_file [file join $out_dir bypass_axi_aw.csv]
set wdb_file [file join $out_dir bypass_axi_aw.wdb]
write_hw_ila_data -force -csv_file $csv_file $data
write_hw_ila_data -force $wdb_file $data

puts "CAPTURE_CSV: $csv_file"
puts "CAPTURE_WDB: $wdb_file"
close_hw_manager
