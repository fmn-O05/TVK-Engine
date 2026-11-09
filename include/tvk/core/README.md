# `include/tvk/core` — Layer 0: Core Foundation

- **Layer**: Layer 0 (`tvk/core`)
- **Responsibility**: Fundamental value types, generational resource handles, zero-exception error wrappers, crash-safe assertions, frame telemetry, lock-free thread pool (`JobSystem`), linear frame arenas, timestep accumulation, typed event bus, and ring-buffer logging.
- **Dependency Rules**: Depends only on the C++ standard library (with emergency terminal restoration hook resolved at link time). No dependencies on Layers 1–9.
- **Technical Specification**: [`docs/specs/01_CORE_AND_PLATFORM.md`](../../../docs/specs/01_CORE_AND_PLATFORM.md) | [`docs/ARCHITECTURE.md`](../../../docs/ARCHITECTURE.md)

## Active Baseline Headers (`M1`)
- `Types.h` — `Color`, `QualityPreset`, `BlitterType` (`Braille`, `HalfBlock`, `Quadrant`, `Sextant`, `ASCII` + legacy aliases), `TVK_CPU_PAUSE()`, and re-exported `Vec2`/`Vec4`.
- `Handle.h` — 32-bit generational `Handle<Tag>` (`16-bit index` + `16-bit generation`) and `HandlePool<T, Tag>`.
- `Result.h` — Zero-exception `ErrorCode` enum and `[[nodiscard]] Result<T, E = ErrorCode>`.
- `Assert.h` — `TVK_ASSERT(expr, msg)` and `TVK_VERIFY(expr, msg)` invoking `tvk::pal::RestoreTerminal()` before logging diagnostics and calling `std::abort()`.
- `Stats.h` — `FrameStats` (`trianglesSubmitted`, `trianglesCulled`, `trianglesRasterized`, `activeTiles`, `ansiBytesEmitted`, `arenaOverflows`, `frameTimeMs`).
- `JobSystem.h` — Portable `CpuRelax()`, `alignas(64) AtomicQueue`, and `JobSystem` (`ExecuteGrid`, `ParallelFor`).

## Planned Phase 1 Headers
- `Memory.h` — 64-byte aligned `FrameLinearArena` and generational `PoolAllocator`.
- `Time.h` — High-resolution `Clock` and `TimestepAccumulator` (with render interpolation factor $\alpha$).
- `EventBus.h` — Typed synchronous and queued event dispatcher.
- `Log.h` — Thread-safe 256-entry in-memory ring-buffer logger.
