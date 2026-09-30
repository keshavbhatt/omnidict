# Quick lookup: research and proposal (2026-09-30)

Status: **phase 1 approved and built** (owner, 2026-09-30: mock and Ctrl+Alt+D approved,
launcher search later). ADR-019 records the portal decision.

The owner asked for a quick lookup integrated with the desktop that also works in the Flatpak
and the snap: look a word up from anywhere without switching to the full window.

## What the platforms allow (checked 2026-09-30)

- **Global shortcut.** The only sandbox-friendly way is the xdg-desktop-portal
  `GlobalShortcuts` portal (xdg-desktop-portal 1.16+). Implemented by KDE Plasma (since 5.27),
  GNOME 48+ (not 46 or 47), and Hyprland; not by wlroots desktops (sway, niri). Works from a
  Flatpak with no extra permission; a snap's `desktop` plug allows the calls (end to end in a
  snap: unverified). Qt has no API for it, and KDE's KGlobalAccel is KDE-only: the app calls
  the portal's D-Bus interface itself (Qt DBus).
- **Reading the word the user selected** (the PRIMARY selection) from a background app: works on
  X11 (native and sandboxed). On Wayland it needs the data-control protocol: KDE has it (from
  a sandbox: unverified), GNOME does not. There is no portal for it.
- **Launcher search.** KRunner (KDE) and GNOME Shell search providers can both be exported by a
  Flatpak (KRunner since Flatpak 1.16, off by default; GNOME's always) and started on demand
  by D-Bus. A snap can export neither.
- **A user-set desktop shortcut** that runs `omnidict --popup` works everywhere, including
  `flatpak run` and `snap run`; on GNOME the window may not get focus ("is ready" notification).
- **Bringing the window to the front on Wayland** needs the launching program's activation
  token (`XDG_ACTIVATION_TOKEN`) passed to the running instance, which then calls
  `requestActivate()`. The single-instance hand-off does not pass it today.
- **Tray icon**: works on KDE; GNOME needs an extension. Not needed for this feature.

| Approach | KDE Wayland | KDE X11 | GNOME Wayland | GNOME X11 (46 to 48 only) |
|---|---|---|---|---|
| Portal global shortcut | native, Flatpak, snap (unverified) | same (unverified) | GNOME 48+: native, Flatpak, snap (unverified) | no |
| User-set shortcut running `--popup` | all | all | all, focus may need a click | all |
| Selected word (PRIMARY) | native; sandbox unverified | all | no | all |
| KRunner / GNOME search | native, Flatpak | native, Flatpak | native, Flatpak | native, Flatpak |

## Proposal

**Phase 1: a quick lookup popup, reachable everywhere.**

1. A small **Quick Lookup** window (mock: `DOCS/mocks/quick-lookup.html`): a search field and the
   results with the top entry, over the other apps, closed with Escape; "Open in Omnidict"
   hands the word to the main window. `omnidict --popup [word]` opens it; a running instance
   shows it instead of starting again.
2. **Which word it starts with**: the word given on the command line; else, where the system
   allows reading it (X11, and KDE Wayland when available), the selected word; else the
   clipboard, which the popup may read once it has focus (copy, then shortcut); else an empty
   field ready for typing.
3. **Global shortcut through the portal** (default suggested: Ctrl+Alt+D, changeable in the
   desktop's own shortcut settings, which is where the portal puts it). A Settings row shows
   whether the shortcut is active and, where the portal is missing (GNOME 46/47, sway), how to
   set a desktop shortcut to `omnidict --popup` instead.
4. **The window comes to the front on Wayland**: the second instance forwards its activation
   token; the running one uses it.

New dependency: Qt DBus (part of Qt, already in both runtimes), with an ADR and a
`THIRD_PARTY.md` row per the project's rule.

**Phase 2 (optional): launcher search** in KRunner and GNOME Shell, for the Flatpak and native
installs (snap cannot): typing a word in the desktop's search lists definitions. A separate
decision after phase 1.

**Not proposed**: a tray icon (GNOME needs an extension), an X11 key grab (the portal covers
KDE X11; GNOME X11 is gone from GNOME 49), clipboard watching (not possible on Wayland, and a
privacy concern).

## Decisions (owner, 2026-09-30)

1. Phase 1 as above: built.
2. Popup mock: approved.
3. Default shortcut: Ctrl+Alt+D (a suggestion to the desktop, which may choose another).
4. Phase 2 (launcher search): later.

## As built

- `services::GlobalShortcuts` (Qt DBus): CreateSession, ListShortcuts, and BindShortcuts only
  when the shortcut is not bound yet, so the desktop asks once; Activated opens the popup with
  its activation token. Not started headless (tests, screenshots), so it never reaches the
  real session's portal from there. Works while Omnidict runs.
- `ui::QuickLookup`: a frameless window of its own (not a child of the main window, which would
  hide it with a minimized main window); closes on Escape, the close button or clicking
  elsewhere; with no word it reads the selection once active (Wayland offers it only then),
  else the clipboard. Its own request ids on the shared LookupService.
- `omnidict --popup [word]`: a running instance shows the popup; a fresh one shows only the
  popup, and closing it ends the app. A second launch passes XDG_ACTIVATION_TOKEN (and X11's
  DESKTOP_STARTUP_ID) to the running one.
- Settings, "Quick lookup": the key as the desktop describes it (and "Change..." where the
  portal can open its own dialog, version 2), or the custom-shortcut command for the way the
  app was installed (Flatpak, snap, AppImage, native).

Sources are listed in the research notes of 2026-09-30 in `DOCS/PROGRESS.md`.
