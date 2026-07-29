#!/usr/bin/env bash
# Shared quality checks: formatting, linting, typecheck.
# Used by scripts/pre-commit and mirrored by .github/workflows/ci.yml.
set -u

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
fail=0

CLANG_FORMAT="$(command -v clang-format || true)"
[ -n "$CLANG_FORMAT" ] || CLANG_FORMAT="$HOME/.local/bin/clang-format"

# 1. C++ formatting
if [ -x "$CLANG_FORMAT" ]; then
    if out=$("$CLANG_FORMAT" --dry-run --Werror "$ROOT"/src/*.cpp "$ROOT"/src/*.h 2>&1); then
        echo "✓ clang-format"
    else
        echo "✗ clang-format (run: clang-format -i src/*.cpp src/*.h)"
        echo "$out" | head -10
        fail=1
    fi
else
    echo "⚠ clang-format not found, skipping (pip install --user clang-format)"
fi

# 2. JS formatting
if npx --yes prettier --check "$ROOT"/extension/*.js > /dev/null 2>&1; then
    echo "✓ prettier"
else
    echo "✗ prettier (run: npx prettier --write extension/*.js)"
    fail=1
fi

# 3. JS syntax lint
for f in "$ROOT"/extension/*.js; do
    tmp="/tmp/check_$(basename "$f" .js).mjs"
    cp "$f" "$tmp"
    if ! node --check "$tmp" 2> /dev/null; then
        echo "✗ node --check: $f"
        fail=1
    fi
done
[ "$fail" -eq 0 ] && echo "✓ node syntax"

# 4. C++ typecheck = full compile
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
