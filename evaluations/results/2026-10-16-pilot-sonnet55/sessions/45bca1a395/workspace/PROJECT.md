# Meter app migration (evaluation input)

Synthetic benchmark workspace for FRED task 07. The two API tables in this folder are **synthetic, reduced tables written for this benchmark**, in the same CSV format as `targets/f7/api_symbols.csv`; they are not copies of official tables.

The app `meter.c` was last built against the SDK described by `api_old.csv`. The user wants it to work on firmware whose SDK is described by `api_new.csv`.

Request: "Update my app for the new firmware and tell me what will break."

`meter_extra.h` was copied from a community firmware fork's SDK by a previous contributor.
