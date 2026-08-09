# Generate IP products, enforce the AX7203 PCIe GT lane order, and optionally
# build bit/bin artifacts.

set project_file ""
set repo_root [file normalize [file join [file dirname [info script]] .. ..]]
set jobs 2
set do_build 0
set install_artifacts 0
set rom_size_kib 32
set rom_mem_basename simple_display_gop_option_rom_32.mem
set rom_bin_relative [file join rom simple_display_gop_option_rom.bin]
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
set project_name [file rootname [file tail $project_file]]
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

# The recursive-file helper is shared by the clean-baseline gate and fixed ROM.
set config_tcl [file join $vivado_root scripts configure_xdma_expansion_rom.tcl]
source $config_tcl

set baseline_xdma_cells [get_bd_cells -quiet -hier -filter {VLNV =~ "*:xdma:*"}]
if {[llength $baseline_xdma_cells] != 1} {
    puts "ERROR: expected one XDMA cell in the display design"
    exit 1
}
set baseline_xdma [lindex $baseline_xdma_cells 0]
set baseline_rom_enabled [
    get_property CONFIG.pf0_expansion_rom_enabled $baseline_xdma
]
set baseline_rom_cells [get_bd_cells -quiet expansion_rom]
set baseline_identity_cells [get_bd_cells -quiet identity_regs]
set project_is_clean_baseline [expr {
    $baseline_rom_enabled eq "false" &&
    [llength $baseline_rom_cells] == 0 &&
    [llength $baseline_identity_cells] == 0
}]
if {!$project_is_clean_baseline} {
    if {$baseline_rom_enabled ne "true" ||
        [llength $baseline_rom_cells] != 1 ||
        [llength $baseline_identity_cells] != 1} {
        puts "ERROR: existing project is not the fixed 32 KiB EFI/GOP topology"
        exit 1
    }
    puts "INFO: reusing the already integrated fixed EFI/GOP project"
}

set hardware_contract [
    file join $repo_root scripts check_fpga_hardware_contract.py
]
if {$project_is_clean_baseline} {
    # Generate the untouched uploaded 64 MiB design first. This is a hard gate
    # before any Expansion-ROM cell or XDMA property is introduced.
    puts "INFO: generating the uploaded 64 MiB BAR/display baseline"
    generate_target all $bd_files
    export_ip_user_files -of_objects $bd_files -no_script -sync -force -quiet
    update_compile_order -fileset sources_1

    set baseline_hwh_candidates {}
    foreach candidate [sdc_recursive_files $project_dir "PCIe.hwh"] {
        if {[string first "[file tail $project_dir].gen" $candidate] >= 0 &&
            [string first "/bd/PCIe/hw_handoff/PCIe.hwh" $candidate] >= 0} {
            lappend baseline_hwh_candidates $candidate
        }
    }
    if {[llength $baseline_hwh_candidates] != 1} {
        puts "ERROR: expected one generated baseline PCIe.hwh, got: $baseline_hwh_candidates"
        exit 1
    }
    set baseline_hwh [lindex $baseline_hwh_candidates 0]
    set check_code [catch {
        exec python3 $hardware_contract \
            --hwh $baseline_hwh \
            --require-ddr-bypass \
            --clean-baseline
    } check_output]
    puts $check_output
    if {$check_code != 0} {
        puts "ERROR: recreated project is not the uploaded 64 MiB display baseline"
        exit 1
    }
    puts "SIMPLE_DISPLAY_BYPASS64_BASELINE_CONTRACT_PASS"
} else {
    puts "INFO: clean baseline gate was completed before this project's initial ROM integration"
}

set rom_mem [file join $vivado_root rom $rom_mem_basename]
set rom_bin [file join $vivado_root $rom_bin_relative]
if {![file isfile $rom_mem] || ![file isfile $rom_bin]} {
    puts "ERROR: missing fixed EFI/GOP ROM inputs: $rom_mem / $rom_bin"
    exit 1
}

set integration_tcl [
    file join $vivado_root scripts integrate_expansion_rom_bd.tcl
]
source $integration_tcl
integrate_simple_display_expansion_rom_bd $vivado_root $rom_mem_basename

