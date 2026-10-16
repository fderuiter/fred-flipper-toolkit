"""Replay the source-ordering step of fbt's GatherSources() (issue #17).

Upstream (firmware 9e810dc, scripts/fbt_tools/sconsrecursiveglob.py) does
`sources_list = list(set(Flatten(sources_list)))` on the manifest's patterns plus
"!lib", then globs each include pattern in that order. Set order of str follows
the per-process hash seed, so object (and link) order varies unless
PYTHONHASHSEED is fixed. Run with -s, not -I: -I also ignores PYTHONHASHSEED.
"""
import sys

patterns = sys.argv[1:] or ["soil_uart.c", "soil_frame.c"]
flat = patterns + ["!lib"]
print(",".join(x for x in list(set(flat)) if not x.startswith("!")))
