# Top-level entry points. The pipeline has its own Makefile (pipeline/), the
# app builds through scripts/dev-build.sh against the KDE snap SDK.
#
#   make fixture   build the sample bundle the C++ tests read
#   make build     configure and build the app with tests, warnings as errors
#   make test      pipeline tests, fixture, app build, ctest
#   make lint      ruff, mypy, clang-format, clang-tidy, shellcheck, reuse
#   make format    apply ruff format and clang-format

export PATH := $(HOME)/.local/bin:$(PATH)

BUILD_DIR ?= build
CXX_SOURCES := $(shell find app/src app/tests -name '*.cpp' -o -name '*.h')
TIDY_SOURCES := $(shell find app/src app/tests -name '*.cpp')

.PHONY: all fixture build test test-pipeline test-app lint lint-pipeline lint-app format clean

all: test

fixture:
	$(MAKE) -C pipeline fixture

build:
	OMNIDICT_BUILD_DIR=$(abspath $(BUILD_DIR)) \
	OMNIDICT_CMAKE_ARGS="-DOMNIDICT_BUILD_TESTS=ON -DOMNIDICT_WERROR=ON" \
	scripts/dev-build.sh

test-pipeline:
	$(MAKE) -C pipeline test

test-app: fixture build
	OMNIDICT_BUILD_DIR=$(abspath $(BUILD_DIR)) scripts/dev-run.sh --ctest

test: test-pipeline test-app

lint-pipeline:
	$(MAKE) -C pipeline lint

lint-app: build
	clang-format --dry-run --Werror $(CXX_SOURCES)
	clang-tidy -p $(BUILD_DIR) --quiet --warnings-as-errors='*' $(TIDY_SOURCES)
	shellcheck -x scripts/*.sh

# reuse 6 scans git-ignored trees such as pipeline/.venv, so lint what git
# tracks plus new files; CI runs the full `reuse lint` on a clean checkout.
lint: lint-pipeline lint-app
	git ls-files --cached --others --exclude-standard -z | xargs -0 reuse lint-file

format:
	$(MAKE) -C pipeline format
	clang-format -i $(CXX_SOURCES)

clean:
	rm -rf $(BUILD_DIR) compile_commands.json
	$(MAKE) -C pipeline clean