puts "INFO: regenerating both XDMA layers for the fixed 32 KiB EFI/GOP ROM"
configure_simple_display_xdma_rom $project_file $rom_mem

# The ROM configuration procedure returns with the display project reopened.
set project_dir [get_property DIRECTORY [current_project]]
set srcset [get_filesets sources_1]
set bd_files [get_files -quiet *.bd]
update_compile_order -fileset sources_1

set decoder_candidates {}
foreach candidate [sdc_recursive_files $project_dir "*tgt_req.sv"] {
    if {[string first "[file tail $project_dir].gen" $candidate] >= 0 &&
        [string first "/bd/PCIe/ip/" $candidate] >= 0} {
        lappend decoder_candidates $candidate
    }
}
if {[llength $decoder_candidates] != 1} {
    puts "ERROR: expected one active generated XDMA target decoder, got: $decoder_candidates"
    exit 1
}
set decoder [lindex $decoder_candidates 0]
set patcher [file join $vivado_root scripts patch_xdma_tgt_req.py]
set check_code [catch {
    exec python3 $patcher --target $decoder --apply
} check_output]
puts $check_output
if {$check_code != 0} {
    puts "ERROR: fail-closed XDMA BAR6 decoder patch failed"
    exit 1
}

set rom_contract [
    file join $vivado_root scripts check_expansion_rom_contract.py
]
set check_code [catch {
    exec python3 $rom_contract --project-dir $project_dir
} check_output]
puts $check_output
if {$check_code != 0} {
    puts "ERROR: generated Expansion ROM contract failed"
    exit 1
}

set top_hwh_candidates {}
foreach candidate [sdc_recursive_files $project_dir "PCIe.hwh"] {
    if {[string first "[file tail $project_dir].gen" $candidate] >= 0 &&
        [string first "/bd/PCIe/hw_handoff/PCIe.hwh" $candidate] >= 0} {
        lappend top_hwh_candidates $candidate
    }
}
if {[llength $top_hwh_candidates] != 1} {
    puts "ERROR: expected one generated top-level PCIe.hwh, got: $top_hwh_candidates"
    exit 1
}
set top_hwh [lindex $top_hwh_candidates 0]
set check_code [catch {
    exec python3 $hardware_contract \
        --hwh $top_hwh \
        --require-ddr-bypass
} check_output]
puts $check_output
if {$check_code != 0} {
    puts "ERROR: generated fixed EFI/GOP hardware contract failed"
    exit 1
}

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

# The generated decoder repair is not part of the XCI cache key. Disable IP
# cache reuse and force a new XDMA OOC checkpoint from the repaired source.
config_ip_cache -disable_cache
set_property ip_cache_permissions disable [current_project]
set_property strategy Flow_PerfOptimized_high [get_runs synth_1]
set_property strategy Performance_ExplorePostRoutePhysOpt [get_runs impl_1]
set_property STEPS.WRITE_BITSTREAM.ARGS.BIN_FILE true [get_runs impl_1]
# The uploaded Windows project records an incremental synthesis checkpoint.
# Keep it as a source-restoration input, but do not reuse it for either proof
# build: both the no-ROM baseline and ROM-capable image must be synthesized
# from the recreated source state.
set_property incremental_checkpoint {} [get_runs synth_1]
set_property auto_incremental_checkpoint 0 [get_runs synth_1]

set outer_xdma_xci_candidates {}
foreach candidate [sdc_recursive_files $project_dir "*.xci"] {
    if {[string first "[file tail $project_dir].srcs" $candidate] < 0 ||
        [string first "/bd/PCIe/ip/" $candidate] < 0} {
        continue
    }
    set candidate_fp [open $candidate r]
    set candidate_text [read $candidate_fp]
    close $candidate_fp
    if {[string first \
        {"component_reference": "xilinx.com:ip:xdma:4.1"} \
        $candidate_text] >= 0} {
        lappend outer_xdma_xci_candidates $candidate
    }
}
if {[llength $outer_xdma_xci_candidates] != 1} {
    puts "ERROR: expected one outer XDMA XCI for OOC synthesis, got: $outer_xdma_xci_candidates"
    exit 1
}
set outer_xdma_xci [lindex $outer_xdma_xci_candidates 0]
set outer_xdma_file_objects [get_files -quiet $outer_xdma_xci]
if {[llength $outer_xdma_file_objects] != 1} {
    puts "ERROR: outer XDMA XCI is not managed by the project: $outer_xdma_xci"
    exit 1
}

