#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "${script_dir}/.." && pwd)"
vivado_bin_dir="${VIVADO_BIN_DIR:-/home/alpk/xilinx/Vivado/2023.2/bin}"
vivado_settings="${VIVADO_SETTINGS:-/home/alpk/xilinx/Vivado/2023.2/settings64.sh}"
sim_build_root="${SIM_BUILD_ROOT:-${project_dir}/.sim_work}"
mkdir -p "${sim_build_root}"
work_dir="$(mktemp -d "${sim_build_root}/expansion-rom.XXXXXX")"

cleanup() {
  if [[ "${KEEP_SIM_WORK:-0}" == "1" ]]; then
    echo "Preserved simulation work directory: ${work_dir}"
  else
    rm -rf -- "${work_dir}"
  fi
}
trap cleanup EXIT

if [[ -f "${vivado_settings}" ]]; then
  # XSim's generated kernel needs the Vivado runtime library paths. The xvlog
  # and xelab wrappers can succeed without these and then fail only at run time.
  source "${vivado_settings}"
fi
vivado_root="$(cd -- "${vivado_bin_dir}/.." && pwd)"
export LD_LIBRARY_PATH="${vivado_root}/lib/lnx64.o${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"

for tool in xvlog xelab xsim; do
  if [[ ! -x "${vivado_bin_dir}/${tool}" ]]; then
    echo "missing Vivado simulator tool: ${vivado_bin_dir}/${tool}" >&2
    exit 1
  fi
done

cp "${project_dir}/rom/simple_display_gop_option_rom_32.mem" "${work_dir}/"
cd "${work_dir}"

"${vivado_bin_dir}/xvlog" --sv \
  "${project_dir}/rtl/simple_display_axi_rom.sv" \
  "${project_dir}/rtl/simple_display_identity_regs.sv" \
  "${project_dir}/sim/tb_simple_display_axi_rom.sv" \
  "${project_dir}/sim/tb_simple_display_identity_regs.sv"
"${vivado_bin_dir}/xelab" tb_simple_display_axi_rom \
  -s tb_simple_display_axi_rom_sim \
  | tee rom_elaboration.log
"${vivado_bin_dir}/xsim" tb_simple_display_axi_rom_sim \
  -tclbatch "${project_dir}/sim/run_all.tcl" \
  | tee rom_simulation.log

if ! grep -q "SIMPLE_DISPLAY_AXI_ROM_UNIT_PASS" rom_simulation.log; then
  echo "focused AXI ROM simulation did not reach its PASS marker" >&2
  exit 1
fi

"${vivado_bin_dir}/xelab" tb_simple_display_identity_regs \
  -s tb_simple_display_identity_regs_sim \
  | tee identity_elaboration.log
"${vivado_bin_dir}/xsim" tb_simple_display_identity_regs_sim \
  -tclbatch "${project_dir}/sim/run_all.tcl" \
  | tee identity_simulation.log

if ! grep -q "SIMPLE_DISPLAY_SDC1_AXI_UNIT_PASS" identity_simulation.log; then
  echo "focused SDC1 AXI simulation did not reach its PASS marker" >&2
  exit 1
fi

echo "SIMPLE_DISPLAY_AXI_ROM_SIMULATION_PASS"
echo "SIMPLE_DISPLAY_SDC1_AXI_SIMULATION_PASS"
