#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

VIVADO="${VIVADO:-/home/alpk/xilinx/Vivado/2023.2/bin/vivado}"
HW_SERVER="${HW_SERVER:-/home/alpk/xilinx/Vitis/2023.2/bin/hw_server}"
BIT_FILE="${BIT_FILE:-${REPO_ROOT}/fpga_hardware/PCIe_wrapper/PCIe_wrapper.bit}"
LTX_FILE="${LTX_FILE:-${REPO_ROOT}/fpga_hardware/PCIe_wrapper/PCIe_wrapper.ltx}"
TARGET_FILTER="${TARGET_FILTER:-*}"
LOG_DIR="${LOG_DIR:-${REPO_ROOT}/tmp_hw_probe}"
START_HW_SERVER=1

usage() {
    cat <<EOF
usage: $(basename "$0") [options]

Programs the FPGA with a bitstream and probes the ILAs with the matching LTX.

Options:
  --bit FILE          Bitstream to program (default: ${BIT_FILE})
  --ltx FILE          LTX probes file (default: ${LTX_FILE})
  --target GLOB       Vivado hardware target glob (default: ${TARGET_FILTER})
  --log-dir DIR       Directory for Vivado and hw_server logs (default: ${LOG_DIR})
  --no-hw-server      Do not launch a local hw_server before running Vivado
  -h, --help          Show this help
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bit)
            BIT_FILE="$2"
            shift 2
            ;;
        --ltx)
            LTX_FILE="$2"
            shift 2
            ;;
        --target)
            TARGET_FILTER="$2"
            shift 2
            ;;
        --log-dir)
            LOG_DIR="$2"
            shift 2
            ;;
        --no-hw-server)
            START_HW_SERVER=0
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "ERROR: unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ ! -x "${VIVADO}" ]]; then
    echo "ERROR: Vivado is not executable: ${VIVADO}" >&2
    exit 2
fi

if [[ ! -f "${BIT_FILE}" ]]; then
    echo "ERROR: bitstream does not exist: ${BIT_FILE}" >&2
    exit 2
fi

if [[ ! -f "${LTX_FILE}" ]]; then
    echo "ERROR: probes file does not exist: ${LTX_FILE}" >&2
    exit 2
fi

mkdir -p "${LOG_DIR}"
VIVADO_LOG="${LOG_DIR}/program_fpga_and_probe_ilas.log"
HW_SERVER_LOG="${LOG_DIR}/hw_server.log"
HW_SERVER_STDOUT="${LOG_DIR}/hw_server.stdout"
HW_SERVER_PID=""

cleanup() {
    if [[ -n "${HW_SERVER_PID}" ]]; then
        kill "${HW_SERVER_PID}" >/dev/null 2>&1 || true
        wait "${HW_SERVER_PID}" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

if [[ "${START_HW_SERVER}" -eq 1 ]]; then
    if [[ ! -x "${HW_SERVER}" ]]; then
        echo "ERROR: hw_server is not executable: ${HW_SERVER}" >&2
        exit 2
    fi
    "${HW_SERVER}" -L"${HW_SERVER_LOG}" -ldiscovery,jtag2 -s TCP::3121 -I 20 >"${HW_SERVER_STDOUT}" 2>&1 &
    HW_SERVER_PID="$!"
    sleep 2
fi

echo "BIT_FILE=${BIT_FILE}"
echo "LTX_FILE=${LTX_FILE}"
echo "TARGET_FILTER=${TARGET_FILTER}"
echo "VIVADO_LOG=${VIVADO_LOG}"

"${VIVADO}" \
    -mode batch \
    -nojournal \
    -nolog \
    -notrace \
    -source "${SCRIPT_DIR}/program_fpga_and_probe_ilas.tcl" \
    -tclargs \
    -bit "${BIT_FILE}" \
    -ltx "${LTX_FILE}" \
    -target "${TARGET_FILTER}" 2>&1 | tee "${VIVADO_LOG}"
