# Write an SPI x4 cfgmem .bin from an existing bitstream.

set bit_file ""
set bin_file ""
set cfg_size_mb 16

for {set i 0} {$i < $::argc} {incr i} {
    set option [lindex $::argv $i]
    switch -- $option {
        "--bit" {
            incr i
            set bit_file [file normalize [lindex $::argv $i]]
        }
        "--bin" {
            incr i
            set bin_file [file normalize [lindex $::argv $i]]
        }
        "--size-mb" {
            incr i
            set cfg_size_mb [lindex $::argv $i]
        }
        default {
            puts "ERROR: unknown option $option"
            exit 2
        }
    }
}

if {$bit_file eq "" || ![file isfile $bit_file]} {
    puts "ERROR: pass --bit /path/to/file.bit"
    exit 1
}
if {$bin_file eq ""} {
    puts "ERROR: pass --bin /path/to/file.bin"
    exit 1
}

file mkdir [file dirname $bin_file]
write_cfgmem -force -format bin -interface SPIx4 -size $cfg_size_mb \
    -loadbit [list up 0x0 $bit_file] \
    -file $bin_file
puts "INFO: wrote cfgmem bin $bin_file from $bit_file"
