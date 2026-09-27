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
