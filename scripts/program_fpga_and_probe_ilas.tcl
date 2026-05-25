set bit_file ""
set ltx_file ""
set target_filter "*"

proc usage {} {
    puts "usage: program_fpga_and_probe_ilas.tcl -bit <file> -ltx <file> ?-target <glob>?"
}

for {set i 0} {$i < [llength $argv]} {incr i} {
    set arg [lindex $argv $i]
    switch -- $arg {
        "-bit" {
            incr i
            set bit_file [lindex $argv $i]
        }
        "-ltx" {
            incr i
            set ltx_file [lindex $argv $i]
        }
        "-target" {
            incr i
            set target_filter [lindex $argv $i]
        }
        default {
            puts "ERROR: unknown argument: $arg"
            usage
            exit 2
        }
    }
}

if {$bit_file eq "" || $ltx_file eq ""} {
    usage
    exit 2
}

if {![file exists $bit_file]} {
    puts "ERROR: bitstream does not exist: $bit_file"
    exit 2
}

if {![file exists $ltx_file]} {
    puts "ERROR: probes file does not exist: $ltx_file"
    exit 2
}

proc print_list {label values} {
    puts $label
    if {[llength $values] == 0} {
        puts "  <none>"
        return
    }
    foreach value $values {
        puts "  $value"
    }
}

proc print_property_if_present {indent object prop} {
    if {[catch {set value [get_property $prop $object]}]} {
        return
    }
    if {$value ne ""} {
        puts "$indent$prop: $value"
    }
}

open_hw_manager
connect_hw_server -url TCP:127.0.0.1:3121

set server [current_hw_server]
puts "HW_SERVER: $server"
refresh_hw_server $server

set targets [get_hw_targets $target_filter]
print_list "HW_TARGETS:" $targets
if {[llength $targets] == 0} {
    puts "ERROR: no hardware targets matched '$target_filter'"
    close_hw_manager
    exit 1
}

set programmed_devices {}
foreach target $targets {
    puts "OPEN_TARGET: $target"
    if {[catch {open_hw_target $target} err]} {
        puts "  ERROR: $err"
        continue
    }

    set devices [get_hw_devices]
    print_list "HW_DEVICES:" $devices

    foreach device $devices {
        current_hw_device $device
        puts "DEVICE: $device"
        print_property_if_present "  " $device PART
        print_property_if_present "  " $device IDCODE
        puts "  PROGRAM.FILE: $bit_file"
        puts "  PROBES.FILE: $ltx_file"
        set_property PROGRAM.FILE $bit_file $device
        set_property PROBES.FILE $ltx_file $device

        if {[catch {program_hw_devices $device} err]} {
            puts "  PROGRAM.ERROR: $err"
            continue
        }
        lappend programmed_devices [get_property NAME $device]
        puts "  PROGRAM.OK: $device"

        if {[catch {refresh_hw_device $device} err]} {
            puts "  REFRESH.ERROR: $err"
            continue
        }

        set ilas [get_hw_ilas -of_objects $device]
        print_list "HW_ILAS:" $ilas
        foreach ila $ilas {
            puts "  ILA: $ila"
            print_property_if_present "    " $ila CELL_NAME
            print_property_if_present "    " $ila CORE_REFRESH_RATE_MS
            set probes [get_hw_probes -of_objects $ila]
            puts "    PROBE_COUNT: [llength $probes]"
            foreach probe $probes {
                puts "    PROBE: $probe"
            }
        }
    }
}

if {[llength $programmed_devices] == 0} {
    close_hw_manager
    puts "ERROR: no devices were programmed"
    exit 1
}

puts "PROGRAMMED_DEVICES: $programmed_devices"
close_hw_manager
