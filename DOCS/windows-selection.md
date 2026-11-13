# Quick lookup on Windows: the selected word

Status: **proposed, waiting for the owner** (2026-11-13). Nothing is implemented yet.

## The problem

The owner tested the MSIX on Windows (2026-11-13): everything works, but quick lookup opens
with an empty field when a word is only selected. On Linux the popup reads the primary
selection (X11, and Wayland once the popup is active), so selecting is enough. Windows has
no primary selection: `QClipboard::supportsSelection()` is false, so the popup falls back to
the clipboard, and that holds a word only after the user copies it. The approved mock
(`mocks/windows.html` note 5) chose exactly that, "Copy a word in any app, then press it",
and ruled out a simulated Ctrl+C. The owner now wants select-and-press, as on Linux.

There is no single Windows call that returns "the text selected in another app". Three ways
exist, and the tools that do this well combine them.

## A. Ask the app through UI Automation

UI Automation (UIA) is the accessibility interface screen readers use. Omnidict asks for the
focused element (`IUIAutomation::GetFocusedElement`), and when it offers the text pattern,
reads its selection (`IUIAutomationTextPattern::GetSelection`, then `GetText`).

- Leaves the clipboard and clipboard history (Win+V) untouched; sends no keys to the app.
- Works where apps expose the text pattern: Notepad, WordPad, Word, standard edit boxes,
  WinUI and UWP apps, Qt apps, and Chrome and Edge (native UIA on by default since Chrome 138,
  August 2025). Firefox, Electron apps (VS Code, Discord, Slack), PDF readers, terminals and
  games are uneven or give nothing.
- Part of Windows (`UIAutomationClient.h`, `uiautomationcore.lib`): no new third-party
  dependency, so no new `THIRD_PARTY.md` row. A short ADR still records the choice.
- Each call crosses into the other app; a hung app can stall it. It runs on a worker thread
  with a time limit (about 300 ms), and the popup shows at once and fills in when the word
  arrives.
- Asking Chromium or Electron for UIA can switch on their accessibility tree, which costs them
  some memory and speed until they restart. Screen readers and Windows' own tools do the same.

## B. Copy for the user (simulated Ctrl+C)

Omnidict saves the clipboard, sends Ctrl+C to the foreground window (`SendInput`), waits for
the clipboard to change (`GetClipboardSequenceNumber`, up to about 300 ms), reads the word,
and puts the saved clipboard back.

- Works in nearly every app where Ctrl+C copies, including the ones UIA misses.
- The copy goes into clipboard history (Win+V) and, when the user turned it on, synced
  clipboard across devices: the selected text lands there whether or not the user wanted it.
  Omnidict cannot stop that, because the other app does the copying.
- Putting the clipboard back is not perfect: some apps place data that is only produced when
  asked (delayed rendering), large or unusual formats, and app-private formats; those can be
  lost. The restore itself can be kept out of history.
- In a terminal, Ctrl+C with nothing selected stops the running program (cmd, PowerShell,
  WSL, the VS Code terminal). Known terminal windows can be skipped, but not every one.
- The user is still holding the shortcut's keys (Ctrl+Alt) when Omnidict sends Ctrl+C, so it
  must first wait for them to be released, or the app sees Ctrl+Alt+C (on many keyboard
  layouts that types a character).
- Windows refuses keys sent to apps running as administrator, so nothing happens there.
- Some security software flags programs that send keystrokes to other apps.

## C. Keep the clipboard (today)

The user copies, then presses the key. Nothing to build, works everywhere, but it is the
extra step the owner found.

## Recommendation

**A, then C; B only as an opt-in setting, if at all.** Read the selection through UI
Automation; when that gives nothing, take the clipboard as now. This covers the browsers,
Office and most everyday apps with no side effects, and never touches the user's clipboard
or their terminals. A simulated copy fills the remaining gaps but writes the user's
selections into clipboard history and can stop a program in a terminal, so if the owner
wants it, it belongs behind a checkbox, off by default ("Copy the selection when an app
does not share it").

The settings hint changes to "Select or copy a word in any app, then press it", so
`mocks/windows.html` note 5 and the Quick lookup row get updated first for approval.

## Testing

The UIA code is Windows only (`services/selection_windows.cpp`, behind a small interface like
`HotkeyBackend`, so tests on Linux use a fake). Unit tests cover the order (selection, then
clipboard, then empty field) and the word cleaning that already exists. The real reading can
only be checked on Windows: the owner's test list would be Notepad, Word, Edge, Chrome,
Firefox, VS Code, a PDF in Edge, and Windows Terminal, each with a selected word and with
nothing selected.
