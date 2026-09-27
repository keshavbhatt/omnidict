# DESIGN: Omnidict

Principle: **type, read, done**. One search field finds a word in every installed dictionary,
the best match is readable before Enter is pressed, and nothing else competes for attention.
Every screen has an HTML mock in `mocks/` (open `mocks/index.html`; the toolbar at the bottom
right switches dark and light, the OS title bar, and the review notes). No screen or visible
change is coded before its mock is approved; approval is recorded per screen in
`mocks/index.html` and `PROGRESS.md` (CLAUDE.md standing rule).

## 1. Brand and tokens

- Name: Omnidict. Mark: the app icon (ADR-011), a blue book with an orange lookup badge.
- The accent is the icon's blue; the warm orange of its badge marks favourites and the
  frequency diamonds. `mocks/mock.css` is the source of these tokens until the app's style
  code exists; the two must never disagree.

| Token | Dark | Light | Used for |
|---|---|---|---|
| `accent` | `#62A0EA` | `#1C71D8` | focus ring, selected row text, links on controls, active tab |
| `accentStrong` | `#1C71D8` | `#1C71D8` | primary button fill (white text) |
| `accentHover` | `#3584E4` | `#1A5FB4` | hover on primary |
| `accentSoft` | `#1B2B42` | `#E4EEFB` | selected row, focus halo, empty-state tile |
| `warm` | `#FFA348` | `#C64600` | favourite star, frequency diamonds |
| `warmSoft` | `#3A2616` | `#FFF1E3` | favourite pills |
| `bg` | `#16181B` | `#F4F5F7` | window and entry background |
| `bar` | `#1C1E22` | `#FFFFFF` | header bar, entry bar, title bar |
| `panel` | `#1E2024` | `#FFFFFF` | results list, sheets, cards |
| `elevated` | `#272A30` | `#FFFFFF` | menus, popups, toasts |
| `hover` | `#2D3138` | `#ECEEF2` | hover on neutral controls and rows |
| `input` | `#131518` | `#FFFFFF` | text fields |
| `border` | `#2F333A` | `#DEE1E7` | hairlines |
| `text` | `#EEF0F3` | `#16181D` | primary text |
| `muted` | `#9AA2AD` | `#5C6573` | previews, section headings, secondary text |
| `link` | `#78AEED` | `#1C71D8` | `lex:` links in entries |
| `headword` | `#78AEED` | `#1A5FB4` | the entry headword |
| `pattern` | `#5CCB84` | `#1B7F38` | grammar patterns such as `[masculine]` |
| `example` | `#A3ABB6` | `#5C6573` | example sentences |
| `success` / `warning` / `danger` | `#5CCB84` / `#F5B14C` / `#FF6B6B` | `#1B7F38` / `#B45309` / `#C01C28` | states |

Type: the system UI font, with Noto fallbacks for every script a dictionary may contain.
13 px secondary, 14 px UI, 15 px entry body (the user can change it), 30 px headword. Shape:
8 px controls, 12 px cards, 14 px sheets.

## 2. Layout

```
+---------------------------------------------------------------+
| [ search ..................... ] [All dictionaries v]   [D] [=] |  header bar
+-----------------------+---------------------------------------+
| SPANISH - ENGLISH     | [<] [>]  Spanish - English      [c][*] |  entry bar
|  perro          *     |                                       |
|  perra                |   perro  ****                         |
| ENGLISH               |   /ˈpero/                             |
|  perron               |   1 NOUN [masculine]                  |
| ALSO FOUND IN DEFS    |   dog ...                             |
+-----------------------+---------------------------------------+
```

No menu bar on Linux and Windows: the header carries the Dictionaries button and the main
menu, and the shortcuts sheet lists every action. macOS keeps its native menu bar. Dialogs
are sheets over the window.

## 3. Screens

| Mock | Shows | Milestone |
|---|---|---|
| `main.html` | Results grouped by dictionary, entry with the new entry bar and attribution line | M2 |
| `main-empty.html` | Empty search: favourites, recent, welcome tips | M2 |
| `main-no-results.html` | Zero matches with "Did you mean" (look only; method open) | M2 |
| `main-link.html` | After following a link inside an entry, with Back | M2 |
| `filter.html` | The dictionary filter popup | M2 |
| `menu.html` | The main menu | M2 |
| `settings.html` | Settings sheet | M2 |
| `shortcuts.html` | Keyboard shortcuts sheet (F1, Ctrl+/) | M2 |
| `whats-new.html` | What's new sheet with a version picker | M2 |
| `bug-report.html` | Report a bug sheet | M2 |
| `about.html` | About with dictionary credits, open-source notices, diagnostics | M2/M3 |
| `main-no-dictionaries.html` | First run with nothing installed | M3 |
| `dictionaries.html` | Manage dictionaries: installed, order, updates | M3 |
| `dictionaries-available.html` | Manage dictionaries: catalogue and downloads | M3 |

## 4. Rules that survive the mocks

- The best match is shown while typing, but only an Enter or a click records it in Recent.
- Every entry ends with its dictionary's credit and licence (the content licence requires it).
- Favourites use the warm colour; nothing else does, so the star is always recognisable.
- No modal dialog for what can be a popup; sheets never stack.
- Every screen works at 900 by 600 and scales up; below 720 px the list and entry share one
  column with Back returning to the list.
- Dark and light are both first class; screenshots for the owner are taken in both.
- Keyboard: every action has a shortcut shown in its tooltip and in the shortcuts sheet;
  Escape clears the search or closes the sheet; the focus ring is the accent with a soft halo.
- Colour is never the only signal: the favourite star changes shape (outline or filled),
  states carry words.
- Nothing in the UI mentions accounts or app licensing (CLAUDE.md).
