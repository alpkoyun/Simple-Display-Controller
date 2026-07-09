set cfgmem_part "mt25ql128-spi-x1_x2_x4"

for {set i 0} {$i < $::argc} {incr i} {
    set option [lindex $::argv $i]
    switch -- $option {
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

proc dump_props {label obj pattern} {
    puts "INFO: $label properties matching $pattern"
    foreach prop [lsort [list_property $obj]] {
        if {[string match -nocase $pattern $prop]} {
            if {[catch {get_property $prop $obj} value]} {
                set value "<unreadable>"
            }
            puts "$prop = $value"
        }
    }
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
dump_props "cfgmem" $cfgmem "*"
dump_props "device" $dev "*CFG*"
dump_props "device" $dev "*MEM*"
