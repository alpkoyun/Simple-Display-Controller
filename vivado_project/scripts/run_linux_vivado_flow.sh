#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
VIVADO_PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd -- "$VIVADO_PROJECT_DIR/.." && pwd)"
VIVADO_BIN="${VIVADO_BIN:-/home/alpk/xilinx/Vivado/2023.2/bin/vivado}"
VITIS_HLS_BIN_DIR="${VITIS_HLS_BIN_DIR:-/home/alpk/xilinx/Vitis_HLS/2023.2/bin}"
BUILD_ROOT="${BUILD_ROOT:-$VIVADO_PROJECT_DIR/linux_build}"
PROJECT_NAME="${PROJECT_NAME:-PCIe}"
HLS_PROJECTS="${HLS_PROJECTS:-color_convert pixel_pack pixel_unpack trace_cntrl_32 trace_cntrl_64}"
JOBS="${JOBS:-2}"
DO_BUILD=0
INSTALL_ARTIFACTS=0
RECREATE=0

usage() {
  cat <<'USAGE'
Usage: run_linux_vivado_flow.sh [--recreate] [--build] [--install-artifacts] [--jobs N]

Default action runs project checks on an existing linux_build project if present,
or recreates it first if missing. --build runs synthesis and implementation.
USAGE
}

hls_ip_missing() {
  local project
  for project in $HLS_PROJECTS; do
    if [[ ! -f "$VIVADO_PROJECT_DIR/hls/$project/script.tcl" ]]; then
      echo "ERROR: missing HLS script: $VIVADO_PROJECT_DIR/hls/$project/script.tcl" >&2
      exit 1
    fi
    if [[ ! -f "$VIVADO_PROJECT_DIR/hls/$project/solution1/impl/ip/component.xml" ]]; then
      return 0
    fi
  done
  return 1
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --recreate) RECREATE=1 ;;
    --build) DO_BUILD=1 ;;
    --install-artifacts) INSTALL_ARTIFACTS=1 ;;
    --jobs) shift; JOBS="$1" ;;
    --help) usage; exit 0 ;;
    *) echo "ERROR: unknown option $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

if [[ ! -x "$VIVADO_BIN" ]]; then
  echo "ERROR: Vivado not found or not executable: $VIVADO_BIN" >&2
  exit 1
fi

export PATH="$VITIS_HLS_BIN_DIR:$PATH"
if hls_ip_missing; then
  if ! command -v vitis_hls >/dev/null 2>&1; then
    echo "ERROR: HLS IP packages are missing and vitis_hls is not on PATH" >&2
    echo "Set VITIS_HLS_BIN_DIR to the Vitis HLS 2023.2 bin directory." >&2
    exit 1
  fi
  echo "INFO: generating missing HLS IP packages"
  (
    cd "$VIVADO_PROJECT_DIR/hls"
    export HLS_PROJECTS
    bash ./build_ip.sh
  )
fi

mkdir -p "$BUILD_ROOT"

PROJECT_FILE="$BUILD_ROOT/$PROJECT_NAME/$PROJECT_NAME.xpr"
if [[ "$RECREATE" == 1 && -e "$BUILD_ROOT/$PROJECT_NAME" ]]; then
  echo "ERROR: $BUILD_ROOT/$PROJECT_NAME already exists; move it aside before --recreate" >&2
  exit 1
fi

if [[ ! -f "$PROJECT_FILE" ]]; then
  echo "INFO: recreating Vivado project in $BUILD_ROOT"
  (
    cd "$BUILD_ROOT"
    "$VIVADO_BIN" -mode batch -nojournal -nolog -notrace \
      -source "$VIVADO_PROJECT_DIR/scripts/recreate_pcie_linux.tcl" \
      -tclargs --origin_dir "$VIVADO_PROJECT_DIR/export" --project_name "$PROJECT_NAME"
  )
fi

BUILD_ARGS=(
  --project "$PROJECT_FILE"
  --repo-root "$REPO_ROOT"
  --jobs "$JOBS"
)

if [[ "$DO_BUILD" == 1 ]]; then
  BUILD_ARGS+=(--build)
fi
if [[ "$INSTALL_ARTIFACTS" == 1 ]]; then
  BUILD_ARGS+=(--install-artifacts)
fi

"$VIVADO_BIN" -mode batch -nojournal -nolog -notrace \
  -source "$VIVADO_PROJECT_DIR/scripts/build_pcie_linux.tcl" \
  -tclargs "${BUILD_ARGS[@]}"
