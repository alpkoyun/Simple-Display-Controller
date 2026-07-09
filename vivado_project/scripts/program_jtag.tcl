set bit_file ""
set ltx_file ""

for {set i 0} {$i < $::argc} {incr i} {
    set option [lindex $::argv $i]
    switch -- $option {
        "--bit" {
            incr i
            set bit_file [file normalize [lindex $::argv $i]]
        }
        "--ltx" {
            incr i
            set ltx_file [file normalize [lindex $::argv $i]]
        }
        default {
            puts "ERROR: unknown option $option"
            exit 2
        }
    }
}

if {$bit_file eq "" || ![file isfile $bit_file]} {
    puts "ERROR: pass --bit /path/to/PCIe_wrapper.bit"
    exit 1
}

open_hw_manager
connect_hw_server
open_hw_target
set dev [lindex [get_hw_devices xc7a200t_0] 0]
if {$dev eq ""} {
    puts "ERROR: xc7a200t_0 not found"
    exit 1
}
current_hw_device $dev
refresh_hw_device $dev
set_property PROGRAM.FILE $bit_file $dev
if {$ltx_file ne "" && [file isfile $ltx_file]} {
    set_property PROBES.FILE $ltx_file $dev
}
program_hw_devices $dev
refresh_hw_device $dev
puts "INFO: programmed $dev with $bit_file"
