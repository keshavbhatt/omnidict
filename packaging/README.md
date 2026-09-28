# Packaging

Omnidict is packaged for Linux as a **snap** and a **Flatpak**, both built in CI, never on a
developer machine (the KDE SDK images are several GB). An AppImage is deferred until after the
first release (owner, 2026-09-28; FEATURES F3). ADR-017 has the reasoning.

The app installs everything the packages need through CMake (`app/CMakeLists.txt`): the
binary, `app/dist/linux/com.ktechpit.omnidict.desktop`,
`app/dist/linux/com.ktechpit.omnidict.metainfo.xml` and the hicolor icons.

## Snap

`snap/snapcraft.yaml` builds `app/` against the **kde-neon-6** extension (Qt 6.11 from the
`kf6-core24` content snap, the same runtime `scripts/dev-run.sh` targets).

- Store name **`omnidict`**, registered by the owner at snapcraft.io (never by CI or an agent).
  App id `com.ktechpit.omnidict`; `adopt-info` takes summary and description from the
  metainfo, the version comes from `project(VERSION)`.
- SQLite and zstd come from the core24 base at run time, ICU from the KDE runtime; their
  `-dev` packages are only build dependencies.
- Plugs: the desktop set (desktop, wayland, x11, opengl, unity7) and `network`, for the
  dictionary catalogue and downloads only. The extension adds `audio-playback` and
  `network-bind` itself.
- **CI**: `.github/workflows/snap.yml` builds amd64 and arm64 on pushes to `main` and on
  version tags. With the repo variable `PUBLISH_TO_STORE=true` and the
  `SNAPCRAFT_STORE_CREDENTIALS` secret, `main` goes to **edge** and tags to **candidate**;
  promoting to stable is done by hand.

### Store listing text

The listing (title, summary, description) is edited by hand at the Snap Store; uploads never
change it. `snap/snapcraft.yaml` carries the same text so the snap's own metadata matches.
Limits: title 40 characters, summary 79, description 4096.

## Flatpak

`packaging/flatpak/com.ktechpit.omnidict.yaml`: `org.kde.Platform` 6.11, built from source (the
app is open source). SQLite with FTS5 and the trigram tokenizer, zstd and ICU come from the
runtime (checked on freedesktop 25.08, which KDE 6.11 is built on: SQLite 3.50.4, ICU 77).
Permissions: network (catalogue and downloads), the display sockets and the GPU; nothing on
disk outside the app's own data.

### Submitting to Flathub

Only with the owner's explicit consent (CLAUDE.md). The Flathub repository takes this manifest
with the `dir` source replaced by the release tag (`type: git`, `url`, `tag`, `commit`). Lint
without building:

```
flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest packaging/flatpak/com.ktechpit.omnidict.yaml
flatpak run --command=flatpak-builder-lint org.flatpak.Builder appstream app/dist/linux/com.ktechpit.omnidict.metainfo.xml
```

The appstream lint reports the repository URLs and screenshots as unreachable until the
repository is public, and asks for a `<release>` entry, which the release checklist adds.

## Store text and search

The metainfo's name, summary (35 characters, Flathub's quality limit), first three keywords
and description follow `/home/commander/DCode/FlathubSEO/docs/flathub-search-algorithm.md`;
the research behind the choices is in `FlathubSEO/docs/omnidict-keyword-research.md`. Keep
`dictionary`, `wiktionary` and `thesaurus` as the first three keywords. Store text never
mentions accounts or licensing (CLAUDE.md).

## Screenshots

`scripts/store-screenshots.sh` renders `screenshots/store/*.png` headless from dictionaries
built in `pipeline/out` and the catalogue served by `make -C pipeline serve`: plain window
captures, as Flathub's guidelines ask. Regenerate them once the real catalogue is published:
until then the catalogue shot shows localhost and the local 308 dictionaries.

## Releases

`.github/workflows/release.yml` turns a pushed version tag (`v0.1.0`) into a GitHub release
whose notes are that version's CHANGELOG section. It refuses a tag that does not match
`project(VERSION)` or a section that is not dated. Before tagging (CLAUDE.md release checklist):

- `CHANGELOG.md`: date the `## [x.y.z]` heading.
- `app/dist/linux/com.ktechpit.omnidict.metainfo.xml`: add `<release version date>` with notes.
- `app/CMakeLists.txt`: bump `project(VERSION)`.
- `snap/snapcraft.yaml` and the Snap Store listing: update the description if it changed.

## Validation without building

```
desktop-file-validate app/dist/linux/com.ktechpit.omnidict.desktop
appstreamcli validate --pedantic app/dist/linux/com.ktechpit.omnidict.metainfo.xml
snapcraft expand-extensions        # in the repo root, no build
DESTDIR=$PWD/stage cmake --install build --prefix /usr && find stage -type f
```
