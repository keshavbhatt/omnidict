#!/usr/bin/env bash
# Store screenshots (the metainfo's <screenshots>): plain captures of the real app, as
# Flathub's quality guidelines ask, rendered headless from dictionaries built locally.
#
#   scripts/store-screenshots.sh            # writes screenshots/store/*.png
#
# Needs the dictionaries below in pipeline/out (make build DICT=...) and uv for Pillow. The
# catalogue shot reads the published catalogue (the app's default), so it shows the real one.
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out="$root/screenshots/store"
work=$(mktemp -d "${TMPDIR:-/tmp}/omnidict-shots.XXXXXX")
trap 'rm -rf "$work"' EXIT
size=1200x760
mkdir -p "$out"

# bundles <dir> <dict_id>...: a directory of links to the named built dictionaries.
bundles() {
    local dir=$1
    shift
    mkdir -p "$dir"
    for id in "$@"; do
        [ -f "$root/pipeline/out/$id/dict.sqlite" ] || { echo "missing pipeline/out/$id: build it first" >&2; exit 1; }
        ln -s "$root/pipeline/out/$id" "$dir/$id"
    done
}

# profile <dir> <theme: 1 light, 2 dark>: a scratch profile, What's new already seen.
profile() {
    mkdir -p "$1"
    printf '[appearance]\ntheme=%s\n\n[window]\nwhatsNewSeenVersion=%s\n' "$2" \
        "$(awk '/^project\(omnidict/ {on = 1} on && $1 == "VERSION" {print $2; exit}' "$root/app/CMakeLists.txt")" \
        >"$1/settings.ini"
}

# shot <name> <theme> <bundles dir> [KEY=value...]: one headless capture.
shot() {
    local name=$1 theme=$2 dir=$3
    shift 3
    profile "$work/profile-$name" "$theme"
    env -u OMNIDICT_CATALOG_URL QT_QPA_PLATFORM=offscreen OMNIDICT_DEBUG_WINDOW_SIZE="$size" OMNIDICT_DEBUG_GRAB="$work/$name.png" "$@" \
        "$root/scripts/dev-run.sh" -- --bundles "$dir" --profile "$work/profile-$name" >/dev/null 2>&1
    [ -s "$work/$name.png" ] || { echo "no capture for $name" >&2; exit 1; }
}

bundles "$work/english" wikt-en oewn-en freedict-en-de freedict-en-fr wikt-es-en
bundles "$work/cjk" jmdict-ja-en cedict-zh-en kengdic-ko-en

shot lookup 1 "$work/english" OMNIDICT_DEBUG_QUERY=book
shot suggestions 1 "$work/english" OMNIDICT_DEBUG_QUERY=dictionery
shot dark 2 "$work/cjk" OMNIDICT_DEBUG_QUERY=水
shot catalogue-sheet 1 "$work/english" OMNIDICT_DEBUG_OPEN=available

cp "$work/lookup.png" "$out/00-lookup.png"
cp "$work/suggestions.png" "$out/02-suggestions.png"
cp "$work/dark.png" "$out/03-dark.png"
# The Dictionaries sheet is a window of its own: shown over the dimmed main window, as on screen.
uv run --no-project --with pillow python - "$work/lookup.png" "$work/catalogue-sheet.png" "$out/01-catalogue.png" <<'EOF'
import sys
from PIL import Image, ImageDraw, ImageFilter

main = Image.open(sys.argv[1]).convert("RGBA")
sheet = Image.open(sys.argv[2]).convert("RGBA")
target = sys.argv[3]
canvas = Image.alpha_composite(main, Image.new("RGBA", main.size, (0, 0, 0, 110)))
x, y = (main.width - sheet.width) // 2, (main.height - sheet.height) // 2
shadow = Image.new("RGBA", main.size, (0, 0, 0, 0))
ImageDraw.Draw(shadow).rounded_rectangle((x, y + 10, x + sheet.width, y + sheet.height + 10), 12, fill=(0, 0, 0, 120))
canvas = Image.alpha_composite(canvas, shadow.filter(ImageFilter.GaussianBlur(18)))
canvas.alpha_composite(sheet, (x, y))
canvas.convert("RGB").save(target)
EOF
ls -l "$out"
