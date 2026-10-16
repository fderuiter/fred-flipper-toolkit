# Data logger migration (evaluation input)

Synthetic benchmark workspace for a FRED held-out task. The two API tables in this folder are **synthetic, reduced tables written for this benchmark**, in the same CSV format as `targets/f7/api_symbols.csv`; they are not copies of official tables. The headers they name are synthetic too.

The app `logger.c` was last built against the SDK described by `api_old.csv`, and users run that build. The user wants it to work on firmware whose SDK is described by `api_new.csv`. `sdk-release-notes.md` is the excerpt the user found for the new SDK.

Request: "Update my logger for the new firmware and tell me what will break, including for people who keep the old .fap installed."
