#!/usr/bin/env bash
# Run a tools/patterns/*.ts script with the main repo's web/ toolchain
# (tsx, sharp, and the "@/..." alias from web/tsconfig.json).
#   tools/patterns/run.sh render <pattern.js> --out <dir> [...]
#   tools/patterns/run.sh stack verify <pattern.js>
# PF_WEB overrides where the main repo's web/ lives.
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
web="${PF_WEB:-$here/../../../랜딩페이지_opensorce/web}"
web="$(cd "$web" && pwd)"
tool="$1"; shift
args=()
caller="$(pwd)"
for x in "$@"; do
  # Relative paths are relative to where the caller stands, not web/ -
  # including ones that do not exist yet (an --out folder, a build target).
  if [[ "$x" != --* && "$x" != /* && "$x" != [A-Za-z]:* ]] && [[ -e "$x" || "$x" == */* ]]; then
    x="$caller/$x"
  fi
  args+=("$x")
done
cd "$web"
NODE_PATH="$web/node_modules" exec npx tsx --tsconfig ./tsconfig.json "$here/$tool.ts" "${args[@]}"
