# Roadmap

Milestones with exit criteria, from `DOCS/PLAN.md` section 8. Packaging is deliberately last
(the kit's principle): a working, tested app before an installable one.

| Milestone | Deliverable | Acceptance | Kit components arriving here |
|---|---|---|---|
| **M0** | Repo scaffold; `omnipipe.schema`, `omnipipe.html_subset`, `omnipipe.normalize` with shared test vectors; `omnipipe.build` producing a valid `dict.sqlite` from hand-written JSONL | CI green; the C++ `Bundle` test opens the fixture bundle and looks up 3 words | - |
| **M1** | Kaikki converter producing `wikt-hi-en`, `wikt-en` (monolingual), `wikt-es-en`; `package.py`; `catalog.py` | Three `.odict` bundles and a `catalog.json` served from a local HTTP server | - |
| **M2** | Qt client: search page, results grouped by dictionary, entry page, history/favorites | Manual test against the M1 bundles | Settings facade, `log_sink`, `single_instance`, `theme_service` and style tokens, `message_sheet`, icons tooling, About, diagnostics, bug report, shortcuts sheet, What's new dialog |
| **M3** | Manage-dictionaries dialog, downloader, install flow, About | Install/update/delete cycle works from the local catalog | `AccountAndLicense` module, `services/licensing`, and its licence text `LICENSES/LicenseRef-Ktechpit-Licensing-Module.txt` (from the kit template, app name Omnidict) |
| **M4** | FreeDict, WordNet, CC-CEDICT, JMdict converters; catalog over 100 dictionaries; publish to CDN | Real catalog reachable from the app | - |
| **M5** | Packaging: Snap, Flatpak, AppImage; CI release workflow | Installable on a clean Ubuntu | `snapcraft.yaml`, the snap CI workflow, the Flatpak manifest, the desktop file, the metainfo, the screenshots script |
| **M6** | Web app (PLAN.md section 7A): static entry-page generator, SSR server with search API, sitemaps, attribution footer, transliteration search for Indian languages, ad slots | Site live on a domain; Google Search Console shows entry pages indexed; a golden-file test proves the web and the desktop client render identical entry HTML | - |
| **v2** | DAWG/perfect-hash index, spell suggestion, MeCab/Jieba tokenization for CJK lookup, Windows/macOS builds, StarDict import (unofficial tier) | N/A | - |

Order: **M0, then M1, then M2**, in that order. M1 lands before M2 so the client is developed
against real data, not only the M0 fixture. **M6 is deliberately last**: no `web/` work starts
before M5 has shipped.
