# Specification 01 — Core Foundation, Platform Abstraction Layer (PAL), Input State Machine & Engine Loop

- **Layers Covered**: Layer 0 (`tvk/core`), Layer 1 (`tvk/platform`), Layer 9 (`tvk/app` runtime host)
- **Target Roadmap Phases**: **Baseline Foundation (M1 — Active)** & **Phase 1 (Complete Runtime Host)**
- **Resolved Baseline Defects**: `B-13`, `B-14`, `B-15`, `B-22`, `B-23`, `B-31`, `B-32`
- **Industry Benchmark References**: Sokol (`sokol_gfx` generational handles), Our Machinery (`FrameLinearArena`, `-fno-exceptions` `Result<T,E>`), EnkiTS (`JobSystem::ParallelFor`), Gaffer on Games (`TimestepAccumulator` with render $\alpha$), Notcurses / Termbox2 / Kitty (`TerminalGuard`, `DECSET 2026`, `CSI u` keyboard protocol, SGR 1006 mouse).

---

## 1. Layer 0: Core Primitives (`include/tvk/core/`)

### 1.1 Generational Resource Handles (`include/tvk/core/Handle.h` — Active in Baseline)

Raw pointers across frames risk use-after-free bugs when engine resources (`Mesh`, `Texture`, `Material`, `Sound`) are destroyed and their memory slots are recycled. `Handle<Tag>` packs a **16-bit slot index** and a **16-bit non-zero generation counter** into a 32-bit trivial value (`sizeof(Handle<Tag>) == 4`, `alignof(Handle<Tag>) == 4`).

```cpp
#pragma once
#include <cstdint>
#include <vector>
#include <utility>

namespace tvk {

template <typename Tag>
struct Handle {
    uint32_t id = 0; // Bits [31..16]: generation (valid >= 1); Bits [15..0]: slot index

    constexpr Handle() noexcept = default;
    constexpr explicit Handle(uint32_t rawId) noexcept : id(rawId) {}
    constexpr Handle(uint16_t index, uint16_t generation) noexcept
        : id((static_cast<uint32_t>(generation) << 16u) | static_cast<uint32_t>(index)) {}

    [[nodiscard]] constexpr bool IsValid() const noexcept { return (id >> 16u) != 0u; }
    [[nodiscard]] constexpr uint16_t Index() const noexcept { return static_cast<uint16_t>(id & 0xFFFFu); }
    [[nodiscard]] constexpr uint16_t Generation() const noexcept { return static_cast<uint16_t>(id >> 16u); }

    constexpr bool operator==(const Handle& other) const noexcept { return id == other.id; }
    constexpr bool operator!=(const Handle& other) const noexcept { return id != other.id; }
};

template <typename T, typename Tag>
class HandlePool {
public:
    using HandleType = Handle<Tag>;

    template <typename... Args>
    [[nodiscard]] HandleType Create(Args&&... args) {
        uint16_t idx = 0;
        if (!m_FreeIndices.empty()) {
            idx = m_FreeIndices.back();
            m_FreeIndices.pop_back();
            m_Slots[idx].value = T(std::forward<Args>(args)...);
            m_Slots[idx].alive = true;
        } else {
            if (m_Slots.size() >= 0xFFFFu) return HandleType{};
            idx = static_cast<uint16_t>(m_Slots.size());
            Slot slot{};
            slot.value = T(std::forward<Args>(args)...);
            slot.generation = 1u;
            slot.alive = true;
            m_Slots.push_back(std::move(slot));
        }
        ++m_AliveCount;
        return HandleType(idx, m_Slots[idx].generation);
    }

    bool Destroy(HandleType h) noexcept {
        if (!IsValid(h)) return false;
        const uint16_t idx = h.Index();
        m_Slots[idx].alive = false;
        // Increment generation (wrap 0xFFFF -> 1, never 0 so 0 remains invalid sentinel)
        uint16_t nextGen = static_cast<uint16_t>(m_Slots[idx].generation + 1u);
        if (nextGen == 0u) nextGen = 1u;
        m_Slots[idx].generation = nextGen;
        m_FreeIndices.push_back(idx);
        --m_AliveCount;
        return true;
    }

    [[nodiscard]] bool IsValid(HandleType h) const noexcept {
        if (!h.IsValid()) return false;
        const uint16_t idx = h.Index();
        return idx < m_Slots.size() && m_Slots[idx].alive && m_Slots[idx].generation == h.Generation();
    }

    [[nodiscard]] T* Get(HandleType h) noexcept {
        return IsValid(h) ? &m_Slots[h.Index()].value : nullptr;
    }

    [[nodiscard]] const T* Get(HandleType h) const noexcept {
        return IsValid(h) ? &m_Slots[h.Index()].value : nullptr;
    }

    void Clear() noexcept {
        m_Slots.clear();
        m_FreeIndices.clear();
        m_AliveCount = 0;
    }

    [[nodiscard]] uint32_t Count() const noexcept { return m_AliveCount; }

private:
    struct Slot {
        T value{};
        uint16_t generation = 1u;
        bool alive = false;
    };
    std::vector<Slot> m_Slots;
    std::vector<uint16_t> m_FreeIndices;
    uint32_t m_AliveCount = 0;
};

} // namespace tvk
```

