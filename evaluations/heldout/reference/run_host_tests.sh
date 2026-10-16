#!/bin/sh
# Reviewer-side host tests for FRED held-out reference solutions (H01-H04).
# Builds each suite with every available compiler configuration and runs it
# against the agent-facing held-out fixture inputs. Usage: sh run_host_tests.sh [outdir]
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
FX="$HERE/../fixtures"
OUT=${1:-${TMPDIR:-/tmp}/fred-heldout-host-tests}
mkdir -p "$OUT"
status=0
for suite in "h01-stopwatch/stopwatch_core.c h01-stopwatch/test_stopwatch_core.c h01-stopwatch" \
             "h02-pomodoro/pomodoro_settings.c h02-pomodoro/test_pomodoro_settings.c h02-pomodoro/samples" \
             "h03-i2c/hx40.c h03-i2c/test_hx40.c h03-i2c/captures" \
             "h04-sub-parser/sub_parser.c h04-sub-parser/test_sub_parser.c h04-sub-parser/corpus"; do
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
