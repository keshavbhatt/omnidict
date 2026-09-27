# Changelog

All notable changes to Omnidict. The format follows [Keep a Changelog](https://keepachangelog.com/);
versions follow [Semantic Versioning](https://semver.org/).

## [0.1.0] - unreleased

First scaffold of Omnidict: a single, consistent dictionary bundle format and the beginnings of
lookup.

### Added
- A dictionary bundle format: one self-contained file per dictionary, holding headwords,
  pronunciations, senses, examples and cross-references in one consistent structure.
- Lookup by exact headword, from a command-line tool for now.
- Matching by prefix and by inflected or variant form (for example, a plural or past-tense
  form finds its base word).