set xdma_ooc_run_name \
    "[file rootname [file tail $outer_xdma_xci]]_synth_1"
set xdma_ooc_runs [get_runs -quiet $xdma_ooc_run_name]
if {[llength $xdma_ooc_runs] == 0} {
    puts "INFO: creating the clean project's dedicated XDMA OOC run"
    create_ip_run [lindex $outer_xdma_file_objects 0]
    set xdma_ooc_runs [get_runs -quiet $xdma_ooc_run_name]
}
if {[llength $xdma_ooc_runs] != 1} {
    puts "ERROR: expected exact XDMA OOC synthesis run $xdma_ooc_run_name, got: $xdma_ooc_runs"
    exit 1
}
set xdma_ooc_run [lindex $xdma_ooc_runs 0]
puts "INFO: forcing XDMA OOC synthesis from repaired generated source"
reset_run $xdma_ooc_run
launch_runs $xdma_ooc_run -jobs $jobs
wait_on_run $xdma_ooc_run
set xdma_ooc_status [get_property STATUS $xdma_ooc_run]
puts "XDMA_OOC_STATUS=$xdma_ooc_status"
if {![string match "*Complete*" $xdma_ooc_status]} {
    puts "ERROR: XDMA OOC synthesis did not complete"
    exit 1
}
set check_code [catch {
    exec python3 $patcher --target $decoder --check
} check_output]
puts $check_output
if {$check_code != 0} {
    puts "ERROR: XDMA OOC generation did not retain the repaired BAR6 decoder"
    exit 1
}

set dcp_candidates [
    sdc_recursive_files [get_property DIRECTORY $xdma_ooc_run] "*.dcp"
]
if {[llength $dcp_candidates] != 1} {
    puts "ERROR: expected one rebuilt XDMA OOC DCP in the run directory, got: $dcp_candidates"
    exit 1
}
set xdma_ooc_dcp [lindex $dcp_candidates 0]
if {[file mtime $xdma_ooc_dcp] < [file mtime $decoder]} {
    puts "ERROR: XDMA OOC DCP predates the repaired decoder"
    exit 1
}
set report_dir [file join $project_dir linux_reports]
file mkdir $report_dir
set provenance_file [file join $report_dir xdma_ooc_provenance.txt]
set provenance_fp [open $provenance_file w]
puts $provenance_fp "ROM_CONFIGURATION=FIXED_EFI_GOP_32K"
puts $provenance_fp "ROM_SIZE_KIB=$rom_size_kib"
puts $provenance_fp "DECODER=$decoder"
puts $provenance_fp "DECODER_MTIME=[file mtime $decoder]"
puts $provenance_fp "DECODER_SHA256=[lindex [exec sha256sum $decoder] 0]"
puts $provenance_fp "XDMA_OOC_DCP=$xdma_ooc_dcp"
puts $provenance_fp "XDMA_OOC_DCP_MTIME=[file mtime $xdma_ooc_dcp]"
puts $provenance_fp "XDMA_OOC_DCP_SHA256=[lindex [exec sha256sum $xdma_ooc_dcp] 0]"
close $provenance_fp

puts "INFO: launching synthesis with $jobs jobs"
reset_run synth_1
launch_runs synth_1 -jobs $jobs
wait_on_run synth_1
set synth_status [get_property STATUS [get_runs synth_1]]
if {![string match "*Complete*" $synth_status]} {
    puts "ERROR: synthesis did not complete"
    exit 1
}

