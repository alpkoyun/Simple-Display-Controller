set bin_file ""
set cfgmem_part "mt25ql128-spi-x1_x2_x4"

proc maybe_set_property {prop value obj} {
    if {[lsearch -exact [list_property $obj] $prop] >= 0} {
        set_property $prop $value $obj
    }
}

for {set i 0} {$i < $::argc} {incr i} {
    set option [lindex $::argv $i]
    switch -- $option {
        "--bin" {
            incr i
            set bin_file [file normalize [lindex $::argv $i]]
        }
        "--cfgmem-part" {
            incr i
            set cfgmem_part [lindex $::argv $i]
        }
        default {
            puts "ERROR: unknown option $option"
            exit 2
        }
    }
}

if {$bin_file eq "" || ![file isfile $bin_file]} {
    puts "ERROR: pass --bin /path/to/PCIe_wrapper.bin"
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

set part [lindex [get_cfgmem_parts $cfgmem_part] 0]
if {$part eq ""} {
    puts "ERROR: cfgmem part not found: $cfgmem_part"
    exit 1
}

create_hw_cfgmem -hw_device $dev $part
set cfgmem [current_hw_cfgmem]
set_property PROGRAM.ADDRESS_RANGE {use_file} $cfgmem
set_property PROGRAM.FILES [list $bin_file] $cfgmem
maybe_set_property PROGRAM.PRM_FILE {} $cfgmem
maybe_set_property PROGRAM.UNUSED_PIN_TERMINATION {pull-none} $cfgmem
maybe_set_property PROGRAM.BLANK_CHECK 0 $cfgmem
set_property PROGRAM.ERASE 1 $cfgmem
set_property PROGRAM.CFG_PROGRAM 1 $cfgmem
set_property PROGRAM.VERIFY 1 $cfgmem
maybe_set_property PROGRAM.CHECKSUM 0 $cfgmem

set cfgmem_bitfile [get_property PROGRAM.HW_CFGMEM_BITFILE $dev]
if {$cfgmem_bitfile eq ""} {
    puts "ERROR: Vivado did not provide PROGRAM.HW_CFGMEM_BITFILE for $cfgmem_part"
    exit 1
}

puts "INFO: loading temporary cfgmem programmer bitstream: $cfgmem_bitfile"
create_hw_bitstream -hw_device $dev $cfgmem_bitfile
program_hw_devices $dev
refresh_hw_device $dev

puts "INFO: programming SPI cfgmem $cfgmem_part from $bin_file"
program_hw_cfgmem $cfgmem
puts "INFO: programmed SPI cfgmem $cfgmem_part from $bin_file"
