# Lessons

Rules inherited from the rewrite kit's `PLAYBOOK.md` ("Rules learned the hard way") that apply
to Omnidict, a Qt Widgets desktop app. Items specific to Qt WebEngine, the DOM, CDP, browser
tabs or yt-dlp are left out; they do not apply to a dictionary client.

| Rule | Why |
|---|---|
| Ask the owner the product questions early (modes, gating, defaults); make the routine calls yourself. | The owner's time is the scarce resource; most calls are not actually open questions. |
| Bind F11 to full screen explicitly. | `QKeySequence::FullScreen` resolves to Ctrl+Shift+F under the KDE platform theme in the runtimes, not F11. |
| Force Fusion combo boxes to the list popup. | Fusion's menu-style combo popup shows every row at once instead of a scrollable list. |
| Use `Q_SIGNALS`/`Q_EMIT` everywhere. | `QT_NO_KEYWORDS` is defined globally; the plain keywords do not exist. |
| Write singular and plural forms by hand. | `%n` plural forms render literally (the literal `%n`) without a loaded translation. |
| Script the screenshots. | Composing consistent, on-brand screenshots by hand takes an hour every time; a script takes a minute. |
| Give a word-wrapped `QLabel` a full-width row and pin `setMinimumHeight(layout->totalHeightForWidth(w))`. | A wrapped label's height depends on its width, and Qt leaves that out of a window's minimum size, so a dialog can open with the label clipped. |
| First use must not open with a modal. | A blocking setup dialog on first launch is an interruption, not a welcome; set things up in place and show a sheet only on failure. |
| Every hover-only action gets a right-click twin. | Keyboard, touchpad and screen-reader users never see a hover-only affordance. |
| Bind the shortcuts sheet to both F1 and Ctrl+/. | Covers both the "help key" instinct and the "show me the shortcuts" instinct. |
| One gesture, one outcome. | The same kind of input (a pasted link, a dropped file, a menu action) must always land in the same place, or the app feels inconsistent. |
| A failed item shows its reason in place. | A tooltip alone is never read; the reason for a failure belongs on the item itself. |
| Run the network tests for real before a release (`OMNIDICT_NETWORK_TESTS=1`). | The plain test suite skips network access by design (`DOCS/CODING_STANDARDS.md` section 11); a release still needs the real path exercised at least once. |

## Learned in this project

| Rule | Why |
|---|---|
| Give widgets looks through dynamic properties and rules in the app style sheet (`ui/style.cpp`), never `setStyleSheet` on a container. | A container's own sheet changes how the app sheet reaches its children: a row's buttons lost their size, and colours baked in at creation missed theme changes (2026-09-27). |
| Rich text draws borders only on table cells: boxed inline text (entry labels) is drawn as an image, with the text kept for Copy. | The approved mock boxes labels; a tinted background was not the same (2026-09-27). |
| Read the rest of a QNetworkReply when it finishes, and check every write. | On a fast local server `finished()` came with bytes still buffered; the file was short and the installer blamed its size. A per-user quota on `/tmp` also cut writes short without an error until the next write (2026-09-28). |
| Give every window title through `ui::titleWithApp()`. | Qt on Linux appends the app's display name to a title that does not end with it, joined by an em dash, which this project never uses (2026-09-28). |
| Turn off Ctrl+wheel zoom on text views (`ui::disableWheelZoom`, or `EntryView::wheelEvent`). | Qt's built-in zoom has no limits, scales only text without a set size, and bypasses the text-size setting (2026-09-28). |
| Give every QComboBox `ui::styleComboPopup` and keep the sheet's right padding small; size a box from measured text. | Qt's default combo delegate ignores `::item` rules and paints in the smaller menu font, so the list was cramped; and Qt keeps the 26 px arrow clear of the text on top of the sheet's padding, so a 30 px right padding cropped "Any language" (2026-09-28). |
| Measure before optimising a sheet: time each step with a temporary `QElapsedTimer`, then remove it. | The Dictionaries sheet took 631 ms to open: 476 ms went to native language names, whose scripts sent Qt through every installed font, not to the 623 rows everyone suspected (2026-09-29). |
| A download whose checksum is wrong while its size is right means the file was replaced: re-read the catalogue, as for a size mismatch. | The rolling release replaces files under the same name; a rebuilt Sumerian bundle had the old size and new bytes, and every client with a cached catalogue failed until it expired (2026-09-29). |
| Qt shapes a complex script (Syriac to Sinhala, Khmer, N'Ko) only with a font whose GSUB table lists that script. | Noto Sans Thaana has GPOS only, so Qt rejects it and Dhivehi shows boxes on this desktop, after trying every font as a fallback (thousands of "OpenType support missing" warnings); no other shaped script lacks a usable font here (2026-09-29). |
