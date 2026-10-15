#!/usr/bin/env bash
# Build in an existing compatible toolchain, or in the legacy Docker image.
set -euo pipefail
repo_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
edk2_dir="${EDK2_DIR:-}"
mode="${1:-}"
if [[ "$mode" == --docker ]]; then shift; fi
if [[ -z "$edk2_dir" || ! -f "$edk2_dir/edksetup.sh" ]]; then
    echo 'Set EDK2_DIR to the pinned EDK II checkout.' >&2; exit 1
fi
edk2_dir="$(cd -- "$edk2_dir" && pwd)"
python3 "$repo_dir/scripts/integrate.py" --edk2 "$edk2_dir" >/dev/null
jobs="${VTPM_JOBS:-4}"
if [[ "$mode" == --docker ]]; then
    image="${VTPM_BUILD_IMAGE:-vtpm-edk2-2018}"
    # Mount the source at its real path as well: the integration symlink must
    # resolve inside Docker when the repository is outside the EDK II tree.
    exec docker run --rm --user "$(id -u):$(id -g)" \
        -v "$edk2_dir:/workspace" -v "$repo_dir:$repo_dir:ro" -w /workspace \
        "$image" bash -c '
            set -e
            export WORKSPACE=/workspace EDK_TOOLS_PATH=/workspace/BaseTools
            export PATH=/workspace/BaseTools/BinWrappers/PosixLike:$PATH
            make -C BaseTools -j"$1"
            setup_edk2() { source ./edksetup.sh; }
            setup_edk2
            shift
            build -p OvmfPkg/OvmfPkgX64.dsc -a X64 -b DEBUG -t GCC5 -D SMM_REQUIRE=TRUE "$@"
        ' bash "$jobs" -n "$jobs" "$@"
fi
# EDK II 2018 setup does not support nounset.
set +u
cd -- "$edk2_dir"
export WORKSPACE="$edk2_dir" EDK_TOOLS_PATH="$edk2_dir/BaseTools"
export PATH="$EDK_TOOLS_PATH/BinWrappers/PosixLike:$PATH"
make -C BaseTools -j"$jobs"
setup_edk2() { source ./edksetup.sh; }
setup_edk2
exec build -p OvmfPkg/OvmfPkgX64.dsc -a X64 -b DEBUG -t GCC5 -D SMM_REQUIRE=TRUE -n "$jobs" "$@"
