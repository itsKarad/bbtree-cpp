#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$REPO_DIR/build"
ENTRY_COUNT=10000000
DURATION_MS=1000
KEY_KIND=""
KEY_FILE=""

usage() {
  cat <<'EOF'
Usage: scripts/run_workload.sh [--entries N] [--duration-ms N]
       [--keys str-json|wiki-titles --keys-file PATH]

Runs u64 and UUID workloads. An optional key file adds one string-key run.
EOF
}

while (($#)); do
  case "$1" in
    --entries)
      (($# >= 2)) || { usage >&2; exit 2; }
      ENTRY_COUNT="$2"
      shift 2
      ;;
    --duration-ms)
      (($# >= 2)) || { usage >&2; exit 2; }
      DURATION_MS="$2"
      shift 2
      ;;
    --keys)
      (($# >= 2)) || { usage >&2; exit 2; }
      KEY_KIND="$2"
      shift 2
      ;;
    --keys-file)
      (($# >= 2)) || { usage >&2; exit 2; }
      KEY_FILE="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      usage >&2
      exit 2
      ;;
  esac
done

if [[ -n "$KEY_KIND" && -z "$KEY_FILE" ]] ||
   [[ -z "$KEY_KIND" && -n "$KEY_FILE" ]]; then
  usage >&2
  exit 2
fi

cmake -S "$REPO_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --target bbtree_bench --parallel

BENCH="$BUILD_DIR/bbtree_bench"
"$BENCH" "$ENTRY_COUNT" "$DURATION_MS" --keys u64
"$BENCH" "$ENTRY_COUNT" "$DURATION_MS" --keys uuid

if [[ -n "$KEY_KIND" ]]; then
  "$BENCH" "$ENTRY_COUNT" "$DURATION_MS" --keys "$KEY_KIND" --keys-file "$KEY_FILE"
fi
