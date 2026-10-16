# FRED: Flipper Resource & Event Driver

An AI-native development toolkit designed to help autonomous coding agents (like Claude Code) reliably research, plan, implement, and debug Flipper Zero applications (FAPs).

## Why FRED?
Generic AI models often hallucinate standard POSIX C behaviors that do not apply to the STM32WB55 microcontroller. Flipper apps require strict adherence to the Furi OS RTOS kernel, specifically:
- **Resource Management:** Symmetrical allocation and teardown of Furi records (`RECORD_GUI`, `RECORD_DIALOGS`).
- **Event-Driven Architecture:** Non-blocking execution using `FuriMessageQueue` and `ViewDispatcher`.
- **Memory Constraints:** Operating within < 64KB of available heap.

FRED bridges the gap between standard C generation and Furi OS constraints by providing a rigid knowledge base, boilerplate templates, and lifecycle-validation scripts specifically tuned for the micro Flipper Build Tool (`ufbt`).

## Initial Roadmap
- [ ] Define `CLAUDE.md` context bounds for downstream AI agents.
- [ ] Create FAP initialization scripts wrapping `ufbt create`.
- [ ] Build C templates for `ViewPort` (simple) and `SceneManager` (complex) GUI architectures.
- [ ] Implement AST/Regex auditing tools for memory leak prevention.

## Usage
*Instructions for loading FRED as a contextual skill in AI agent environments coming soon.*
