#!/usr/bin/env bash
# Shared quality checks: formatting, linting, typecheck.
# Used by scripts/pre-commit and mirrored by .github/workflows/ci.yml.
set -u

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
CLIENT="$ROOT/clients/linux-qt"
fail=0

CLANG_FORMAT="$(command -v clang-format || true)"
[ -n "$CLANG_FORMAT" ] || CLANG_FORMAT="$HOME/.local/bin/clang-format"

# 1. C++ formatting
if [ -x "$CLANG_FORMAT" ]; then
    if out=$("$CLANG_FORMAT" --dry-run --Werror "$CLIENT"/src/*.cpp "$CLIENT"/src/*.h "$CLIENT"/tests/*.cpp 2>&1); then
        echo "✓ clang-format"
    else
        echo "✗ clang-format (run: clang-format -i clients/linux-qt/src/*.cpp clients/linux-qt/src/*.h clients/linux-qt/tests/*.cpp)"
        echo "$out" | head -10
        fail=1
    fi
else
    echo "⚠ clang-format not found, skipping (pip install --user clang-format)"
fi

# 2. JS formatting
if npx --yes prettier --check "$CLIENT"/extension/*.js > /dev/null 2>&1; then
    echo "✓ prettier"
else
    echo "✗ prettier (run: npx prettier --write clients/linux-qt/extension/*.js)"
    fail=1
fi

# 3. JS syntax lint
for f in "$CLIENT"/extension/*.js; do
    tmp="/tmp/check_$(basename "$f" .js).mjs"
    cp "$f" "$tmp"
    if ! node --check "$tmp" 2> /dev/null; then
        echo "✗ node --check: $f"
        fail=1
    fi
done
[ "$fail" -eq 0 ] && echo "✓ node syntax"

# 4. Rust backend: fmt, clippy, tests
if command -v cargo > /dev/null 2>&1; then
    if cargo fmt --manifest-path "$ROOT/backend/Cargo.toml" -- --check > /dev/null 2>&1; then
        echo "✓ cargo fmt"
    else
        echo "✗ cargo fmt (run: cargo fmt --manifest-path backend/Cargo.toml)"
        fail=1
    fi
    if cargo clippy --manifest-path "$ROOT/backend/Cargo.toml" -- -D warnings > /tmp/translator_clippy.log 2>&1; then
        echo "✓ cargo clippy"
    else
        echo "✗ cargo clippy (see /tmp/translator_clippy.log)"
        fail=1
    fi
    if cargo test --manifest-path "$ROOT/backend/Cargo.toml" > /tmp/translator_cargotest.log 2>&1; then
        echo "✓ cargo test"
    else
        echo "✗ cargo test (see /tmp/translator_cargotest.log)"
        fail=1
    fi
else
    echo "⚠ cargo not found, skipping Rust checks"
fi

# 5. C++ typecheck = full compile (builds the Rust backend via cargo too)
CMAKE="$HOME/Qt/Tools/CMake/bin/cmake"
[ -x "$CMAKE" ] || CMAKE=cmake
if [ -d "$ROOT/build" ]; then
    if "$CMAKE" --build "$ROOT/build" > /tmp/translator_precheck_build.log 2>&1; then
        echo "✓ build (typecheck)"
    else
        echo "✗ build failed (see /tmp/translator_precheck_build.log)"
        fail=1
    fi
else
    echo "⚠ no build dir, skipping build check"
fi

exit "$fail"
