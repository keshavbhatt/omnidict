#!/usr/bin/env bash
# Builds omnidict on the host against the KDE Qt 6.11 snap SDK
# (kde-qt6-core24-sdk), the same Qt the shipped snap consumes at runtime from
# the kf6-core24 content snap and that Flathub's KDE runtime provides.
# Dev/prod parity: what compiles here is what ships (ADR-009).
#
#   scripts/dev-build.sh            # RelWithDebInfo into ./build
#   OMNIDICT_BUILD_TYPE=Debug scripts/dev-build.sh
#   OMNIDICT_CMAKE_ARGS="-DOMNIDICT_WERROR=ON" scripts/dev-build.sh
#   scripts/dev-build.sh --tests    # also run ctest afterwards
set -euo pipefail

DIR="$(cd "$(dirname "$0")/.." && pwd)"
SDK=/snap/kde-qt6-core24-sdk/current
BUILD="${OMNIDICT_BUILD_DIR:-$DIR/build}"

# shellcheck source-path=SCRIPTDIR source=snap-runtime-env.sh
source "$DIR/scripts/snap-runtime-env.sh"

if [ ! -d "$SDK/usr/lib/x86_64-linux-gnu/cmake/Qt6" ]; then
    echo "Qt SDK snap missing. Install with:" >&2
    echo "  sudo snap install kde-qt6-core24-sdk" >&2
    exit 1
fi
if [ ! -d "$OMNIDICT_RT/usr/lib/x86_64-linux-gnu" ]; then
    echo "kf6-core24 runtime snap missing (needed to run/test). Install with:" >&2
    echo "  sudo snap install kf6-core24" >&2
    exit 1
fi

# The SDK's own build tools (moc, rcc, ...) need the SDK's libraries, some of
# which sit in the libproxy subdirectory.
export LD_LIBRARY_PATH="$SDK/usr/lib/x86_64-linux-gnu:$SDK/usr/lib/x86_64-linux-gnu/libproxy${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# Extra CMake arguments: OMNIDICT_CMAKE_ARGS="-DOMNIDICT_WERROR=ON -DFOO=bar"
read -r -a EXTRA_ARGS <<< "${OMNIDICT_CMAKE_ARGS:-}"

omnidict_prepare_runtime_farm "$BUILD"

# A host compiler without multiarch paths would otherwise find the host's
# SQLite, ICU and zstd instead of the SDK's; FindSQLite3 also trusts the
# host's pkg-config first, so its paths are given outright. The SDK carries
# only headers and a dangling libsqlite3.so/libzstd.so; the libraries
# themselves are in the core24 base snap, as they are for the shipped snap.
# Both are linked through the farm so the RPATH never points at core24's
# glibc (ADR-015 for zstd).
# The CMake root is app/, not the repository root (ADR-010).
cmake -S "$DIR/app" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE="${OMNIDICT_BUILD_TYPE:-RelWithDebInfo}" \
    -DCMAKE_PREFIX_PATH="$SDK/usr" \
    -DCMAKE_LIBRARY_PATH="$SDK/usr/lib/x86_64-linux-gnu" \
    -DSQLite3_INCLUDE_DIR="$SDK/usr/include" \
    -DSQLite3_LIBRARY="$BUILD/core24-libs/libsqlite3.so.0" \
    -DZstd_INCLUDE_DIR="$SDK/usr/include" \
    -DZstd_LIBRARY="$BUILD/core24-libs/libzstd.so.1" \
    -DQt6_DIR="$SDK/usr/lib/x86_64-linux-gnu/cmake/Qt6" \
    -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath-link,$SDK/usr/lib/x86_64-linux-gnu -Wl,--allow-shlib-undefined" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DOMNIDICT_SNAP_RUNTIME="$OMNIDICT_RT" \
    -DOMNIDICT_SNAP_RUNTIME_LIB_PATH="$(omnidict_runtime_lib_path "$BUILD")" \
    "${EXTRA_ARGS[@]}"
cmake --build "$BUILD" -j"$(nproc)"

# clangd / IDEs look for compile_commands.json at the repo root.
ln -sf "$BUILD/compile_commands.json" "$DIR/compile_commands.json"

if [ "${1:-}" = "--tests" ]; then
    "$DIR/scripts/dev-run.sh" --ctest
fi
