# Generate IP products, enforce the AX7203 PCIe GT lane order, and optionally
# build bit/bin artifacts.

set project_file ""
set repo_root [file normalize [file join [file dirname [info script]] .. ..]]
set jobs 2
set do_build 0
set install_artifacts 0

for {set i 0} {$i < $::argc} {incr i} {
    set option [lindex $::argv $i]
    switch -- $option {
        "--project" {
            incr i
            set project_file [file normalize [lindex $::argv $i]]
        }
        "--repo-root" {
            incr i
            set repo_root [file normalize [lindex $::argv $i]]
        }
        "--jobs" {
            incr i
            set jobs [lindex $::argv $i]
        }
        "--build" {
            set do_build 1
        }
        "--install-artifacts" {
            set install_artifacts 1
        }
        default {
            puts "ERROR: unknown option $option"
            exit 2
        }
    }
}

if {$project_file eq "" || ![file isfile $project_file]} {
    puts "ERROR: pass --project /path/to/PCIe.xpr"
    exit 1
}

open_project $project_file
set project_dir [get_property DIRECTORY [current_project]]
set vivado_root [file normalize [file join $repo_root vivado_project]]
set srcset [get_filesets sources_1]
set_property ip_repo_paths [list \
    [file join $vivado_root hls] \
    [file join $vivado_root IPs IP_Packages] \
] $srcset
update_ip_catalog -rebuild

set locked_ips [get_ips -quiet -filter {IS_LOCKED == 1}]
if {[llength $locked_ips] > 0} {
    puts "INFO: upgrading locked IPs before block-design validation: $locked_ips"
    report_ip_status
    upgrade_ip $locked_ips
    update_compile_order -fileset sources_1
}

set bd_files [get_files -quiet *.bd]
if {[llength $bd_files] == 0} {
    puts "ERROR: no block design found in project"
    exit 1
}

foreach bd $bd_files {
    open_bd_design $bd
    validate_bd_design
}

puts "INFO: generating IP targets before lane-order check"
generate_target all $bd_files
export_ip_user_files -of_objects $bd_files -no_script -sync -force -quiet
update_compile_order -fileset sources_1

set checker [file join $vivado_root scripts check_fix_pcie_lane_order.py]
if {![file isfile $checker]} {
    puts "ERROR: missing lane-order checker: $checker"
    exit 1
}

puts "INFO: enforcing AX7203 PCIe GT lane order 5,4,6,7"
set check_code [catch {
    exec python3 $checker $project_dir --fix
} check_output]
puts $check_output
if {$check_code != 0} {
    puts "ERROR: PCIe GT lane-order check failed"
    exit 1
}

if {!$do_build} {
    puts "INFO: pre-synthesis project checks completed; pass --build to run synth/impl"
    close_project
    exit 0
}

set_property strategy Flow_PerfOptimized_high [get_runs synth_1]
set_property strategy Performance_ExplorePostRoutePhysOpt [get_runs impl_1]

puts "INFO: launching synthesis with $jobs jobs"
reset_run synth_1
launch_runs synth_1 -jobs $jobs
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {
    puts "ERROR: synthesis did not complete"
    exit 1
}

puts "INFO: re-checking lane order immediately before implementation"
set check_code [catch {
    exec python3 $checker $project_dir --fix
} check_output]
puts $check_output
if {$check_code != 0} {
    puts "ERROR: PCIe GT lane-order check failed after synthesis"
    exit 1
}

puts "INFO: launching implementation through write_bitstream with $jobs jobs"
reset_run impl_1
launch_runs impl_1 -to_step write_bitstream -jobs $jobs
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
    puts "ERROR: implementation did not complete"
    exit 1
}

open_run impl_1
set report_dir [file join $project_dir linux_reports]
file mkdir $report_dir
report_timing_summary -file [file join $report_dir timing_summary.rpt]
report_route_status -file [file join $report_dir route_status.rpt]
report_drc -file [file join $report_dir drc.rpt]
report_utilization -file [file join $report_dir utilization.rpt]

set setup_slack [get_property SLACK [get_timing_paths -max_paths 1 -setup]]
if {$setup_slack < 0.0} {
    puts "WARNING: routed setup slack is $setup_slack ns; installing artifacts anyway for hobby-board validation"
} else {
    puts "INFO: routed setup slack is $setup_slack ns"
}

set impl_dir [file join $project_dir PCIe.runs impl_1]
set bit_files [glob -nocomplain -directory $impl_dir *.bit]
if {[llength $bit_files] > 0} {
    set bin_path [file join $impl_dir PCIe_wrapper.bin]
    puts "INFO: writing SPI x4 cfgmem bin: $bin_path"
    write_cfgmem -force -format bin -interface SPIx4 -size 16 \
        -loadbit [list up 0x0 [lindex $bit_files 0]] \
        -file $bin_path
}
set bin_files [glob -nocomplain -directory $impl_dir *.bin]
set ltx_files [glob -nocomplain -directory $impl_dir *.ltx]
puts "INFO: bit files: $bit_files"
puts "INFO: bin files: $bin_files"
puts "INFO: ltx files: $ltx_files"
puts "INFO: reports: $report_dir"

if {$install_artifacts} {
    set out_dir [file join $repo_root fpga_hardware PCIe_wrapper]
    file mkdir $out_dir
    if {[llength $bit_files] > 0} {
        file copy -force [lindex $bit_files 0] [file join $out_dir PCIe_wrapper.bit]
    }
    if {[llength $bin_files] > 0} {
        file copy -force [lindex $bin_files 0] [file join $out_dir PCIe_wrapper.bin]
    }
    if {[llength $ltx_files] > 0} {
        file copy -force [lindex $ltx_files 0] [file join $out_dir PCIe_wrapper.ltx]
    }
    puts "INFO: installed generated artifacts under $out_dir"
}

close_project
