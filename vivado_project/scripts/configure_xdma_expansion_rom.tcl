# Reproducibly configure both XDMA 4.1 Expansion ROM layers for the fixed
# 32 KiB EFI/GOP Option-ROM checkout.
#
# Source this file from a Vivado process with the display project open, then
# call:
#   configure_simple_display_xdma_rom \
#       /path/to/PCIe.xpr /path/to/simple_display_gop_option_rom_32.mem
#
# The procedure returns with the display project open. It intentionally
# regenerates the outer XDMA first, then configures/regenerates the nested
# pcie_7x child that XDMA 4.1 does not update automatically.

proc sdc_recursive_files {directory pattern} {
    set result {}
    foreach path [glob -nocomplain -directory $directory *] {
        if {[file isdirectory $path]} {
            set result [concat $result [sdc_recursive_files $path $pattern]]
        } elseif {[string match $pattern [file tail $path]]} {
            lappend result [file normalize $path]
        }
    }
    return $result
}

proc configure_simple_display_xdma_rom {project_file mem_file} {
    set size_kib 32
    if {![file isfile $project_file]} {
        error "missing project: $project_file"
    }

    set project_file [file normalize $project_file]
    set project_dir [file dirname $project_file]
    set project_name [file rootname [file tail $project_file]]
    set mem_file [file normalize $mem_file]
    if {![file isfile $mem_file]} {
        error "missing Expansion ROM initialization file: $mem_file"
    }
    set mem_basename [file tail $mem_file]

    if {[llength [get_projects -quiet]] == 0} {
        open_project $project_file
    }
    if {[llength [get_files -quiet $mem_file]] == 0} {
        add_files -norecurse -fileset sources_1 $mem_file
    }
    set_property file_type {Memory Initialization Files} [get_files $mem_file]

    set xdma_cells [get_bd_cells -quiet -hier -filter {VLNV =~ "*:xdma:*"}]
    if {[llength $xdma_cells] != 1} {
        error "expected one XDMA block-design cell, got: $xdma_cells"
    }
    set xdma_cell [lindex $xdma_cells 0]
    set_property -dict [list \
        CONFIG.pf0_expansion_rom_enabled {true} \
        CONFIG.pf0_expansion_rom_type {Bypass_AXI_Master} \
        CONFIG.pf0_expansion_rom_size $size_kib \
        CONFIG.pf0_expansion_rom_scale {Kilobytes} \
        CONFIG.pciebar2axibar_6 {0x0000000001000000} \
    ] $xdma_cell

    set rom_cells [get_bd_cells -quiet expansion_rom]
    set identity_cells [get_bd_cells -quiet identity_regs]
    if {[llength $rom_cells] != 1} {
        error "missing Expansion ROM module-reference cell"
    }
    if {[llength $identity_cells] != 1} {
        error "fixed EFI/GOP configuration is missing the SDC1 identity cell"
    }
    set_property CONFIG.ROM_BYTES [expr {$size_kib * 1024}] $rom_cells
    set_property CONFIG.ROM_INIT_FILE $mem_basename $rom_cells
    set_property CONFIG.ROM_APERTURE_BYTES \
        [expr {$size_kib * 1024}] $identity_cells
    set rom_range [format "0x%08X" [expr {$size_kib * 1024}]]
    assign_bd_address -offset 0x01000000 -range $rom_range \
        -target_address_space [get_bd_addr_spaces $xdma_cell/M_AXI_BYPASS] \
        [get_bd_addr_segs expansion_rom/S_AXI/reg0] -force

    validate_bd_design
    save_bd_design
    set bd_files [get_files -quiet *.bd]
    generate_target all $bd_files
    export_ip_user_files -of_objects $bd_files -no_script -sync -force -quiet
    update_compile_order -fileset sources_1

    # BD-owned IP is not returned by get_ips in every project context.
    # Generated-XCI validation below is therefore the authoritative outer-IP
    # check; log the configured BD cell here for the transcript.
    puts "OUTER_XDMA_BD_CELL=$xdma_cell"
    foreach property_name {
        CONFIG.pf0_expansion_rom_enabled
        CONFIG.pf0_expansion_rom_type
        CONFIG.pf0_expansion_rom_size
        CONFIG.pf0_expansion_rom_scale
        CONFIG.pciebar2axibar_6
    } {
        puts "$property_name=[get_property $property_name $xdma_cell]"
    }

    set child_candidates [
        sdc_recursive_files $project_dir "*pcie2_ip.xci"
    ]
    set active_children {}
    foreach candidate $child_candidates {
        if {[string first "${project_name}.gen" $candidate] >= 0 &&
            [string first "/bd/PCIe/ip/" $candidate] >= 0} {
            lappend active_children $candidate
        }
    }
    if {[llength $active_children] != 1} {
        error "expected one active nested pcie_7x XCI, got: $active_children"
    }
    set child_xci [lindex $active_children 0]

    close_project
    create_project -in_memory -part xc7a200tfbg484-2
    read_ip $child_xci
    set child_ips [get_ips -quiet]
    if {[llength $child_ips] != 1} {
        error "expected one nested pcie_7x IP, got: $child_ips"
    }
    set child_ip [lindex $child_ips 0]
    set_property -dict [list \
        CONFIG.Expansion_Rom_Enabled {true} \
        CONFIG.Expansion_Rom_Size $size_kib \
        CONFIG.Expansion_Rom_Scale {Kilobytes} \
    ] $child_ip
    generate_target all $child_ip
    puts "NESTED_PCIE7X_XCI=$child_xci"
    foreach property_name {
        CONFIG.Expansion_Rom_Enabled
        CONFIG.Expansion_Rom_Size
        CONFIG.Expansion_Rom_Scale
    } {
        puts "$property_name=[get_property $property_name $child_ip]"
    }
    close_project

    open_project $project_file
    puts "SIMPLE_DISPLAY_XDMA_ROM_CONFIGURATION_PASS"
}
