#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
VIVADO_PROJECT_DIR="$(cd -- "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd -- "$VIVADO_PROJECT_DIR/.." && pwd)"
VIVADO_BIN="${VIVADO_BIN:-/home/alpk/xilinx/Vivado/2023.2/bin/vivado}"
VITIS_HLS_BIN_DIR="${VITIS_HLS_BIN_DIR:-/home/alpk/xilinx/Vitis_HLS/2023.2/bin}"
BUILD_ROOT="${BUILD_ROOT:-$VIVADO_PROJECT_DIR/linux_build}"
PROJECT_NAME="PCIe_GOP_ROM_32K_1080P_DEFAULT"
HLS_PROJECTS="color_convert pixel_pack pixel_unpack trace_cntrl_32 trace_cntrl_64"
JOBS="${JOBS:-2}"
DO_BUILD=0
INSTALL_ARTIFACTS=0
RECREATE=0
VERIFY_ONLY=0

usage() {
  cat <<'USAGE'
Usage: run_linux_vivado_flow.sh [--recreate] [--build] [--verify]
                                [--install-artifacts]
                                [--jobs N]

Default action runs project checks on an existing linux_build project if present,
or recreates it first if missing. --build runs synthesis and implementation.
This checkout has one fixed hardware configuration: the validated display
design plus a 32 KiB uncompressed EFI/GOP Expansion ROM and SDC1 identity.
Packaging and Vivado recreation fail closed unless cold-state binding evidence
pins the exact GOP driver embedded in the ROM.
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
    --verify) VERIFY_ONLY=1 ;;
    --install-artifacts) INSTALL_ARTIFACTS=1 ;;
    --jobs)
      shift
      if [[ $# -eq 0 ]]; then
        echo "ERROR: --jobs requires a positive integer" >&2
        exit 2
      fi
      JOBS="$1"
      ;;
    --help) usage; exit 0 ;;
    *) echo "ERROR: unknown option $1" >&2; usage >&2; exit 2 ;;
  esac
  shift
done

if [[ ! "$JOBS" =~ ^[1-9][0-9]*$ ]]; then
  echo "ERROR: --jobs requires a positive integer, got: $JOBS" >&2
  exit 2
fi
if [[ "$VERIFY_ONLY" == 1 && ("$RECREATE" == 1 || "$DO_BUILD" == 1 || "$INSTALL_ARTIFACTS" == 1) ]]; then
  echo "ERROR: --verify cannot be combined with --recreate, --build, or --install-artifacts" >&2
  exit 2
fi
if [[ "$INSTALL_ARTIFACTS" == 1 && "$DO_BUILD" != 1 ]]; then
  echo "ERROR: --install-artifacts requires --build" >&2
  exit 2
fi

python3 "${VIVADO_PROJECT_DIR}/scripts/check_vivado_source_tree.py"

cold_bind_evidence="${COLD_BIND_EVIDENCE:-${REPO_ROOT}/firmware/uefi/test_logs/2026-08-05_1080p_cold_bind_pass/COLD_GOP_BIND_VALIDATION_PASS.json}"
gop_driver="${GOP_DRIVER:-${REPO_ROOT}/firmware/uefi/test_logs/2026-08-05_1080p_cold_bind_pass/tested_payload/SimpleDisplayGopDxe.efi}"
gop_test="${GOP_TEST:-${REPO_ROOT}/firmware/uefi/test_logs/2026-08-05_1080p_cold_bind_pass/tested_payload/SimpleDisplayGopTest.efi}"
gop_rom="${VIVADO_PROJECT_DIR}/rom/simple_display_gop_option_rom.bin"
if ! python3 "${REPO_ROOT}/scripts/check_uefi_cold_bind_validation.py" \
  --evidence "${cold_bind_evidence}" \
  --driver "${gop_driver}" \
  --gop-test "${gop_test}"; then
  echo "ERROR: fixed EFI/GOP hardware requires the recorded cold-state binding pass." >&2
  echo "Run scripts/record_uefi_cold_bind_validation.py with the captured logs first." >&2
  exit 1
fi
python3 "${VIVADO_PROJECT_DIR}/scripts/check_efi_option_rom.py" \
  "${gop_rom}" --driver "${gop_driver}"

python3 "${VIVADO_PROJECT_DIR}/scripts/check_bypass_64mb_baseline.py"

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
if [[ "$VERIFY_ONLY" == 1 ]]; then
  if [[ ! -f "$PROJECT_FILE" ]]; then
    echo "ERROR: canonical routed project does not exist: $PROJECT_FILE" >&2
    exit 1
  fi
  "$VIVADO_BIN" -mode batch -nojournal -nolog -notrace \
    -source "$VIVADO_PROJECT_DIR/scripts/verify_pcie_linux_implementation.tcl" \
    -tclargs --project "$PROJECT_FILE" --repo-root "$REPO_ROOT"
  exit 0
fi

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
