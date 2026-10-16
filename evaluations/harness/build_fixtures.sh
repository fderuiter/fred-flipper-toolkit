#!/bin/sh
# Build designated FRED fixtures with uFBT against the SDK selected in UFBT_HOME,
# run ufbt lint (formatting/naming only), and print each .fap SHA-256.
# Usage: UFBT_HOME=<isolated dir> PATH=<venv with ufbt>:$PATH sh build_fixtures.sh <outdir>
# Hashes are only comparable across runs with the same WORK and UFBT_HOME paths and a fixed
# PYTHONHASHSEED (issue #17; evaluations/results/2026-10-16-build-reproducibility).
: "${UFBT_HOME:?set UFBT_HOME to the isolated SDK state directory}"
export PYTHONHASHSEED="${PYTHONHASHSEED:-0}"
echo "WORK=${WORK:-/tmp/fred-sdk} UFBT_HOME=$UFBT_HOME PYTHONHASHSEED=$PYTHONHASHSEED"
R=$(cd "$(dirname "$0")/.." && pwd)
OUT=${1:?output directory}
rm -rf ${WORK:-/tmp/fred-sdk}/apps "$OUT"; mkdir -p ${WORK:-/tmp/fred-sdk}/apps "$OUT"
for spec in reference/01-utility:fred_tally reference/02-settings:fred_interval_timer reference/03-uart:fred_soil_uart reference/04-ir-parser:fred_ir_viewer reference/06-lifecycle-fixed:uart_logger_fixed fixtures/06-lifecycle-review:uart_logger_eval; do
  src=${spec%%:*}; id=${spec##*:}
  cp -R "$R/$src" "${WORK:-/tmp/fred-sdk}/apps/$id"
  cd "${WORK:-/tmp/fred-sdk}/apps/$id"
  ufbt > "$OUT/$id.build.log" 2>&1; b=$?
  ufbt lint > "$OUT/$id.lint.log" 2>&1; l=$?
  h=$(sha256sum dist/$id.fap 2>/dev/null | cut -d' ' -f1)
  w=$(grep -c 'warning:' "$OUT/$id.build.log")
  api=$(grep -o 'Target: [0-9]*, API: [0-9.]*' "$OUT/$id.build.log" | tail -1)
  echo "$id build=$b lint=$l warnings=$w [$api] sha256=$h"
done
