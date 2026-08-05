# Verify an already completed implementation without resetting or relaunching
# synthesis/place/route. This keeps an accepted routed checkpoint unchanged.

set project_file ""
set repo_root [file normalize [file join [file dirname [info script]] .. ..]]
set rom_size_kib 32
set minimum_setup_wns_ns -2.500

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

set rom_bin [file join $repo_root vivado_project rom simple_display_gop_option_rom.bin]
set rom_mem [file join $repo_root vivado_project rom simple_display_gop_option_rom_32.mem]

open_project $project_file
set project_dir [get_property DIRECTORY [current_project]]
set project_name [file rootname [file tail $project_file]]
set impl_run [get_runs -quiet impl_1]
if {[llength $impl_run] != 1 ||
    [get_property PROGRESS $impl_run] ne "100%"} {
    puts "ERROR: impl_1 is not a completed implementation"
    exit 1
}

open_run impl_1
set report_dir [file join $project_dir linux_reports]
file mkdir $report_dir
report_timing_summary -file [file join $report_dir timing_summary.rpt]
report_route_status -file [file join $report_dir route_status.rpt]
report_drc -file [file join $report_dir drc.rpt]
report_utilization -file [file join $report_dir utilization.rpt]

set setup_paths [get_timing_paths -delay_type max -max_paths 200 -nworst 1]
set setup_path [get_timing_paths -delay_type max -max_paths 1 -nworst 1]
set hold_path [get_timing_paths -delay_type min -max_paths 1 -nworst 1]
if {[llength $setup_path] != 1 || [llength $hold_path] != 1} {
    puts "ERROR: could not resolve routed setup/hold paths"
    exit 1
}
set setup_slack [get_property SLACK $setup_path]
set hold_slack [get_property SLACK $hold_path]
set setup_startpoint [get_property STARTPOINT_PIN $setup_path]
set setup_endpoint [get_property ENDPOINT_PIN $setup_path]
set setup_path_text [string tolower "$setup_startpoint $setup_endpoint"]
set setup_path_is_ila [expr {[string first "ila" $setup_path_text] >= 0}]

set functional_setup_slack ""
set functional_setup_startpoint ""
set functional_setup_endpoint ""
foreach candidate_path $setup_paths {
    set candidate_startpoint [get_property STARTPOINT_PIN $candidate_path]
    set candidate_endpoint [get_property ENDPOINT_PIN $candidate_path]
    set candidate_text [string tolower "$candidate_startpoint $candidate_endpoint"]
    if {[string first "ila" $candidate_text] < 0} {
        set functional_setup_slack [get_property SLACK $candidate_path]
        set functional_setup_startpoint $candidate_startpoint
        set functional_setup_endpoint $candidate_endpoint
        break
    }
}
if {$functional_setup_slack eq ""} {
    puts "ERROR: could not find a non-ILA setup path in the 200 worst paths"
    exit 1
}

set drc_errors [get_drc_violations -quiet -filter {SEVERITY == Error}]
set unrouted_nets [get_nets -quiet -hier -filter {ROUTE_STATUS == UNROUTED}]
set partial_nets [get_nets -quiet -hier -filter {ROUTE_STATUS == PARTIALLY_ROUTED}]

array set expected_gt {
    0 GTPE2_CHANNEL_X0Y5
    1 GTPE2_CHANNEL_X0Y4
    2 GTPE2_CHANNEL_X0Y6
    3 GTPE2_CHANNEL_X0Y7
}
array set observed_lane {}
foreach cell [get_cells -quiet -hier -filter {REF_NAME == GTPE2_CHANNEL}] {
    if {[regexp {pipe_lane\[([0-3])\]} $cell unused lane]} {
        set observed_lane($lane) [get_property LOC $cell]
    }
}
set placement_error 0
foreach lane {0 1 2 3} {
    if {![info exists observed_lane($lane)] ||
        $observed_lane($lane) ne $expected_gt($lane)} {
        set placement_error 1
    }
}

set common_cells [get_cells -quiet -hier -filter {REF_NAME == GTPE2_COMMON}]
set pcie_cells [get_cells -quiet -hier -filter {REF_NAME == PCIE_2_1}]
set refclk_cells [get_cells -quiet -hier -filter {REF_NAME == IBUFDS_GTE2}]
set reset_port [get_ports -quiet sys_rst_n]
set routed_rom_cells [get_cells -quiet -hier -filter {
    NAME =~ "*expansion_rom*" ||
    REF_NAME == simple_display_axi_rom ||
    ORIG_REF_NAME == simple_display_axi_rom
}]
set routed_identity_cells [get_cells -quiet -hier -filter {
    NAME =~ "*identity_regs*" ||
    REF_NAME == simple_display_identity_regs ||
    ORIG_REF_NAME == simple_display_identity_regs
}]

