# Content sources

Per-source notes from `DOCS/PLAN.md` section 6.2. **No content source is added to the pipeline
without a documented licence in this file.** Kaikki is first, in M1; the rest follow in M4.

| Source | Format | Yields | Licence | Dump URL | Status |
|---|---|---|---|---|---|
| Kaikki (kaikki.org) | JSONL per Wiktionary edition | Hundreds of `X to en` pairs from English Wiktionary; `en to X` and `X to X` pairs from other Wiktionary editions | CC BY-SA 4.0 | to be recorded when the converter is written | planned (M1) |
| FreeDict | TEI XML (P5) | About 150 bilingual pairs, good coverage for `en to X` | mostly GPL / CC, varies per dictionary | to be recorded when the converter is written | planned (M4) |
| WordNet (Open English WordNet) | XML / LMF | `en` monolingual entries plus synonym/antonym relations | CC BY 4.0 | to be recorded when the converter is written | planned (M4) |
| CC-CEDICT | plain text, one line per entry | `zh to en` | CC BY-SA 4.0 | to be recorded when the converter is written | planned (M4) |
| JMdict | XML | `ja to en` (and other targets) | CC BY-SA 4.0 | to be recorded when the converter is written | planned (M4) |
| KEngDic | SQL/CSV | `ko to en` | MPL 2.0 | to be recorded when the converter is written | planned (M4) |
| StarDict community | `.ifo`/`.idx`/`.dict.dz` | Thousands of dictionaries, licence varies or is unclear per file | varies/unclear, needs review per dictionary | to be recorded when the converter is written | planned (v2, opt-in "unofficial" tier only, after licence review) |

## Rule

No content source is wired into `pipeline/omnipipe/converters/` without a row in this table
recording its licence. A source whose licence is "varies/unclear" (StarDict) stays out of the
default catalog until each dictionary drawn from it has been reviewed individually.
