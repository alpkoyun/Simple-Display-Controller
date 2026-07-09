if {$::argc > 0} {
    set patterns $::argv
} else {
    set patterns {mt25* n25q* mx25* w25* s25*}
}

foreach pattern $patterns {
    puts "INFO: cfgmem parts matching $pattern"
    set found 0
    foreach part [lsort [get_cfgmem_parts]] {
        if {[string match -nocase $pattern $part]} {
            puts $part
            set found 1
        }
    }
    if {!$found} {
        puts "INFO: no matches"
    }
}
