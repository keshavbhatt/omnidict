#!/usr/bin/env python3
"""Draws the Windows logos from the app icon (ADR-022): the MSIX logos in
packaging/windows/Assets and the program's own icon, app/dist/windows/omnidict.ico.

Run it again after the icon changes and commit the results. Needs cairosvg and Pillow:

  uvx --with cairosvg --with pillow python scripts/windows-store-assets.py
"""

import io
import pathlib

import cairosvg
from PIL import Image

ROOT = pathlib.Path(__file__).resolve().parent.parent
ICON = ROOT / "app/src/resources/icons/hicolor/scalable/apps/com.ktechpit.omnidict.svg"
ASSETS = ROOT / "packaging/windows/Assets"
EXE_ICON = ROOT / "app/dist/windows/omnidict.ico"


def icon(size: int) -> Image.Image:
    png = cairosvg.svg2png(url=str(ICON), output_width=size, output_height=size)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def tile(name: str, width: int, height: int, share: float) -> None:
    """The icon centred on a transparent tile, as tall as SHARE of the tile."""
    canvas = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    side = round(min(width, height) * share)
    canvas.alpha_composite(icon(side), ((width - side) // 2, (height - side) // 2))
    canvas.save(ASSETS / name, optimize=True)


def main() -> None:
    ASSETS.mkdir(parents=True, exist_ok=True)
    for scale in (100, 200):
        f = scale / 100
        tile(f"StoreLogo.scale-{scale}.png", round(50 * f), round(50 * f), 1.0)
        tile(f"Square44x44Logo.scale-{scale}.png", round(44 * f), round(44 * f), 1.0)
        tile(
            f"Square150x150Logo.scale-{scale}.png", round(150 * f), round(150 * f), 0.66
        )
        tile(f"Wide310x150Logo.scale-{scale}.png", round(310 * f), round(150 * f), 0.66)
    # The taskbar, Start and the file list use these, without a plate behind them.
    for size in (16, 24, 32, 48, 256):
        tile(f"Square44x44Logo.targetsize-{size}.png", size, size, 1.0)
        tile(f"Square44x44Logo.targetsize-{size}_altform-unplated.png", size, size, 1.0)
        tile(
            f"Square44x44Logo.targetsize-{size}_altform-lightunplated.png",
            size,
            size,
            1.0,
        )

    # The .exe's icon, each size drawn from the vector rather than scaled down from one bitmap.
    EXE_ICON.parent.mkdir(parents=True, exist_ok=True)
    sizes = (16, 20, 24, 32, 40, 48, 64, 256)
    largest = icon(sizes[-1])
    largest.save(
        EXE_ICON,
        format="ICO",
        sizes=[(s, s) for s in sizes],
        append_images=[icon(s) for s in sizes[:-1]],
    )


if __name__ == "__main__":
    main()