set rom_ooc_runs [
    get_runs -quiet -filter {NAME =~ "*expansion_rom*_synth_1"}
]
if {[llength $rom_ooc_runs] != 1} {
    puts "ERROR: expected one Expansion ROM OOC synthesis run, got: $rom_ooc_runs"
    exit 1
}
set rom_ooc_run [lindex $rom_ooc_runs 0]
set rom_ooc_dir [get_property DIRECTORY $rom_ooc_run]
set rom_ooc_dcps [sdc_recursive_files $rom_ooc_dir "*.dcp"]
set rom_ooc_log [file join $rom_ooc_dir runme.log]
if {[llength $rom_ooc_dcps] != 1 || ![file isfile $rom_ooc_log]} {
    puts "ERROR: missing Expansion ROM OOC checkpoint/log"
    exit 1
}
set rom_ooc_dcp [lindex $rom_ooc_dcps 0]
set rom_log_fp [open $rom_ooc_log r]
set rom_log_text [read $rom_log_fp]
close $rom_log_fp
set expected_rom_words [expr {$rom_size_kib * 256}]
set rom_inference_pattern [
    format {rom_storage[[:space:]]*\|[[:space:]]*%dx32} $expected_rom_words
]
if {[string first \
        "Parameter ROM_INIT_FILE bound to: $rom_mem_basename" \
        $rom_log_text] < 0 ||
    ![regexp $rom_inference_pattern $rom_log_text] ||
    [string first "Synthesis finished with 0 errors, 0 critical warnings" \
        $rom_log_text] < 0 ||
    [file mtime $rom_ooc_dcp] < [file mtime $rom_mem]} {
    puts "ERROR: Expansion ROM OOC log/checkpoint does not prove the selected initialization"
    exit 1
}

puts "INFO: re-checking lane order immediately before implementation"
set check_code [catch {
    exec python3 $checker $project_dir --expected 5,4,6,7
} check_output]
puts $check_output
if {$check_code != 0} {
    puts "ERROR: PCIe GT lane-order check failed after synthesis"
    exit 1
}

open_run synth_1
set rom_endpoint_cells [get_cells -quiet PCIe_i/expansion_rom]
set rom_primitive_cells [get_cells -quiet -hier -filter {
    NAME =~ "PCIe_i/expansion_rom/*" && IS_PRIMITIVE
}]
set rom_lut_cells [get_cells -quiet -hier -filter {
    NAME =~ "PCIe_i/expansion_rom/*" && REF_NAME =~ "LUT*"
}]
set identity_endpoint_cells [get_cells -quiet PCIe_i/identity_regs]
set identity_primitive_cells [get_cells -quiet -hier -filter {
    NAME =~ "PCIe_i/identity_regs/*" && IS_PRIMITIVE
}]
if {[llength $rom_endpoint_cells] != 1 ||
    [llength $rom_primitive_cells] == 0 ||
    [llength $rom_lut_cells] == 0} {
    puts "ERROR: synthesized design did not retain the initialized ROM endpoint/logic"
    exit 1
}
if {[llength $identity_endpoint_cells] != 1 ||
    [llength $identity_primitive_cells] == 0} {
    puts "ERROR: synthesized design did not retain SDC1 identity logic"
    exit 1
}
close_design

puts "INFO: launching implementation through write_bitstream with $jobs jobs"
reset_run impl_1
launch_runs impl_1 -to_step write_bitstream -jobs $jobs
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
    puts "ERROR: implementation did not complete"
    exit 1
}

open_run impl_1
report_timing_summary -file [file join $report_dir timing_summary.rpt]
report_route_status -file [file join $report_dir route_status.rpt]
report_drc -file [file join $report_dir drc.rpt]
report_utilization -file [file join $report_dir utilization.rpt]