---

### 1.2 Zero-Exception Error Handling (`include/tvk/core/Result.h` — Active in Baseline)

Because TVK Engine compiles with `-fno-exceptions`, all fallible operations return `Result<T, E>`:

```cpp
#pragma once
#include <cstdint>
#include <utility>

namespace tvk {

enum class ErrorCode : uint8_t {
    Ok = 0,
    InvalidArgument,
    FileNotFound,
    IoError,
    ParseError,
    OutOfMemory,
    CapacityExceeded,
    TerminalError,
    Unsupported
};

template <typename T, typename E = ErrorCode>
class [[nodiscard]] Result {
public:
    static Result Ok(T val) {
        Result r;
        r.m_Value = std::move(val);
        r.m_Error = E{};
        r.m_HasValue = true;
        return r;
    }

    static Result Err(E err) {
        Result r;
        r.m_Error = err;
        r.m_HasValue = false;
        return r;
    }

    [[nodiscard]] constexpr bool IsOk() const noexcept { return m_HasValue; }
    [[nodiscard]] constexpr bool IsErr() const noexcept { return !m_HasValue; }
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_HasValue; }

    [[nodiscard]] T& Value() noexcept { return m_Value; }
    [[nodiscard]] const T& Value() const noexcept { return m_Value; }
    [[nodiscard]] T ValueOr(T fallback) const { return m_HasValue ? m_Value : std::move(fallback); }
    [[nodiscard]] constexpr E Error() const noexcept { return m_Error; }

private:
    T m_Value{};
    E m_Error{};
    bool m_HasValue = false;
};

} // namespace tvk
```

---

### 1.3 Crash-Safe Terminal Assertions (`include/tvk/core/Assert.h` — Active in Baseline)

If an assertion fails while the terminal is in raw mode and the alternate screen buffer (`\x1b[?1049h`), calling `std::abort()` directly leaves the user's shell unusable. `TVK_ASSERT` and `TVK_VERIFY` invoke `tvk::pal::RestoreTerminal()` before printing diagnostics to `stderr` and calling `std::abort()`.

```cpp
#pragma once
#include <cstdio>
#include <cstdlib>
#include "tvk/platform/PAL.h"

namespace tvk::detail {
[[noreturn]] inline void AssertFail(const char* expr, const char* msg, const char* file, int line) noexcept {
    tvk::pal::RestoreTerminal();
    std::fprintf(stderr, "\n[TVK ASSERTION FAILED] %s:%d\n  Expression: %s\n  Message:    %s\n",
                 file, line, expr, msg ? msg : "(none)");
    std::fflush(stderr);
    std::abort();
}
} // namespace tvk::detail

#ifndef NDEBUG
#define TVK_ASSERT(expr, msg) \
    do { if (!(expr)) { ::tvk::detail::AssertFail(#expr, (msg), __FILE__, __LINE__); } } while (0)
#else
#define TVK_ASSERT(expr, msg) do { (void)sizeof(expr); } while (0)
#endif

#define TVK_VERIFY(expr, msg) \
    do { if (!(expr)) { ::tvk::detail::AssertFail(#expr, (msg), __FILE__, __LINE__); } } while (0)
```

