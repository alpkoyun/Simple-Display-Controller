# Recreate the Windows-origin PCIe Vivado project with Linux-local paths.
#
# This wrapper keeps the exported Vivado script as the source of truth, but
# patches the one path that is wrong for this repository layout:
#   export/PCIe.tcl expects ../../IPs/IP_Packages from export/
#   this checkout has ../IPs/IP_Packages from export/

set origin_dir [file normalize [file join [file dirname [info script]] .. export]]
set project_name PCIe

for {set i 0} {$i < $::argc} {incr i} {
    set option [lindex $::argv $i]
    switch -- $option {
        "--origin_dir" {
            incr i
            set origin_dir [file normalize [lindex $::argv $i]]
        }
        "--project_name" {
            incr i
            set project_name [lindex $::argv $i]
        }
        default {
            puts "ERROR: unknown option $option"
            exit 2
        }
    }
}

set export_tcl [file join $origin_dir PCIe.tcl]
if {![file isfile $export_tcl]} {
    puts "ERROR: missing exported project script: $export_tcl"
    exit 1
}

set fixed_ip_repo [file normalize [file join $origin_dir .. IPs IP_Packages]]
if {![file isdirectory $fixed_ip_repo]} {
    puts "ERROR: missing custom IP repository: $fixed_ip_repo"
    exit 1
}

set fh [open $export_tcl r]
set body [read $fh]
close $fh

set body [string map [list \
    {[file normalize "$origin_dir/../../IPs/IP_Packages"]} {[file normalize "$origin_dir/../IPs/IP_Packages"]} \
    {[file normalize "$origin_dir/../../IPs/IP_Packages"]} {[file normalize "$origin_dir/../IPs/IP_Packages"]} \
] $body]

set saved_argv $::argv
set saved_argc $::argc
set ::argv [list --origin_dir $origin_dir --project_name $project_name]
set ::argc [llength $::argv]

set code [catch {uplevel #0 $body} result options]

set ::argv $saved_argv
set ::argc $saved_argc

if {$code != 0} {
    puts "ERROR: project recreation failed: $result"
    puts [dict get $options -errorinfo]
    exit 1
}

set srcset [get_filesets sources_1]
set_property ip_repo_paths [list \
    [file normalize [file join $origin_dir .. hls]] \
    [file normalize [file join $origin_dir .. IPs IP_Packages]] \
] $srcset
update_ip_catalog -rebuild
update_compile_order -fileset sources_1

puts "INFO: Linux project recreation complete: [get_property DIRECTORY [current_project]]"
