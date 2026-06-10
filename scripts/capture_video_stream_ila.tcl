set ltx_file [lindex $argv 0]
set out_dir [lindex $argv 1]
set trigger_name [lindex $argv 2]
set ila_name [lindex $argv 3]
if {$ltx_file eq ""} {
    set ltx_file "fpga_hardware/PCIe_wrapper/PCIe_wrapper.ltx"
}
if {$out_dir eq ""} {
    set out_dir "tmp_ila_capture"
}
if {$trigger_name eq ""} {
    set trigger_name "tvalid"
}
if {$ila_name eq ""} {
    set ila_name "xdma"
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

proc probe_candidates {name} {
    set lname [string tolower $name]
    if {$lname eq "tdata"} {
        return [list "*axis_tdata*" "*TDATA*" "*probe3*"]
    }
    if {$lname eq "tvalid"} {
        return [list "*axis_tvalid*" "*TVALID*" "*probe5*"]
    }
    if {$lname eq "tready"} {
        return [list "*axis_tready*" "*TREADY*" "*probe6*"]
    }
    if {$lname eq "tlast"} {
        return [list "*axis_tlast*" "*TLAST*" "*probe7*"]
    }
    return [list "*$name*"]
}

proc find_probe {ila name} {
    foreach pattern [probe_candidates $name] {
        set probe [lindex [get_hw_probes $pattern -of_objects $ila] 0]
        if {$probe ne ""} {
            return $probe
        }
    }
    return ""
}

set stream_ila ""
set ila_glob "*xdma_ila*"
if {[string tolower $ila_name] eq "video"} {
    set ila_glob "*video_stream_ila*"
}
foreach ila [get_hw_ilas -of_objects $device] {
    set cell [get_property CELL_NAME $ila]
    puts "ILA: $ila CELL_NAME=$cell"
    if {[string match $ila_glob $cell]} {
        set stream_ila $ila
    }
}
if {$stream_ila eq ""} {
    fail "$ila_name ILA not found using pattern $ila_glob"
}

current_hw_ila $stream_ila
puts "STREAM_ILA: $stream_ila"

set probes [get_hw_probes -of_objects $stream_ila]
foreach probe $probes {
    puts "PROBE: $probe WIDTH=[get_property WIDTH $probe]"
}

set trigger_probe [find_probe $stream_ila $trigger_name]
if {$trigger_probe eq ""} {
    fail "$trigger_name probe not found"
}

puts "TRIGGER_PROBE: $trigger_probe"
set_property TRIGGER_COMPARE_VALUE {eq1'b1} $trigger_probe

if {[catch {set_property CONTROL.TRIGGER_POSITION 0 $stream_ila} err]} {
    puts "WARN: could not set trigger position: $err"
}

run_hw_ila $stream_ila
wait_on_hw_ila $stream_ila
set data [upload_hw_ila_data $stream_ila]

set csv_file [file join $out_dir ${ila_name}_ila_$trigger_name.csv]
set wdb_file [file join $out_dir ${ila_name}_ila_$trigger_name.wdb]
write_hw_ila_data -force -csv_file $csv_file $data
write_hw_ila_data -force $wdb_file $data

puts "CAPTURE_CSV: $csv_file"
puts "CAPTURE_WDB: $wdb_file"

close_hw_manager
