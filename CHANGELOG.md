# Changelog

All notable changes to Omnidict. The format follows [Keep a Changelog](https://keepachangelog.com/);
versions follow [Semantic Versioning](https://semver.org/).

## [0.1.0] - 2026-09-30

The first version: an offline dictionary with more than six hundred free dictionaries to
choose from, in hundreds of languages.

### Added
- A catalogue of dictionaries to download: Wiktionary for over three hundred languages, hundreds of FreeDict bilingual dictionaries, English WordNet, JMdict (Japanese), CC-CEDICT (Chinese) and Kengdic (Korean).
- Filter the catalogue by language and provider, and see each dictionary's size and number of entries before downloading.
- Downloads run in the background, resume after an interruption and are checked before they are installed; updates are offered when a newer version is out.
- Reorder dictionaries, switch them off, or remove them.
- Results appear as you type, grouped by dictionary, with the best match first.
- Accents are optional: resume finds résumé; an inflected form finds its entry: ran finds run.
- Wildcards: ? stands for one letter and * for any run of letters.
- "Did you mean" suggestions when a word is misspelled, in every script.
- Words that appear only inside definitions are listed under "Also found in definitions".
- Favourites and recent lookups; Ctrl+D stars the entry on screen.
- Links inside an entry open that word, and Back returns; the mouse's back and forward buttons work as in a browser.
- Light and dark themes that follow the desktop, and a text size you can change.
- Keyboard shortcuts for everything, listed with F1.
- An About screen that credits every dictionary and links its source, and a bug report sheet.
- Quick lookup: select a word in any app and press Ctrl+Alt+D to see its meaning in a small window; `omnidict --popup` opens it from a custom shortcut on desktops without global shortcuts.
