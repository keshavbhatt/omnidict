#!/usr/bin/env bash
# Runs the dev build against the kf6-core24 runtime content snap, the very
# libraries the shipped omnidict snap uses. Host fonts, display and D-Bus work
# directly.
#
#   scripts/dev-run.sh                 # launch the app
#   scripts/dev-run.sh --ctest [args]  # run the test suite (env baked in by CMake)
#   scripts/dev-run.sh -- --some-flag  # pass arguments to omnidict
set -euo pipefail

DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="${OMNIDICT_BUILD_DIR:-$DIR/build}"

# shellcheck source-path=SCRIPTDIR source=snap-runtime-env.sh
source "$DIR/scripts/snap-runtime-env.sh"

[ -d "$OMNIDICT_RT/usr/lib/x86_64-linux-gnu" ] || {
    echo "kf6-core24 runtime snap missing: sudo snap install kf6-core24" >&2; exit 1; }

if [ "${1:-}" = "--ctest" ]; then
    # ctest is a host binary: keep the host environment. The tests themselves
    # get the runtime env through their CTest ENVIRONMENT property
    # (app/tests/CMakeLists.txt, from OMNIDICT_SNAP_RUNTIME*).
    cd "$BUILD"
    exec ctest --output-on-failure "${@:2}"
fi

omnidict_prepare_runtime_farm "$BUILD"
omnidict_export_runtime_env "$BUILD"
# Route file dialogs through xdg-desktop-portal so they use the system dialogs
# (the KDE platform-theme plugin is not in the runtime snap).
export QT_QPA_PLATFORMTHEME="${QT_QPA_PLATFORMTHEME_OVERRIDE:-xdgdesktopportal}"
export QT_LOGGING_RULES="${QT_LOGGING_RULES:-omnidict.*.debug=true}"

[ "${1:-}" = "--" ] && shift
cd "$DIR"
exec "$BUILD/src/omnidict" "$@"
