#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pin_tag="edk2-stable202605"
pin_commit="b03a21a63e3bd001f52c527e5a57feddb53a690b"
edk2_dir="${EDK2_DIR:-${repo_root}/build/gop/edk2}"
artifact_dir="${ARTIFACT_DIR:-${repo_root}/build/gop/artifacts}"
build_target="${BUILD_TARGET:-DEBUG}"
check_only=0

if [[ "${1:-}" == "--check" ]]; then
  check_only=1
elif [[ $# -ne 0 ]]; then
  echo "usage: $0 [--check]" >&2
  exit 2
fi

case "${build_target}" in
  DEBUG|RELEASE) ;;
  *)
    echo "BUILD_TARGET must be DEBUG or RELEASE" >&2
    exit 2
    ;;
esac

missing=0
for command_name in git make gcc g++ python3; do
  if ! command -v "${command_name}" >/dev/null 2>&1; then
    echo "missing required command: ${command_name}" >&2
    missing=1
  fi
done

if ! command -v nasm >/dev/null 2>&1; then
  echo "missing required command: nasm (EDK II X64 assembly)" >&2
  missing=1
fi

if [[ ${missing} -ne 0 ]]; then
  exit 1
fi

if [[ ${check_only} -eq 1 ]]; then
  if [[ -d "${edk2_dir}/.git" ]]; then
    actual_commit="$(git -C "${edk2_dir}" rev-parse HEAD)"
    if [[ "${actual_commit}" != "${pin_commit}" ]]; then
      echo "EDK2_DIR is at ${actual_commit}, expected ${pin_commit}" >&2
      exit 1
    fi
    echo "pinned EDK II checkout: ${edk2_dir}"
  else
    echo "pinned EDK II checkout will be created at: ${edk2_dir}"
  fi
  echo "UEFI build prerequisites: OK"
  exit 0
fi

mkdir -p "$(dirname "${edk2_dir}")" "${artifact_dir}"
if [[ ! -d "${edk2_dir}/.git" ]]; then
  git clone --depth 1 --branch "${pin_tag}" https://github.com/tianocore/edk2.git "${edk2_dir}"
fi

actual_commit="$(git -C "${edk2_dir}" rev-parse HEAD)"
if [[ "${actual_commit}" != "${pin_commit}" ]]; then
  echo "refusing unpinned EDK II checkout: ${actual_commit}" >&2
  echo "expected ${pin_commit} (${pin_tag})" >&2
  exit 1
fi

# Fetch only the sources required by BaseTools and MdePkg metadata. Avoid
# downloading unrelated CryptoPkg and test-framework submodules.
git -C "${edk2_dir}" submodule update --init \
  BaseTools/Source/C/BrotliCompress/brotli \
  MdeModulePkg/Library/BrotliCustomDecompressLib/brotli \
  MdePkg/Library/BaseFdtLib/libfdt \
  MdePkg/Library/MipiSysTLib/mipisyst

export WORKSPACE="${edk2_dir}"
export PACKAGES_PATH="${edk2_dir}:${repo_root}/firmware/uefi"
export EDK_TOOLS_PATH="${edk2_dir}/BaseTools"
export CONF_PATH="${edk2_dir}/Conf"
export PYTHON_COMMAND=python3
export SOURCE_DATE_EPOCH
SOURCE_DATE_EPOCH="$(git -C "${edk2_dir}" show -s --format=%ct HEAD)"

make -C "${edk2_dir}/BaseTools"
cd "${edk2_dir}"
# edksetup defines the build command and writes Conf files inside the ignored
# EDK II workspace.
set +u
source edksetup.sh BaseTools
set -u

build \
  -a X64 \
  -t GCC \
  -b "${build_target}" \
  -p SimpleDisplayPkg/SimpleDisplayPkg.dsc

output_root="${edk2_dir}/Build/SimpleDisplayPkg/${build_target}_GCC/X64"
for image in SimpleDisplayBringup SimpleDisplayGopDxe SimpleDisplayGopTest; do
  source_image="${output_root}/SimpleDisplayPkg/${image}/${image}/OUTPUT/${image}.efi"
  if [[ ! -f "${source_image}" ]]; then
    source_image="$(find "${output_root}" -type f -name "${image}.efi" -print -quit)"
  fi
  if [[ -z "${source_image}" || ! -f "${source_image}" ]]; then
    echo "build succeeded but ${image}.efi was not found" >&2
    exit 1
  fi
  cp "${source_image}" "${artifact_dir}/${image}.efi"
done

(
  cd "${artifact_dir}"
  sha256sum SimpleDisplayBringup.efi SimpleDisplayGopDxe.efi SimpleDisplayGopTest.efi > SHA256SUMS
)

{
  echo "edk2_tag=${pin_tag}"
  echo "edk2_commit=${actual_commit}"
  echo "architecture=X64"
  echo "toolchain=GCC"
  echo "target=${build_target}"
  echo "compiler=$(gcc --version | head -n 1)"
  echo "source_date_epoch=${SOURCE_DATE_EPOCH}"
  echo "command=build -a X64 -t GCC -b ${build_target} -p SimpleDisplayPkg/SimpleDisplayPkg.dsc"
} > "${artifact_dir}/build-metadata.txt"

echo "UEFI artifacts: ${artifact_dir}"
cat "${artifact_dir}/SHA256SUMS"