set impl_dir [file join $project_dir ${project_name}.runs impl_1]
set bit_files [glob -nocomplain -directory $impl_dir *.bit]
set bin_files [glob -nocomplain -directory $impl_dir *.bin]
set ltx_files [glob -nocomplain -directory $impl_dir *.ltx]

set contract_file [file join $report_dir implementation_contract.txt]
set contract_fp [open $contract_file w]
puts $contract_fp "ROM_CONFIGURATION=FIXED_EFI_GOP_32K"
puts $contract_fp "ROM_SIZE_KIB=$rom_size_kib"
puts $contract_fp "ROUTED_SETUP_WNS_NS=$setup_slack"
puts $contract_fp "MINIMUM_ACCEPTED_SETUP_WNS_NS=$minimum_setup_wns_ns"
puts $contract_fp "SETUP_TIMING_POLICY=PROJECT_THRESHOLD_SETUP_AND_NONNEGATIVE_HOLD"
puts $contract_fp "SETUP_PATH_IS_ILA=$setup_path_is_ila"
puts $contract_fp "SETUP_STARTPOINT=$setup_startpoint"
puts $contract_fp "SETUP_ENDPOINT=$setup_endpoint"
puts $contract_fp "FUNCTIONAL_SETUP_WNS_NS=$functional_setup_slack"
puts $contract_fp "FUNCTIONAL_SETUP_STARTPOINT=$functional_setup_startpoint"
puts $contract_fp "FUNCTIONAL_SETUP_ENDPOINT=$functional_setup_endpoint"
puts $contract_fp "ROUTED_HOLD_WHS_NS=$hold_slack"
puts $contract_fp "DRC_ERROR_COUNT=[llength $drc_errors]"
puts $contract_fp "UNROUTED_NET_COUNT=[llength $unrouted_nets]"
puts $contract_fp "PARTIALLY_ROUTED_NET_COUNT=[llength $partial_nets]"
puts $contract_fp "ROM_ENDPOINT_NETLIST_CELL_COUNT=[llength $routed_rom_cells]"
puts $contract_fp "SDC1_IDENTITY_NETLIST_CELL_COUNT=[llength $routed_identity_cells]"
foreach lane {0 1 2 3} {
    if {[info exists observed_lane($lane)]} {
        puts $contract_fp "pipe_lane\[$lane\]=$observed_lane($lane)"
    }
}
close $contract_fp

if {$setup_slack < $minimum_setup_wns_ns} {
    puts "ERROR: routed setup WNS is below the project threshold: setup=$setup_slack threshold=$minimum_setup_wns_ns"
    exit 1
}
if {$hold_slack < 0.0} {
    puts "ERROR: routed hold timing did not close: hold=$hold_slack"
    exit 1
}
if {[llength $drc_errors] != 0 ||
    [llength $unrouted_nets] != 0 ||
    [llength $partial_nets] != 0} {
    puts "ERROR: routed DRC/net-status contract failed"
    exit 1
}
if {$placement_error ||
    [llength $common_cells] != 1 ||
    [get_property LOC [lindex $common_cells 0]] ne "GTPE2_COMMON_X0Y1" ||
    [llength $pcie_cells] != 1 ||
    [get_property LOC [lindex $pcie_cells 0]] ne "PCIE_X0Y0" ||
    [llength $refclk_cells] != 1 ||
    [get_property LOC [lindex $refclk_cells 0]] ne "IBUFDS_GTE2_X0Y3" ||
    [llength $reset_port] != 1 ||
    [get_property PACKAGE_PIN $reset_port] ne "J20"} {
    puts "ERROR: implemented AX7203 placement contract failed"
    exit 1
}
if {[llength $routed_rom_cells] == 0} {
    puts "ERROR: routed design does not retain the Expansion ROM endpoint"
    exit 1
}
if {[llength $routed_identity_cells] == 0} {
    puts "ERROR: routed design does not retain SDC1 identity logic"
    exit 1
}
if {[llength $bit_files] != 1 || [llength $bin_files] == 0 ||
    [llength $ltx_files] == 0} {
    puts "ERROR: expected one bit and at least one bin/ltx artifact"
    exit 1
}

set hash_file [file join $report_dir artifact_hashes.txt]
set hash_fp [open $hash_file w]
set hash_artifacts [concat $bit_files $bin_files $ltx_files [list $rom_bin $rom_mem]]
foreach artifact $hash_artifacts {
    if {![file isfile $artifact]} {
        puts "ERROR: missing artifact selected for hashing: $artifact"
        close $hash_fp
        exit 1
    }
    puts $hash_fp [exec sha256sum $artifact]
}
close $hash_fp

puts "INFO: implementation contract: $contract_file"
puts "INFO: artifact hashes: $hash_file"
puts "SIMPLE_DISPLAY_IMPLEMENTATION_CONTRACT_PASS"
close_project
