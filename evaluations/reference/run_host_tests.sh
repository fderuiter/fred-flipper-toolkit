#!/bin/sh
# Reviewer-side host tests for FRED reference solutions (tasks 02, 03, 04).
# Builds each suite with every available compiler configuration and runs it
# against the agent-facing fixture inputs. Usage: sh run_host_tests.sh [outdir]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
FX="$HERE/../fixtures"
OUT=${1:-${TMPDIR:-/tmp}/fred-host-tests}
mkdir -p "$OUT"
status=0
for suite in "02-settings/settings_store.c 02-settings/test_settings_store.c 02-settings/samples" \
             "03-uart/soil_frame.c 03-uart/test_soil_frame.c 03-uart/captures" \
             "04-ir-parser/ir_parser.c 04-ir-parser/test_ir_parser.c 04-ir-parser/corpus"; do
  set -- $suite
  name=${1%%/*}
  for cfg in "gcc -O0 -g -fsanitize=address,undefined" "gcc -O2" "clang -O0" "clang -O2"; do
    cc=${cfg%% *}
    command -v "$cc" >/dev/null 2>&1 || { echo "SKIP $name [$cfg]: $cc not installed"; continue; }
    bin="$OUT/$name-$(echo "$cfg" | tr ' =,' '___')"
    if $cfg -std=c99 -Wall -Wextra -Werror "$HERE/$1" "$HERE/$2" -o "$bin" 2>"$bin.cc.log"; then
      if "$bin" "$FX/$3" >"$bin.log" 2>&1; then
        echo "PASS $name [$cfg]: $(tail -1 "$bin.log")"
      else
        echo "FAIL $name [$cfg]: see $bin.log"; status=1
      fi
    else
      echo "FAIL $name [$cfg]: compile error, see $bin.cc.log"; status=1
    fi
  done
done
exit $status
