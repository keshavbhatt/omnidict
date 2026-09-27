export PATH := $(HOME)/.local/bin:$(PATH)

.PHONY: sync test lint format fixture clean

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

clean:
	rm -rf out .pytest_cache .mypy_cache .ruff_cache