---

### 1.4 Thread Pool, Portable Spin-Wait & `ParallelFor` (`include/tvk/core/JobSystem.h` — Active in Baseline)

Fixes `B-14`, `B-22`, and `B-23`:
1. **Portable `CpuRelax()`**:
   - On `x86_64`/`i386`: `_mm_pause()` (`<immintrin.h>`)
   - On `aarch64`/`arm64`: `__asm__ __volatile__("yield" ::: "memory")` (or `__yield()` on MSVC ARM64)
   - Fallback: `std::this_thread::yield()`
2. **Constructor Underflow Protection**: `m_NumThreads` is clamped to `std::max(1, numThreads)` so `m_Workers.reserve(m_NumThreads - 1)` never underflows when `numThreads <= 0`.
3. **Happens-Before Memory Ordering**: `m_TaskContext` and `m_TaskCallback` are assigned **inside** `std::lock_guard<std::mutex> lock(m_WakeMutex)` before incrementing `m_JobGeneration`, guaranteeing visibility on weakly-ordered architectures (ARM64).
4. **`ParallelFor(uint32_t count, uint32_t grainSize, const Func& func)`**:
   Partitions `[0, count)` into chunks of `std::max(1u, grainSize)` and dispatches across worker threads using an internal `alignas(64) AtomicQueue`.

---

### 1.5 Frame Linear Arena & Pool Allocator (`include/tvk/core/Memory.h` — Phase 1 Spec)

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <new>
#include <vector>

namespace tvk {

class alignas(64) FrameLinearArena {
public:
    explicit FrameLinearArena(size_t capacityBytes = 2u * 1024u * 1024u)
        : m_Buffer(capacityBytes, 0), m_Capacity(capacityBytes) {}

    FrameLinearArena(const FrameLinearArena&) = delete;
    FrameLinearArena& operator=(const FrameLinearArena&) = delete;

    [[nodiscard]] void* AllocateBytes(size_t sizeBytes, size_t alignment = 64u) noexcept {
        const size_t alignedOffset = (m_Offset + (alignment - 1u)) & ~(alignment - 1u);
        if (alignedOffset + sizeBytes > m_Capacity) {
            ++m_OverflowCount;
            return nullptr;
        }
        void* ptr = m_Buffer.data() + alignedOffset;
        m_Offset = alignedOffset + sizeBytes;
        if (m_Offset > m_PeakOffset) m_PeakOffset = m_Offset;
        return ptr;
    }

    template <typename T>
    [[nodiscard]] T* Allocate(size_t count = 1u) noexcept {
        void* raw = AllocateBytes(sizeof(T) * count, alignof(T) > 16u ? alignof(T) : 16u);
        return static_cast<T*>(raw);
    }

    void Reset() noexcept { m_Offset = 0u; }

    [[nodiscard]] size_t GetUsedBytes() const noexcept { return m_Offset; }
    [[nodiscard]] size_t GetPeakBytes() const noexcept { return m_PeakOffset; }
    [[nodiscard]] size_t GetCapacityBytes() const noexcept { return m_Capacity; }
    [[nodiscard]] uint32_t GetOverflowCount() const noexcept { return m_OverflowCount; }

private:
    std::vector<uint8_t> m_Buffer;
    size_t m_Capacity = 0u;
    size_t m_Offset = 0u;
    size_t m_PeakOffset = 0u;
    uint32_t m_OverflowCount = 0u;
};

} // namespace tvk
```

---

### 1.6 Fixed-Timestep Accumulator with Render Alpha (`include/tvk/core/Time.h` — Phase 1 Spec)

Implements the Gaffer on Games *"Fix Your Timestep!"* accumulator with a spiral-of-death clamp (`maxFrameDelta = 0.25f`, `maxSubSteps = 8`) and normalized interpolation factor $\alpha \in [0, 1)$:

$$\Delta t_{\text{clamped}} = \min(\Delta t_{\text{raw}}, 0.25\text{ s}), \quad \alpha = \frac{\text{accumulator}}{\Delta t_{\text{fixed}}} \in [0.0, 1.0)$$

```cpp
#pragma once
#include <algorithm>
#include <cstdint>

