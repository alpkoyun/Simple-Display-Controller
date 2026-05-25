set ltx_file [lindex $argv 0]
set out_dir [lindex $argv 1]
set trigger_name [lindex $argv 2]
if {$ltx_file eq ""} {
    set ltx_file "fpga_hardware/PCIe_wrapper/PCIe_wrapper.ltx"
}
if {$out_dir eq ""} {
    set out_dir "tmp_ila_capture"
}
if {$trigger_name eq ""} {
    set trigger_name "tvalid"
}
file mkdir $out_dir

proc fail {msg} {
    puts "ERROR: $msg"
    exit 1
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

puts "DEVICE: $device"
puts "PART: [get_property PART $device]"
puts "IDCODE: [get_property IDCODE $device]"
puts "PROBES.FILE: $ltx_file"

set video_ila ""
foreach ila [get_hw_ilas -of_objects $device] {
    set cell [get_property CELL_NAME $ila]
    puts "ILA: $ila CELL_NAME=$cell"
    if {[string match "*video_stream_ila*" $cell]} {
        set video_ila $ila
    }
}
if {$video_ila eq ""} {
    fail "video_stream_ila not found"
}

current_hw_ila $video_ila
puts "VIDEO_ILA: $video_ila"

set probes [get_hw_probes -of_objects $video_ila]
foreach probe $probes {
    puts "PROBE: $probe WIDTH=[get_property WIDTH $probe]"
}

set trigger_probe [lindex [get_hw_probes *$trigger_name* -of_objects $video_ila] 0]
if {$trigger_probe eq "" && $trigger_name eq "tvalid"} {
    set trigger_probe [lindex [get_hw_probes probe6 -of_objects $video_ila] 0]
}
if {$trigger_probe eq ""} {
    fail "$trigger_name probe not found"
}

puts "TRIGGER_PROBE: $trigger_probe"
set_property TRIGGER_COMPARE_VALUE {eq1'b1} $trigger_probe

if {[catch {set_property CONTROL.TRIGGER_POSITION 0 $video_ila} err]} {
    puts "WARN: could not set trigger position: $err"
}

run_hw_ila $video_ila
wait_on_hw_ila $video_ila
set data [upload_hw_ila_data $video_ila]

set csv_file [file join $out_dir video_stream_ila_$trigger_name.csv]
set wdb_file [file join $out_dir video_stream_ila_$trigger_name.wdb]
write_hw_ila_data -force -csv_file $csv_file $data
write_hw_ila_data -force $wdb_file $data

puts "CAPTURE_CSV: $csv_file"
puts "CAPTURE_WDB: $wdb_file"

close_hw_manager
