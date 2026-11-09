# `src/core` — Layer 0 Implementation (`tvk/core`)

- **Layer**: Layer 0 (`tvk/core`)
- **Public Headers**: [`include/tvk/core/`](../../include/tvk/core/README.md)
- **Technical Specification**: [`docs/specs/01_CORE_AND_PLATFORM.md`](../../docs/specs/01_CORE_AND_PLATFORM.md)

## Active Baseline Sources (`M1`)
- `JobSystem.cpp` — Worker thread pool lifecycle, clamped `m_NumThreads >= 1` (`B-23`), spin-then-sleep wake loop with portable `CpuRelax()` (`B-22`), and barrier synchronization.

## Planned Phase 1 Sources
- `Log.cpp` — Thread-safe 256-entry ring-buffer logger implementation.