namespace tvk {

struct TimestepAccumulator {
    float fixedDt = 1.0f / 60.0f;
    float maxFrameDt = 0.25f;
    float accumulator = 0.0f;
    uint32_t maxStepsPerFrame = 8u;

    // Adds frame delta and returns the number of fixed steps to execute this frame
    [[nodiscard]] uint32_t Advance(float rawFrameDt) noexcept {
        const float clamped = std::clamp(rawFrameDt, 0.0f, maxFrameDt);
        accumulator += clamped;
        uint32_t steps = 0u;
        while (accumulator >= fixedDt && steps < maxStepsPerFrame) {
            accumulator -= fixedDt;
            ++steps;
        }
        if (accumulator >= fixedDt) {
            accumulator = 0.0f; // Drop excess remainder after maxStepsPerFrame cap
        }
        return steps;
    }

    // Interpolation factor in [0, 1) for smooth rendering between prevTransform and currTransform
    [[nodiscard]] float GetAlpha() const noexcept {
        return (fixedDt > 1e-6f) ? std::clamp(accumulator / fixedDt, 0.0f, 1.0f) : 0.0f;
    }
};

} // namespace tvk
```

---

### 1.7 EventBus & Ring-Buffer Logger (`include/tvk/core/EventBus.h`, `Log.h` — Phase 1 Spec)

- **`EventBus`**: Compile-time type-indexed channel table (`WindowResizeEvent`, `KeyEvent`, `MouseButtonEvent`, `MouseMoveEvent`, `MouseScrollEvent`, `CollisionEvent`) supporting immediate `Publish<E>(const E&)` and queued `Enqueue<E>(const E&)` / `DispatchQueued()`.
- **`Log`**: Fixed 256-entry ring buffer (`struct LogEntry { uint64_t frame; LogLevel level; char message[120]; };`, `sizeof(LogEntry) == 136 B`, total ring = `34.8 KB`) protected by a lightweight spinlock/mutex. **Never writes to `stdout` or `stderr` while the alternate terminal buffer is active**; entries are rendered inside the `F1` `DebugOverlay`.

---

## 2. Layer 1: Platform Abstraction Layer & Input State Machine (`include/tvk/platform/`)

### 2.1 Crash-Safe `PAL` & `TerminalGuard` (`src/platform/PAL.cpp` — Active in Baseline)

#### Startup Sequence (`pal::SetupTerminal()`)
1. **Idempotency Guard**: `static std::atomic<bool> s_TerminalInitialized{false};` ensures multiple calls (`Sandbox.cpp` + `TerminalPresenter`) never overwrite the saved original `struct termios` (`B-31`).
2. **POSIX `termios` Raw Mode (`B-13`)**:
   - Saves original attributes via `tcgetattr(STDIN_FILENO, &s_OrigTermios)`.
   - Applies raw flags:
     ```cpp
     raw.c_iflag &= ~static_cast<tcflag_t>(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
     raw.c_cflag |= static_cast<tcflag_t>(CS8);
     raw.c_lflag &= ~static_cast<tcflag_t>(ECHO | ICANON | IEXTEN);
     raw.c_cc[VMIN] = 0;
     raw.c_cc[VTIME] = 0;
     tcsetattr(STDIN_FILENO, TCSANOW, &raw);
     ```
3. **Signal & Exit Registration**:
   - Installs `std::atexit(RestoreTerminal)` and `sigaction` handlers for `SIGINT`, `SIGTERM`, `SIGABRT`, `SIGSEGV`, and `SIGWINCH` (`volatile sig_atomic_t g_ResizePending = 1`).
4. **ANSI Terminal Initialization Sequence**:
   - Emits `"\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H"` (plus `\x1b[?1000h\x1b[?1002h\x1b[?1006h\x1b[>1u` when mouse and Kitty keyboard modes are enabled in Phase 1).

#### Async-Signal-Safe Shutdown Sequence (`pal::RestoreTerminal()`)
- Invoked on clean exit, `TVK_ASSERT` failure, or fatal signal (`SIGINT`/`SIGTERM`/`SIGABRT`/`SIGSEGV`).
- Uses **only POSIX async-signal-safe functions** (`tcsetattr`, `::write`, `_Exit`):
  ```cpp
  static constexpr char kResetSeq[] =
      "\x1b[?1006l\x1b[?1002l\x1b[?1000l\x1b[<u\x1b[0m\x1b[?25h\x1b[?2026l\x1b[?1049l";
  ::write(STDOUT_FILENO, kResetSeq, sizeof(kResetSeq) - 1);
  tcsetattr(STDIN_FILENO, TCSANOW, &s_OrigTermios);
  ```

---

### 2.2 Hybrid Keyboard & SGR 1006 Mouse Input State Machine (`include/tvk/platform/Input.h` — Phase 1 Spec)

#### Complete `ParserState` Transition Table

Each frame, `Input::Poll(uint64_t nowNs)` performs a single non-blocking `::read(STDIN_FILENO, buf, 4096)` and feeds all bytes through the 7-state finite-state machine:

| Current State | Input Byte(s) | Next State | Action / Emitted Event |
| :--- | :--- | :--- | :--- |
| **`Ground`** | `0x1B` (`ESC`) | `EscSeen` | Record `escTimestampNs`. If at end of read buffer (`i + 1 == bytesRead`), emit `Key::Escape` and return to `Ground`. |
| **`Ground`** | `0x03` (`^C`), `0x11` (`^Q`) | `Ground` | Emit `Key::C` / `Key::Q` with `KeyMod::Ctrl` (triggers graceful engine exit if configured). |
| **`Ground`** | `0x09` (`\t`), `0x0D`/`0x0A` (`\r`/`\n`), `0x7F`/`0x08` | `Ground` | Emit `Key::Tab`, `Key::Enter`, `Key::Backspace`. |
| **`Ground`** | `0x20`..`0x7E` (Printable ASCII) | `Ground` | Map `'a'..'z'`, `'0'..'9'`, punctuation to `Key` enum (`'A'..'Z'` adds `KeyMod::Shift`). |
| **`Ground`** | `0xC0`..`0xF7` (UTF-8 Lead Byte) | `UTF8_MultiByte` | Collect 2–4 UTF-8 continuation bytes (`0x80`..`0xBF`) for text input box widgets. |
| **`EscSeen`** | `0x5B` (`'['`) | `CSI_Param` | Reset CSI numeric parameter array `params[0..3] = {0}` and `paramCount = 0`. |
| **`EscSeen`** | `0x4F` (`'O'`) | `SS3` | Prepare for `F1`–`F4` (`\x1bOP`..`\x1bOS`) or keypad arrows. |
| **`EscSeen`** | Any other byte `b` | `Ground` | Emit `Key` for `b` with `KeyMod::Alt` (`Alt+Key`). |
| **`SS3`** | `'P'`, `'Q'`, `'R'`, `'S'` | `Ground` | Emit `Key::F1`, `Key::F2`, `Key::F3`, `Key::F4`. |
| **`CSI_Param`** | `'<'` (`0x3C`) at start | `SGR_Mouse` | Switch to SGR 1006 Mouse parameter accumulator (`Cb`, `Cx`, `Cy`). |
| **`CSI_Param`** | `'0'`..`'9'` | `CSI_Param` | `params[paramIdx] = params[paramIdx] * 10 + (b - '0')`. |
| **`CSI_Param`** | `';'` or `':'` | `CSI_Param` | Advance `paramIdx` (note `':'` separates Kitty sub-parameters such as event type `1`=press, `2`=repeat, `3`=release). |
| **`CSI_Param`** | `'A'`, `'B'`, `'C'`, `'D'` | `Ground` | Emit `Key::Up`, `Key::Down`, `Key::Right`, `Key::Left` with modifier from `params[1]`. |
| **`CSI_Param`** | `'H'`, `'F'` | `Ground` | Emit `Key::Home`, `Key::End`. |
| **`CSI_Param`** | `'~'` | `Ground` | Map `params[0]`: `1/7->Home`, `2->Insert`, `3->Delete`, `4/8->End`, `5->PageUp`, `6->PageDown`, `15->F5`, `17->F6`, `18->F7`, `19->F8`, `20->F9`, `21->F10`, `23->F11`, `24->F12`. |
| **`CSI_Param`** | `'u'` (Kitty CSI u) | `Ground` | `codepoint = params[0]`, `mod = params[1]`, `eventType = subParam` (`1`=Press, `2`=Repeat, `3`=Release). Directly updates key state (`Down` or `Released`). |
| **`SGR_Mouse`** | `'0'`..`'9'`, `';'` | `SGR_Mouse` | Accumulate `Cb = params[0]`, `Cx = params[1]`, `Cy = params[2]`. |
| **`SGR_Mouse`** | `'M'` (Press/Motion) or `'m'` (Release) | `Ground` | Convert 1-based `(Cx, Cy)` to 0-based cell `(Cx - 1, Cy - 1)`. Decode button/drag/wheel from `Cb`. |

#### Key-Release Synthesis for Legacy VT100/ANSI Terminals
Standard terminals that do not support the Kitty `\x1b[>1u` protocol never emit key-up events. `Input` handles both seamlessly:
- If any `\x1b[...u` sequence with event type `:3u` is received, `m_KittyProtocolActive` is set to `true` and keys remain `Down` until their explicit `:3u` release event arrives.
- When `m_KittyProtocolActive == false`, each key stores `uint64_t lastEventNs`. A key remains `IsKeyDown(k) == true` while `nowNs - lastEventNs <= m_KeyHoldDecayNs` (default `250,000,000 ns` = `250 ms`, refreshed on every OS auto-repeat byte), and transitions to `IsKeyReleased(k) == true` on the first frame after the hold window expires.

---

## 3. Layer 9: Engine Runtime & Application Host (`include/tvk/app/`)

```cpp
#pragma once
#include <cstdint>
#include "tvk/core/Types.h"

namespace tvk {

struct EngineConfig {
    uint32_t targetFps = 60u;
    float fixedTimestep = 1.0f / 60.0f;
    BlitterType initialBlitter = BlitterType::Braille;
    int numWorkerThreads = 0;       // 0 = std::thread::hardware_concurrency()
    bool headless = false;
    uint32_t fallbackWidth = 120u;
    uint32_t fallbackHeight = 40u;
    size_t frameArenaBytes = 2u * 1024u * 1024u;
    bool exitOnEscape = true;
};

class Engine;
class CommandBuffer;
class Canvas2D;
class UIContext;

class Application {
public:
    virtual ~Application() = default;
    virtual bool OnInit(Engine& /*engine*/) { return true; }
    virtual void OnFixedUpdate(float /*fixedDt*/) {}
    virtual void OnUpdate(float /*dt*/) {}
    virtual void OnRender(CommandBuffer& /*cmd*/, float /*alpha*/) {}
    virtual void OnUI(Canvas2D& /*canvas*/, UIContext& /*ui*/) {}
    virtual void OnShutdown() {}
};

} // namespace tvk
```

---

## 4. Verification Criteria (`test_core` & `test_input`)

1. **`Handle<Tag>` & `HandlePool<T, Tag>` (`tests/test_core.cpp`)**:
   - Creating a handle returns `IsValid() == true`, `Generation() == 1`.
   - Destroying a handle invalidates the old handle (`IsValid(oldHandle) == false`, `Get(oldHandle) == nullptr`) and recycles the slot at `Generation() == 2`.
2. **`Result<T, ErrorCode>` (`tests/test_core.cpp`)**:
   - `Result<int>::Ok(42)` satisfies `IsOk() == true`, `Value() == 42`.
   - `Result<int>::Err(ErrorCode::FileNotFound)` satisfies `IsErr() == true`, `ValueOr(-1) == -1`.
3. **`JobSystem` (`tests/test_core.cpp`)**:
   - `JobSystem(0)` clamps to `1` thread without underflow (`B-23`).
   - `ParallelFor(10000, 64, ...)` computes exact parallel reductions across 1, 2, and 4 threads.
4. **`PAL` & `Input` State Machine (`tests/test_core.cpp` / `test_input.cpp`)**:
   - Feeding synthetic byte streams `"\x1b"`, `"\x1b[A"`, `"\x1b[1;5C"`, `"\x1b[15~"`, `"\x1b[<0;40;12M"`, `"\x1b[<0;40;12m"`, and `"\x1b[97;1:3u"` produces the exact expected `Key` and SGR mouse transitions.
