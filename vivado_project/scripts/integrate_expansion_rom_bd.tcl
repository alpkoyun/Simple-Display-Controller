# Add the source-controlled 32 KiB EFI/GOP ROM and SDC1 identity cells to the
# validated display block design. Git selects this fixed hardware contract.
#
# The procedure is idempotent and is also used to migrate an already recreated
# 64 MiB BAR2 project before the normal regeneration/patch flow.

proc integrate_simple_display_expansion_rom_bd {vivado_root rom_mem_basename} {
    set vivado_root [file normalize $vivado_root]
    set rom_rtl [file join $vivado_root rtl simple_display_axi_rom.sv]
    set identity_rtl [file join $vivado_root rtl simple_display_identity_regs.sv]
    set rom_mem [file join $vivado_root rom $rom_mem_basename]
    set rtl_sources [list $rom_rtl $identity_rtl]
    set required_sources [list $rom_rtl $identity_rtl $rom_mem]
    foreach source $required_sources {
        if {![file isfile $source]} {
            error "missing Expansion ROM source: $source"
        }
        if {[llength [get_files -quiet $source]] == 0} {
            add_files -norecurse -fileset sources_1 $source
        }
    }
    # Module-reference tops must be typed as Verilog in Vivado 2023.2. These
    # .sv files intentionally use only Verilog-2001 constructs.
    set_property file_type {Verilog} [get_files $rtl_sources]
    set_property file_type {Memory Initialization Files} [
        get_files $rom_mem
    ]
    update_compile_order -fileset sources_1

    set bd_files [get_files -quiet *.bd]
    if {[llength $bd_files] != 1} {
        error "expected one display block design, got: $bd_files"
    }
    open_bd_design [lindex $bd_files 0]

    set xdma_cells [get_bd_cells -quiet -hier -filter {VLNV =~ "*:xdma:*"}]
    set bypass_interconnects [get_bd_cells -quiet microblaze_0_axi_periph]
    if {[llength $xdma_cells] != 1 || [llength $bypass_interconnects] != 1} {
        error "missing validated XDMA/bypass interconnect"
    }
    set xdma_cell [lindex $xdma_cells 0]
    set bypass_interconnect [lindex $bypass_interconnects 0]

    # Refuse to integrate into the stale 2 MiB recreation source. Vivado may
    # serialize the translation with more leading zeroes, so compare it as an
    # integer while keeping the exact values in the transcript.
    set bypass_size [get_property CONFIG.axist_bypass_size $xdma_cell]
    set bypass_translation [
        get_property CONFIG.pciebar2axibar_axist_bypass $xdma_cell
    ]
    puts "DISPLAY_BASELINE_BYPASS_SIZE=$bypass_size"
    puts "DISPLAY_BASELINE_BYPASS_TRANSLATION=$bypass_translation"
    if {[catch {set bypass_translation_value [expr {wide($bypass_translation)}]}] ||
        $bypass_size ne "64" ||
        $bypass_translation_value != 0x3c000000} {
        error "display baseline is not the validated 64 MiB BAR2/0x3c000000 design"
    }

    if {[llength [get_bd_cells -quiet expansion_rom]] == 0} {
        create_bd_cell -type module -reference simple_display_axi_rom expansion_rom
    }
    if {[llength [get_bd_cells -quiet identity_regs]] == 0} {
        create_bd_cell -type module -reference simple_display_identity_regs identity_regs
    }

    # Keep the validated nine-output display/control interconnect intact.  The
    # earlier eleven-output topology made every 200 MHz control path pay for
    # ROM/SDC1 address decode and inserted a large S00 data FIFO.  A small
    # same-clock splitter isolates the two new read-mostly endpoints while M00
    # forwards the original BAR2 traffic unchanged.
    foreach endpoint {expansion_rom/S_AXI identity_regs/S_AXI} {
        set endpoint_net [get_bd_intf_nets -quiet -of_objects [
            get_bd_intf_pins $endpoint
        ]]
        if {[llength $endpoint_net] != 0} {
            disconnect_bd_intf_net $endpoint_net [get_bd_intf_pins $endpoint]
        }
    }
    set_property CONFIG.NUM_MI {9} $bypass_interconnect

    set splitter_name expansion_rom_interconnect
    if {[llength [get_bd_cells -quiet $splitter_name]] == 0} {
        create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 \
            $splitter_name
    }
    set splitter [get_bd_cells $splitter_name]
    set_property -dict [list \
        CONFIG.NUM_SI {1} \
        CONFIG.NUM_MI {3} \
        CONFIG.STRATEGY {0} \
    ] $splitter

    set bypass_input [get_bd_intf_pins $bypass_interconnect/S00_AXI]
    set bypass_input_net [get_bd_intf_nets -quiet -of_objects $bypass_input]
    if {[llength $bypass_input_net] != 0} {
        disconnect_bd_intf_net $bypass_input_net $bypass_input
    }
    if {[llength [get_bd_intf_nets -quiet -of_objects [
        get_bd_intf_pins $splitter_name/S00_AXI
    ]]] == 0} {
        connect_bd_intf_net \
            [get_bd_intf_pins $xdma_cell/M_AXI_BYPASS] \
            [get_bd_intf_pins $splitter_name/S00_AXI]
    }
    connect_bd_intf_net \
        [get_bd_intf_pins $splitter_name/M00_AXI] \
        $bypass_input
    connect_bd_intf_net \
        [get_bd_intf_pins $splitter_name/M01_AXI] \
        [get_bd_intf_pins expansion_rom/S_AXI]
    connect_bd_intf_net \
        [get_bd_intf_pins $splitter_name/M02_AXI] \
        [get_bd_intf_pins identity_regs/S_AXI]

    set clock_pins {
        expansion_rom_interconnect/ACLK
        expansion_rom_interconnect/S00_ACLK
        expansion_rom_interconnect/M00_ACLK
        expansion_rom_interconnect/M01_ACLK
        expansion_rom_interconnect/M02_ACLK
        expansion_rom/s_axi_aclk
        identity_regs/s_axi_aclk
    }
    set reset_pins {
        expansion_rom_interconnect/ARESETN
        expansion_rom_interconnect/S00_ARESETN
        expansion_rom_interconnect/M00_ARESETN
        expansion_rom_interconnect/M01_ARESETN
        expansion_rom_interconnect/M02_ARESETN
        expansion_rom/s_axi_aresetn
        identity_regs/s_axi_aresetn
    }
    foreach pin $clock_pins {
        if {[llength [get_bd_nets -quiet -of_objects [get_bd_pins $pin]]] == 0} {
            connect_bd_net [get_bd_pins $xdma_cell/axi_aclk] [get_bd_pins $pin]
        }
    }
    foreach pin $reset_pins {
        if {[llength [get_bd_nets -quiet -of_objects [get_bd_pins $pin]]] == 0} {
            connect_bd_net [get_bd_pins $xdma_cell/axi_aresetn] [get_bd_pins $pin]
        }
    }

    assign_bd_address -offset 0x01000000 -range 0x00008000 \
        -target_address_space [get_bd_addr_spaces $xdma_cell/M_AXI_BYPASS] \
        [get_bd_addr_segs expansion_rom/S_AXI/reg0] -force
    assign_bd_address -offset 0x3C080000 -range 0x00010000 \
        -target_address_space [get_bd_addr_spaces $xdma_cell/M_AXI_BYPASS] \
        [get_bd_addr_segs identity_regs/S_AXI/reg0] -force

    validate_bd_design
    save_bd_design
    update_compile_order -fileset sources_1
    puts "SIMPLE_DISPLAY_EXPANSION_ROM_BD_INTEGRATION_PASS"
}
