#!/usr/bin/env bash
set -euo pipefail

# check if y2k22 patch has been applied
if [ -n "${XILINX_HLS:-}" ] && [ ! -f "$XILINX_HLS/common/scripts/automg_patch_20220104.tcl" ]; then
	echo "Please make sure you have applied the y2k22 patch to your installation"
	echo "https://support.xilinx.com/s/article/76960?language=en_US"
fi

projects="${HLS_PROJECTS:-color_convert pixel_pack pixel_unpack trace_cntrl_32 trace_cntrl_64}"

for project in $projects
do
  script="$project/script.tcl"
  if [ ! -f "$script" ]; then
    echo "ERROR: missing HLS script: $script" >&2
    exit 1
  fi
  vitis_hls -f "$script"
done
