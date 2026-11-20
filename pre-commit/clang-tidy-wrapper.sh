#!/usr/bin/env bash
# =============================================================================
# clang-tidy wrapper — checks compile_commands.json before running clang-tidy
# =============================================================================
set -euo pipefail

BUILD_DIRS=("build")

for dir in "${BUILD_DIRS[@]}"; do
    if [[ -f "${dir}/compile_commands.json" ]]; then
        break
    fi
done

if [[ ! -f "${dir}/compile_commands.json" ]]; then
    cat >&2 <<'EOF'
=======================================================================
  [SKIP] compile_commands.json NOT FOUND
----------------------------------------------------------------------
  clang-tidy requires a compilation database to work.
  Generate one with `python3 build.py` (creates build/compile_commands.json)
  to enable clang-tidy locally.

  In environments without a build (e.g. CI), clang-tidy is skipped so the
  remaining checks can proceed.
=======================================================================
EOF
    exit 0
fi

exec clang-tidy "$@"