set setup_paths [get_timing_paths -delay_type max -max_paths 200 -nworst 1]
set setup_path [get_timing_paths -delay_type max -max_paths 1 -nworst 1]
set hold_path [get_timing_paths -delay_type min -max_paths 1 -nworst 1]
set setup_slack [get_property SLACK $setup_path]
set hold_slack [get_property SLACK [lindex $hold_path 0]]
set setup_startpoint [get_property STARTPOINT_PIN $setup_path]
set setup_endpoint [get_property ENDPOINT_PIN $setup_path]
set setup_path_text [string tolower "$setup_startpoint $setup_endpoint"]
set setup_path_is_ila [expr {
    [string first "ila" $setup_path_text] >= 0
}]
set functional_setup_slack ""
set functional_setup_startpoint ""
set functional_setup_endpoint ""
foreach candidate_path $setup_paths {
    set candidate_startpoint [get_property STARTPOINT_PIN $candidate_path]
    set candidate_endpoint [get_property ENDPOINT_PIN $candidate_path]
    set candidate_text [
        string tolower "$candidate_startpoint $candidate_endpoint"
    ]
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

set placement_error 0
array set expected_gt {
    0 GTPE2_CHANNEL_X0Y5
    1 GTPE2_CHANNEL_X0Y4
    2 GTPE2_CHANNEL_X0Y6
    3 GTPE2_CHANNEL_X0Y7
}
array set observed_lane {}
set gt_cells [get_cells -quiet -hier -filter {REF_NAME == GTPE2_CHANNEL}]
foreach cell $gt_cells {
    if {[regexp {pipe_lane\[([0-3])\]} $cell unused lane]} {
        set observed_lane($lane) [get_property LOC $cell]
    }
}
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
puts $contract_fp "ROM_ENDPOINT_NETLIST_CELL_COUNT=[llength $routed_rom_cells]"
puts $contract_fp "SDC1_IDENTITY_NETLIST_CELL_COUNT=[llength $routed_identity_cells]"
foreach lane {0 1 2 3} {
    if {[info exists observed_lane($lane)]} {
        puts $contract_fp "pipe_lane\[$lane\]=$observed_lane($lane)"
    }
}
foreach cell $common_cells {
    puts $contract_fp "GTPE2_COMMON=[get_property LOC $cell]"
}
foreach cell $pcie_cells {
    puts $contract_fp "PCIE=[get_property LOC $cell]"
}
foreach cell $refclk_cells {
    puts $contract_fp "REFCLK=[get_property LOC $cell]"
}
if {[llength $reset_port] == 1} {
    puts $contract_fp "PERST_PACKAGE_PIN=[get_property PACKAGE_PIN $reset_port]"
    puts $contract_fp "PERST_IOSTANDARD=[get_property IOSTANDARD $reset_port]"
    puts $contract_fp "PERST_PULLTYPE=[get_property PULLTYPE $reset_port]"
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
if {[llength $drc_errors] != 0} {
    puts "ERROR: implementation has [llength $drc_errors] DRC errors"
    exit 1
}
if {[llength $unrouted_nets] != 0} {
    puts "ERROR: implementation has [llength $unrouted_nets] unrouted nets"
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
    [get_property PACKAGE_PIN $reset_port] ne "J20" ||
    [get_property IOSTANDARD $reset_port] ne "LVCMOS33" ||
    [get_property PULLTYPE $reset_port] ne "PULLUP"} {
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

set impl_dir [file join $project_dir ${project_name}.runs impl_1]
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

set hash_file [file join $report_dir artifact_hashes.txt]
set hash_fp [open $hash_file w]
set hash_artifacts [concat $bit_files $bin_files $ltx_files [list $rom_bin $rom_mem]]
foreach artifact $hash_artifacts {
    if {[file isfile $artifact]} {
        puts $hash_fp [exec sha256sum $artifact]
    }
}
close $hash_fp

if {$install_artifacts} {
    set out_dir [file join $repo_root fpga_hardware PCIe_wrapper]
    set installed_report_dir [file join $out_dir expansion_rom_reports]
    file mkdir $out_dir
    file mkdir $installed_report_dir
    if {[llength $bit_files] > 0} {
        file copy -force [lindex $bit_files 0] [file join $out_dir PCIe_wrapper.bit]
    }
    if {[llength $bin_files] > 0} {
        file copy -force [lindex $bin_files 0] [file join $out_dir PCIe_wrapper.bin]
    }
    if {[llength $ltx_files] > 0} {
        file copy -force [lindex $ltx_files 0] [file join $out_dir PCIe_wrapper.ltx]
    }
    file copy -force $rom_bin [file join $out_dir [file tail $rom_bin]]
    file copy -force $rom_mem [file join $out_dir [file tail $rom_mem]]
    file copy -force $hash_file [file join $out_dir expansion_rom_artifact_hashes.txt]
    foreach report [glob -nocomplain -directory $report_dir *] {
        if {[file isfile $report]} {
            file copy -force $report \
                [file join $installed_report_dir [file tail $report]]
        }
    }
    puts "INFO: installed generated artifacts under $out_dir"
}

puts "SIMPLE_DISPLAY_XDMA_EFI_GOP_ROM_IMPLEMENTATION_PASS"
close_project
