export PATH := $(HOME)/.local/bin:$(PATH)

# Dictionary builds (PLAN.md 6.3):
#   make fetch SOURCE=kaikki     refresh every dump of a source
#   make build DICT=wikt-hi-en   -> out/wikt-hi-en/dict.sqlite
#   make package DICT=wikt-hi-en -> out/publish/dicts/wikt-hi-en/<version>/{*.odict,manifest.json}
#   make catalog                 -> out/publish/catalog.json
#   make serve                   serves out/publish on http://localhost:8000
#   make all                     every dictionary below, then the catalog (make -j works)

SOURCE ?= kaikki
PUBLISH_DIR ?= out/publish
BASE_URL ?= http://localhost:8000
PORT ?= 8000

DICTS_kaikki := wikt-hi-en wikt-es-en wikt-en
DICTS_oewn := oewn-en
DICTS := $(DICTS_kaikki) $(DICTS_oewn)
# dict_id prefix -> converter module
CONVERTER_wikt := kaikki
CONVERTER_oewn := oewn
converter = $(CONVERTER_$(firstword $(subst -, ,$(1))))
# English is large: skip the VACUUM, which needs a temporary copy of the whole bundle.
BUILD_FLAGS_wikt-en := --no-vacuum

.PHONY: sync test lint format fixture fetch build package catalog serve publish all clean \
	require-dict

sync:
	uv sync

test:
	uv run pytest

lint:
	uv run ruff check .
	uv run ruff format --check .
	uv run mypy omnipipe tests

format:
	uv run ruff format .
	uv run ruff check --fix .

fixture:
	uv run python -m omnipipe.build \
		--in ../tests/fixtures/sample-en.jsonl \
		--meta ../tests/fixtures/sample-en.meta.json \
		--out out/sample-en \
		--built-at 2026-09-27T00:00:00Z

require-dict:
	@test -n "$(DICT)" || { echo "DICT is required, one of: $(DICTS)" >&2; exit 1; }

fetch:
	@test -n "$(DICTS_$(SOURCE))" || { echo "unknown SOURCE $(SOURCE)" >&2; exit 1; }
	for d in $(DICTS_$(SOURCE)); do \
		uv run python -m omnipipe.converters.$(SOURCE) --dict $$d --fetch-only || exit 1; \
	done

build: require-dict
	uv run python -m omnipipe.converters.$(call converter,$(DICT)) --dict $(DICT) --no-fetch \
		--out out $(BUILD_FLAGS_$(DICT))

package: require-dict
	uv run python -m omnipipe.package --in out/$(DICT)/dict.sqlite \
		--publish $(PUBLISH_DIR) --base-url $(BASE_URL)

catalog:
	uv run python -m omnipipe.catalog --publish $(PUBLISH_DIR)

serve:
	uv run python -m http.server $(PORT) --directory $(PUBLISH_DIR)

# Where bundles are published is an open question (PLAN.md section 9, item 2).
publish:
	@echo "publish is not set up: the CDN/object store provider is still open (PLAN.md 9)" >&2
	@exit 1

bundle-%:
	$(MAKE) build DICT=$*
	$(MAKE) package DICT=$*

all: $(addprefix bundle-,$(DICTS))
	$(MAKE) catalog

clean:
	rm -rf out .pytest_cache .mypy_cache .ruff_cache
