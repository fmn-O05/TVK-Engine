#include "TestHarness.h"
#include "tvk/app/Engine.h"
#include "tvk/app/EngineConfig.h"
#include "tvk/core/EventBus.h"
#include "tvk/core/Log.h"
#include "tvk/core/Memory.h"
#include "tvk/core/Time.h"
#include "tvk/platform/Input.h"
#include "tvk/platform/TerminalGuard.h"
#include <cerrno>
#include <csignal>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <unistd.h>

using namespace tvk;

// ============================================================================
// 1. ANSI Arrow Keys & Navigation Keys
// ============================================================================
TVK_TEST(Input_AnsiArrowAndNavKeys) {
    Input input;

    // Up Arrow: \x1b[A
    input.BeginFrame();
    input.Feed("\x1b[A");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Up));
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::Up));

    // Down Arrow: \x1b[B
    input.BeginFrame();
    input.Feed("\x1b[B");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Down));
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::Down));

    // Right Arrow: \x1b[C
    input.BeginFrame();
    input.Feed("\x1b[C");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Right));
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::Right));

    // Left Arrow: \x1b[D
    input.BeginFrame();
    input.Feed("\x1b[D");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Left));
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::Left));

    // Home and End
    input.BeginFrame();
    input.Feed("\x1b[H\x1b[F");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Home));
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::End));

    // PageUp and PageDown
    input.BeginFrame();
    input.Feed("\x1b[5~\x1b[6~");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::PageUp));
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::PageDown));

    // Insert and Delete
    input.BeginFrame();
    input.Feed("\x1b[2~\x1b[3~");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Insert));
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Delete));

    // Modified Arrow: Ctrl + Right Arrow (\x1b[1;5C)
    input.BeginFrame();
    input.Feed("\x1b[1;5C");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Right));
    TVK_EXPECT_TRUE(input.GetActiveModifiers() & KeyMod::Ctrl);
}

// ============================================================================
// 2. Function Keys F1 Through F12
// ============================================================================
TVK_TEST(Input_FunctionKeysF1ThroughF12) {
    Input input;

    // F1..F4 via SS3 (\x1bOP..\x1bOS)
    input.BeginFrame();
    input.Feed("\x1bOP");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::F1));

    input.BeginFrame();
    input.Feed("\x1bOQ");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::F2));

    input.BeginFrame();
    input.Feed("\x1bOR");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::F3));

    input.BeginFrame();
    input.Feed("\x1bOS");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::F4));

    // F5..F12 via CSI ~
    input.BeginFrame();
    input.Feed("\x1b[15~");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::F5));

    input.BeginFrame();
    input.Feed("\x1b[17~");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::F6));

    input.BeginFrame();
    input.Feed("\x1b[18~");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::F7));

    input.BeginFrame();
    input.Feed("\x1b[19~");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::F8));

    input.BeginFrame();
    input.Feed("\x1b[20~");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::F9));

    input.BeginFrame();
    input.Feed("\x1b[21~");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::F10));

    input.BeginFrame();
    input.Feed("\x1b[23~");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::F11));

    input.BeginFrame();
    input.Feed("\x1b[24~");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::F12));
}

// ============================================================================
// 3. Standalone ESC vs Escape Sequence Disambiguation
// ============================================================================
TVK_TEST(Input_EscapeDisambiguation) {
    Input input;

    // Case 1: Standalone ESC at end of buffer
    input.BeginFrame();
    input.Feed("\x1b");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Escape));
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::Escape));

    // Case 2: ESC followed immediately by '[' (arrow sequence) must NOT trigger Escape
    input.BeginFrame();
    input.Feed("\x1b[A");
    TVK_EXPECT_FALSE(input.IsKeyPressed(KeyCode::Escape));
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Up));

    // Case 3: ESC followed by character 'a' triggers Alt+A
    input.BeginFrame();
    input.Feed("\x1b" "a");
    TVK_EXPECT_FALSE(input.IsKeyPressed(KeyCode::Escape));
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::A));
    TVK_EXPECT_TRUE(input.GetActiveModifiers() & KeyMod::Alt);
}

// ============================================================================
// 4. Kitty Keyboard Protocol Parsing
// ============================================================================
TVK_TEST(Input_KittyKeyboardProtocol) {
    Input input;

    // Key Press: 'a' (\x1b[97u)
    input.BeginFrame();
    input.Feed("\x1b[97u");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::A));
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::A));
    TVK_EXPECT_TRUE(input.IsKittyProtocolActive());

    // Subsequent frame without events: key remains down, pressed flag clears
    input.BeginFrame();
    TVK_EXPECT_FALSE(input.IsKeyPressed(KeyCode::A));
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::A));

    // Explicit Key Release via Kitty: 'a' release (\x1b[97:3u)
    input.BeginFrame();
    input.Feed("\x1b[97:3u");
    TVK_EXPECT_FALSE(input.IsKeyDown(KeyCode::A));
    TVK_EXPECT_TRUE(input.IsKeyReleased(KeyCode::A));

    // Kitty Key Press with Modifiers: Ctrl+Shift+'a' (\x1b[97;6:1u)
    input.BeginFrame();
    input.Feed("\x1b[97;6:1u");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::A));
    TVK_EXPECT_TRUE(input.GetActiveModifiers() & KeyMod::Ctrl);
    TVK_EXPECT_TRUE(input.GetActiveModifiers() & KeyMod::Shift);
}

// ============================================================================
// 5. SGR 1006 Mouse Tracking
// ============================================================================
TVK_TEST(Input_Sgr1006MouseTracking) {
    Input input;

    // Left Button Press at 1-based (20, 10) -> 0-based (19, 9)
    input.BeginFrame();
    input.Feed("\x1b[<0;20;10M");
    TVK_EXPECT_TRUE(input.IsMouseButtonDown(MouseButton::Left));
    TVK_EXPECT_TRUE(input.IsMouseButtonPressed(MouseButton::Left));
    TVK_EXPECT_EQ(input.GetMousePos().x, 19);
    TVK_EXPECT_EQ(input.GetMousePos().y, 9);

    // Mouse Drag with Left Button to 1-based (25, 15)
    input.BeginFrame();
    input.Feed("\x1b[<32;25;15M");
    TVK_EXPECT_TRUE(input.IsMouseButtonDown(MouseButton::Left));
    TVK_EXPECT_EQ(input.GetMousePos().x, 24);
    TVK_EXPECT_EQ(input.GetMousePos().y, 14);

    // Left Button Release ('m' terminator)
    input.BeginFrame();
    input.Feed("\x1b[<0;25;15m");
    TVK_EXPECT_FALSE(input.IsMouseButtonDown(MouseButton::Left));
    TVK_EXPECT_TRUE(input.IsMouseButtonReleased(MouseButton::Left));

    // Scroll Wheel Up (\x1b[<64;20;10M)
    input.BeginFrame();
    input.Feed("\x1b[<64;20;10M");
    TVK_EXPECT_EQ(input.GetMouseWheelDelta(), 1);

    // Scroll Wheel Down (\x1b[<65;20;10M)
    input.BeginFrame();
    input.Feed("\x1b[<65;20;10M");
    TVK_EXPECT_EQ(input.GetMouseWheelDelta(), -1);
}

// ============================================================================
// 6. Key-Release Decay Timer Expiry (Legacy Non-Kitty Terminals)
// ============================================================================
TVK_TEST(Input_DecayTimerExpiry) {
    Input input;
    // Set decay timer to 200 ms (0.200s)
    input.SetKeyHoldDecaySeconds(0.200);

    // Feed key press at t = 1.000s
    input.BeginFrame();
    input.Feed("w");
    input.Update(1.000);
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::W));
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::W));

    // Update at t = 1.100s (100 ms later: within decay threshold)
    input.BeginFrame();
    input.Update(1.100);
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::W));
    TVK_EXPECT_FALSE(input.IsKeyReleased(KeyCode::W));

    // Update at t = 1.300s (300 ms later: exceeds 200 ms decay threshold)
    input.BeginFrame();
    input.Update(1.300);
    TVK_EXPECT_FALSE(input.IsKeyDown(KeyCode::W));
    TVK_EXPECT_TRUE(input.IsKeyReleased(KeyCode::W));
}

// ============================================================================
// 7. UTF-8 Multi-Byte Decoding
// ============================================================================
TVK_TEST(Input_Utf8MultiByteDecoding) {
    Input input;

    // 2-byte sequence: \xC3\xA9 ('é' = U+00E9)
    input.BeginFrame();
    input.Feed("\xC3\xA9");
    TVK_EXPECT_EQ(input.GetLastUtf8Codepoint(), 0x00E9u);

    // 3-byte sequence: \xE2\x82\xAC ('€' = U+20AC)
    input.BeginFrame();
    input.Feed("\xE2\x82\xAC");
    TVK_EXPECT_EQ(input.GetLastUtf8Codepoint(), 0x20ACu);

    // 3-byte sequence: \xE2\x96\x88 ('█' = U+2588)
    input.BeginFrame();
    input.Feed("\xE2\x96\x88");
    TVK_EXPECT_EQ(input.GetLastUtf8Codepoint(), 0x2588u);

    // 4-byte sequence: \xF0\x9F\x9A\x80 ('🚀' = U+1F680)
    input.BeginFrame();
    input.Feed("\xF0\x9F\x9A\x80");
    TVK_EXPECT_EQ(input.GetLastUtf8Codepoint(), 0x1F680u);

    // Split feed handling: 2 bytes of a 3-byte sequence followed by final byte
    input.BeginFrame();
    input.Feed("\xE2\x82");
    input.Feed("\xAC");
    TVK_EXPECT_EQ(input.GetLastUtf8Codepoint(), 0x20ACu);
}

// ============================================================================
// 8. Action Bindings
// ============================================================================
TVK_TEST(Input_ActionBindings) {
    Input input;
    input.BindAction("Jump", KeyCode::Space);
    input.BindAction("Fire", KeyCode::LeftControl);

    input.BeginFrame();
    input.Feed(" ");
    TVK_EXPECT_TRUE(input.IsActionPressed("Jump"));
    TVK_EXPECT_TRUE(input.IsActionDown("Jump"));
    TVK_EXPECT_FALSE(input.IsActionDown("Fire"));

    input.UnbindAction("Jump", KeyCode::Space);
    TVK_EXPECT_FALSE(input.IsActionDown("Jump"));
}

// ============================================================================
// 9. FrameLinearArena Bump Allocation & Reset
// ============================================================================
TVK_TEST(Core_FrameLinearArena) {
    FrameLinearArena arena(1024u);
    TVK_EXPECT_EQ(arena.GetCapacityBytes(), 1024u);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 0u);
    TVK_EXPECT_EQ(arena.GetOverflowCount(), 0u);

    // 64-byte alignment check
    void* p1 = arena.AllocateBytes(10u, 64u);
    TVK_EXPECT_TRUE(p1 != nullptr);
    TVK_EXPECT_EQ(reinterpret_cast<uintptr_t>(p1) % 64u, 0u);

    void* p2 = arena.AllocateBytes(20u, 64u);
    TVK_EXPECT_TRUE(p2 != nullptr);
    TVK_EXPECT_EQ(reinterpret_cast<uintptr_t>(p2) % 64u, 0u);
    TVK_EXPECT_TRUE(reinterpret_cast<uintptr_t>(p2) > reinterpret_cast<uintptr_t>(p1));

    // High-water mark check
    size_t peakBefore = arena.GetPeakBytes();
    TVK_EXPECT_TRUE(peakBefore >= arena.GetUsedBytes());

    // Reset check
    arena.Reset();
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 0u);
    TVK_EXPECT_EQ(arena.GetPeakBytes(), peakBefore);

    // Capacity overflow check
    void* overflowPtr = arena.AllocateBytes(2048u, 64u);
    TVK_EXPECT_TRUE(overflowPtr == nullptr);
    TVK_EXPECT_EQ(arena.GetOverflowCount(), 1u);
}

// ============================================================================
// 10. PoolAllocator Generational Recycling
// ============================================================================
TVK_TEST(Core_PoolAllocator) {
    PoolAllocator<int> pool(4u);
    TVK_EXPECT_EQ(pool.Capacity(), 4u);
    TVK_EXPECT_EQ(pool.Count(), 0u);
    TVK_EXPECT_TRUE(pool.IsEmpty());

    auto h0 = pool.Allocate(100);
    auto h1 = pool.Allocate(200);
    auto h2 = pool.Allocate(300);
    auto h3 = pool.Allocate(400);

    TVK_EXPECT_TRUE(pool.IsValid(h0));
    TVK_EXPECT_TRUE(pool.IsValid(h3));
    TVK_EXPECT_EQ(pool.Count(), 4u);
    TVK_EXPECT_TRUE(pool.IsFull());

    // Allocation when full returns invalid handle
    auto h4 = pool.Allocate(500);
    TVK_EXPECT_FALSE(pool.IsValid(h4));

    // Access elements
    TVK_EXPECT_EQ(*pool.Get(h0), 100);
    TVK_EXPECT_EQ(*pool.Get(h2), 300);

    // Free h1
    TVK_EXPECT_TRUE(pool.Free(h1));
    TVK_EXPECT_FALSE(pool.IsValid(h1));
    TVK_EXPECT_EQ(pool.Count(), 3u);

    // Reallocate uses slot 1 with incremented generation
    auto h1_new = pool.Allocate(999);
    TVK_EXPECT_TRUE(pool.IsValid(h1_new));
    TVK_EXPECT_FALSE(pool.IsValid(h1)); // Old handle is stale!
    TVK_EXPECT_EQ(*pool.Get(h1_new), 999);

    // Pointer-based handle lookup
    int* ptr = pool.Get(h1_new);
    TVK_EXPECT_TRUE(ptr != nullptr);
    auto resolvedHandle = pool.GetHandle(ptr);
    TVK_EXPECT_EQ(resolvedHandle, h1_new);
}

// ============================================================================
// 11. TimestepAccumulator Sub-Stepping & Clamping
// ============================================================================
TVK_TEST(Core_TimestepAccumulator) {
    TimestepAccumulator accum;
    accum.fixedDt = 1.0f / 60.0f;
    accum.maxFrameDt = 0.25f;
    accum.maxStepsPerFrame = 8u;

    // Single step
    uint32_t steps = accum.Advance(1.0f / 60.0f);
    TVK_EXPECT_EQ(steps, 1u);
    TVK_EXPECT_NEAR(accum.GetAlpha(), 0.0f, 0.05f);

    // 3 sub-steps
    steps = accum.Advance(3.0f / 60.0f);
    TVK_EXPECT_EQ(steps, 3u);

    // Half step accumulation check for alpha
    steps = accum.Advance(0.5f * (1.0f / 60.0f));
    TVK_EXPECT_EQ(steps, 0u);
    TVK_EXPECT_NEAR(accum.GetAlpha(), 0.5f, 0.05f);

    // Complete the remaining half step
    steps = accum.Advance(0.5f * (1.0f / 60.0f));
    TVK_EXPECT_EQ(steps, 1u);

    // Spiral-of-death clamp: 2.0 second pause clamped to maxFrameDt (0.25s) and 8 steps max
    steps = accum.Advance(2.0f);
    TVK_EXPECT_TRUE(steps <= 8u);
    TVK_EXPECT_TRUE(accum.accumulator < accum.fixedDt);
}

// ============================================================================
// 12. EventBus & Ring-Buffer Logger
// ============================================================================
struct SampleInputEvent {
    int key = 0;
    bool isDown = false;
};

struct SampleResizeEvent {
    int width = 0;
    int height = 0;
};

TVK_TEST(Core_EventBusAndLog) {
    Log::SetEchoToConsole(false);

    // EventBus Immediate Dispatch
    EventBus bus;
    int receivedKey = 0;
    bus.Subscribe<SampleInputEvent>([&](const SampleInputEvent& e) {
        receivedKey = e.key;
    });

    bus.Publish(SampleInputEvent{42, true});
    TVK_EXPECT_EQ(receivedKey, 42);

    // EventBus Queued Dispatch
    int queuedWidth = 0;
    bus.Subscribe<SampleResizeEvent>([&](const SampleResizeEvent& e) {
        queuedWidth = e.width;
    });

    bus.Enqueue(SampleResizeEvent{160, 50});
    TVK_EXPECT_EQ(queuedWidth, 0); // Not yet dispatched

    bus.DispatchQueued();
    TVK_EXPECT_EQ(queuedWidth, 160); // Dispatched!

    // Logger Ring-Buffer
    Log::Clear();
    Log::Print(LogLevel::Info, "Unit test frame %d initialized", 1);
    TVK_EXPECT_EQ(Log::GetEntryCount(), 1u);

    const LogEntry* entry = Log::GetEntry(0);
    TVK_EXPECT_TRUE(entry != nullptr);
    if (entry) {
        TVK_EXPECT_TRUE(std::strstr(entry->message, "Unit test frame 1 initialized") != nullptr);
    }

    // Write 300 entries into 256-capacity ring buffer
    for (int i = 0; i < 300; ++i) {
        Log::Print(LogLevel::Debug, "Log entry %d", i);
    }
    TVK_EXPECT_EQ(Log::GetEntryCount(), 256u); // Bounded to 256

    // DumpToFile verification
    const char* dumpPath = "/tmp/tvk_log_verify.txt";
    ::unlink(dumpPath);
    bool dumpOk = Log::DumpToFile(dumpPath);
    TVK_EXPECT_TRUE(dumpOk);

    FILE* f = std::fopen(dumpPath, "r");
    TVK_EXPECT_TRUE(f != nullptr);
    if (f) {
        char buf[256];
        bool found = false;
        while (std::fgets(buf, sizeof(buf), f)) {
            if (std::strstr(buf, "Log entry")) {
                found = true;
                break;
            }
        }
        std::fclose(f);
        TVK_EXPECT_TRUE(found);
    }
    ::unlink(dumpPath);
}

// ============================================================================
// 13. TerminalGuard Move Semantics & RAII
// ============================================================================
TVK_TEST(Platform_TerminalGuard_MoveSemanticsAndRAII) {
    TerminalConfig cfg;
    cfg.alternateScreen = false;
    cfg.hideCursor = false;
    cfg.mouseTracking = false;
    cfg.kittyKeyboard = false;

    // Test move construction
    TerminalGuard g1(cfg);
    bool initiallyActive = g1.IsActive();
    TerminalGuard g2(std::move(g1));
    TVK_EXPECT_FALSE(g1.IsActive());
    TVK_EXPECT_EQ(g2.IsActive(), initiallyActive);

    // Test move assignment
    TerminalGuard g3(cfg);
    g3 = std::move(g2);
    TVK_EXPECT_FALSE(g2.IsActive());
    TVK_EXPECT_EQ(g3.IsActive(), initiallyActive);

    g3.Restore();
    TVK_EXPECT_FALSE(g3.IsActive());
}

// ============================================================================
// 14. Platform Layer Regression Tests (Gate Remediation Verification)
// ============================================================================

TVK_TEST(Platform_TerminalGuard_MoveAssignmentActivePreservation) {
    TerminalConfig cfg;
    cfg.alternateScreen = false;
    cfg.hideCursor = false;
    cfg.mouseTracking = false;
    cfg.kittyKeyboard = false;

    pal::RestoreTerminal();
    TVK_EXPECT_FALSE(pal::IsTerminalSetup());

    // Create active guard 1
    TerminalGuard g1(cfg);
    TVK_EXPECT_TRUE(g1.IsActive());
    TVK_EXPECT_TRUE(pal::IsTerminalSetup());

    // Create active guard 2
    TerminalGuard g2(cfg);
    TVK_EXPECT_TRUE(g2.IsActive());
    TVK_EXPECT_TRUE(pal::IsTerminalSetup());

    // Move-assign g2 into g1: MUST preserve raw mode and active terminal!
    g1 = std::move(g2);
    TVK_EXPECT_TRUE(g1.IsActive());
    TVK_EXPECT_FALSE(g2.IsActive());
    TVK_EXPECT_TRUE(pal::IsTerminalSetup()); // CRITICAL: must remain active!

    // Destruction or explicit restore of g1 restores the terminal
    g1.Restore();
    TVK_EXPECT_FALSE(g1.IsActive());
    TVK_EXPECT_FALSE(pal::IsTerminalSetup());
}

TVK_TEST(Platform_TerminalGuard_MoveAssignmentFromInactive) {
    TerminalConfig cfg;
    cfg.alternateScreen = false;
    cfg.hideCursor = false;
    cfg.mouseTracking = false;
    cfg.kittyKeyboard = false;

    pal::RestoreTerminal();

    TerminalGuard gActive(cfg);
    TVK_EXPECT_TRUE(gActive.IsActive());
    TVK_EXPECT_TRUE(pal::IsTerminalSetup());

    TerminalGuard gInactive;
    gInactive.Restore(); // Ensure inactive
    TVK_EXPECT_FALSE(gInactive.IsActive());

    // Move assigning inactive into active must restore terminal
    gActive = std::move(gInactive);
    TVK_EXPECT_FALSE(gActive.IsActive());
    TVK_EXPECT_FALSE(pal::IsTerminalSetup()); // Successfully restored!
}

TVK_TEST(Platform_TerminalConfig_ParameterHonoring) {
    pal::RestoreTerminal();

    // Test 1: Config with all features disabled
    {
        TerminalConfig cfg;
        cfg.alternateScreen = false;
        cfg.hideCursor = false;
        cfg.mouseTracking = false;
        cfg.kittyKeyboard = false;

        TerminalGuard guard(cfg);
        TVK_EXPECT_TRUE(guard.IsActive());
        TVK_EXPECT_FALSE(pal::GetActiveConfig().alternateScreen);
        TVK_EXPECT_FALSE(pal::GetActiveConfig().hideCursor);
        TVK_EXPECT_FALSE(pal::GetActiveConfig().mouseTracking);
        TVK_EXPECT_FALSE(pal::GetActiveConfig().kittyKeyboard);

        // Verify emitted resume sequence does not contain alternate screen or cursor hide
        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResumeSeq(), "1049h") == nullptr);
        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResumeSeq(), "25l") == nullptr);
        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResumeSeq(), "1000h") == nullptr);
        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResumeSeq(), ">1u") == nullptr);

        // Verify emitted reset sequence does not contain alternate screen restore or cursor show
        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResetSeq(), "1049l") == nullptr);
        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResetSeq(), "25h") == nullptr);
        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResetSeq(), "1006l") == nullptr);
        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResetSeq(), "<u") == nullptr);
    }

    // Test 2: Config with features enabled
    {
        TerminalConfig cfg;
        cfg.alternateScreen = true;
        cfg.hideCursor = true;
        cfg.mouseTracking = true;
        cfg.kittyKeyboard = true;

        TerminalGuard guard(cfg);
        TVK_EXPECT_TRUE(guard.IsActive());
        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResumeSeq(), "1049h") != nullptr);
        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResumeSeq(), "25l") != nullptr);
        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResumeSeq(), "1000h") != nullptr);
        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResumeSeq(), ">1u") != nullptr);

        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResetSeq(), "1049l") != nullptr);
        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResetSeq(), "25h") != nullptr);
        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResetSeq(), "1006l") != nullptr);
        TVK_EXPECT_TRUE(std::strstr(pal::GetActiveResetSeq(), "<u") != nullptr);
    }
}

TVK_TEST(Platform_SignalHandler_ErrnoPreservation) {
    errno = 12345;
    raise(SIGCONT);
    TVK_EXPECT_EQ(errno, 12345);
}

// ============================================================================
// 15. REGRESSION: FrameLinearArena Integer Wrap-Around & Alignment Bounds
// ============================================================================
TVK_TEST(Core_FrameLinearArena_IntegerWrapRegression) {
    constexpr size_t kArenaSize = 1024u * 1024u; // 1 MB
    FrameLinearArena arena(kArenaSize);

    // 15.1 Allocate(SIZE_MAX) must return nullptr and increment overflow
    void* pWrapMax = arena.Allocate(std::numeric_limits<size_t>::max(), 64u);
    TVK_EXPECT_TRUE(pWrapMax == nullptr);
    TVK_EXPECT_EQ(arena.GetOverflowCount(), 1u);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 0u);

    // 15.2 Allocate(SIZE_MAX - 64) must return nullptr
    void* pWrapNearMax = arena.Allocate(std::numeric_limits<size_t>::max() - 64u, 64u);
    TVK_EXPECT_TRUE(pWrapNearMax == nullptr);
    TVK_EXPECT_EQ(arena.GetOverflowCount(), 2u);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 0u);

    // 15.3 Non-zero offset wrap: Allocate 128 bytes, then attempt wrapped allocation
    void* pValid1 = arena.Allocate(128u, 64u);
    TVK_EXPECT_TRUE(pValid1 != nullptr);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 128u);

    // If offset = 128 and bytes = SIZE_MAX - 100, unchecked addition would wrap to 28 <= capacity
    void* pWrapWithOffset = arena.Allocate(std::numeric_limits<size_t>::max() - 100u, 64u);
    TVK_EXPECT_TRUE(pWrapWithOffset == nullptr);
    TVK_EXPECT_EQ(arena.GetOverflowCount(), 3u);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 128u); // UsedBytes MUST NOT roll backwards!

    // 15.4 Typed Allocate<T> wrap: count * sizeof(T) exceeding SIZE_MAX
    uint64_t* pTypedWrap = arena.Allocate<uint64_t>((std::numeric_limits<size_t>::max() / sizeof(uint64_t)) + 1u);
    TVK_EXPECT_TRUE(pTypedWrap == nullptr);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 128u);

    // 15.5 Valid subsequent allocation must still succeed
    void* pValid2 = arena.Allocate(64u, 64u);
    TVK_EXPECT_TRUE(pValid2 != nullptr);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 192u);
}

// ============================================================================
// 16. REGRESSION: TimestepAccumulator NaN, Inf, and Negative Poisoning
// ============================================================================
TVK_TEST(Core_TimestepAccumulator_NaNAndInfRegression) {
    TimestepAccumulator accum;
    accum.fixedDt = 1.0f / 60.0f;
    accum.maxFrameDt = 0.25f;
    accum.maxStepsPerFrame = 8u;

    // 16.1 Feed NaN: must return 0 steps and NOT poison accumulator
    const float nanVal = std::numeric_limits<float>::quiet_NaN();
    uint32_t steps = accum.Advance(nanVal);
    TVK_EXPECT_EQ(steps, 0u);
    TVK_EXPECT_FALSE(std::isnan(accum.GetAccumulator()));
    TVK_EXPECT_EQ(accum.GetAccumulator(), 0.0f);
    TVK_EXPECT_FALSE(std::isnan(accum.GetAlpha()));
    TVK_EXPECT_EQ(accum.GetAlpha(), 0.0f);

    // 16.2 Subsequent legitimate frame MUST succeed (proves simulation does not permanently freeze)
    steps = accum.Advance(1.0f / 60.0f);
    TVK_EXPECT_EQ(steps, 1u);
    TVK_EXPECT_FALSE(std::isnan(accum.GetAccumulator()));
    TVK_EXPECT_FALSE(std::isnan(accum.GetAlpha()));

    // 16.3 Feed +Infinity: must be sanitized to 0.0f without crashing
    const float infVal = std::numeric_limits<float>::infinity();
    steps = accum.Advance(infVal);
    TVK_EXPECT_EQ(steps, 0u);
    TVK_EXPECT_FALSE(std::isnan(accum.GetAccumulator()));
    TVK_EXPECT_FALSE(std::isinf(accum.GetAccumulator()));

    // 16.4 Feed negative delta time: must be sanitized to 0.0f
    steps = accum.Advance(-10.0f);
    TVK_EXPECT_EQ(steps, 0u);
    TVK_EXPECT_TRUE(accum.GetAccumulator() >= 0.0f);

    // 16.5 Self-healing: if accumulator was somehow already poisoned, next step clears it
    accum.accumulator = nanVal;
    steps = accum.Advance(1.0f / 60.0f);
    TVK_EXPECT_EQ(steps, 1u);
    TVK_EXPECT_FALSE(std::isnan(accum.GetAccumulator()));
    TVK_EXPECT_FALSE(std::isnan(accum.GetAlpha()));
}

// ============================================================================
// 17. REGRESSION: Double ESC Disambiguation & Alt Lock Prevention
// ============================================================================
TVK_TEST(Input_DoubleEscapeSequenceRegression) {
    Input input;

    // 17.1 Feed double ESC: \x1b\x1b
    input.BeginFrame();
    input.Feed("\x1b\x1b");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Escape));
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::Escape));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // 17.2 In next frame, feed 'a'
    // State machine MUST have returned to Ground; 'a' must be plain 'a', NOT Alt+A!
    input.BeginFrame();
    input.Feed("a");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::A));
    TVK_EXPECT_FALSE(HasMod(input.GetActiveModifiers(), KeyMod::Alt));

    // 17.3 Consecutive in same stream: \x1b\x1b followed by a
    input.BeginFrame();
    input.Feed("\x1b\x1b" "a");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Escape));
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::A));
    TVK_EXPECT_FALSE(HasMod(input.GetActiveModifiers(), KeyMod::Alt));
}

// ============================================================================
// 18. REGRESSION: Kitty Keyboard Protocol Functional Key PUA Decoding
// ============================================================================
TVK_TEST(Input_KittyFunctionalKeyPuaRegression) {
    Input input;

    // Up Arrow (PUA 57352 = 0xE008). Must NOT truncate to 0x08 (Backspace)!
    input.BeginFrame();
    input.Feed("\x1b[57352u");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Up));
    TVK_EXPECT_FALSE(input.IsKeyPressed(KeyCode::Backspace));

    // Down Arrow (PUA 57353 = 0xE009)
    input.BeginFrame();
    input.Feed("\x1b[57353u");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Down));

    // Left Arrow (PUA 57350 = 0xE006)
    input.BeginFrame();
    input.Feed("\x1b[57350u");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Left));

    // Right Arrow (PUA 57351 = 0xE007)
    input.BeginFrame();
    input.Feed("\x1b[57351u");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Right));

    // Home (PUA 57356 = 0xE00C) and End (PUA 57357 = 0xE00D)
    input.BeginFrame();
    input.Feed("\x1b[57356u\x1b[57357u");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Home));
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::End));

    // Enter (PUA 57345) and Tab (PUA 57346)
    input.BeginFrame();
    input.Feed("\x1b[57345u\x1b[57346u");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Enter));
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Tab));
}

// ============================================================================
// 19. REGRESSION: SGR 1006 Mouse Motion Without Buttons Held (Cb = 35) & Delta Accumulation
// ============================================================================
TVK_TEST(Input_Sgr1006MotionCb35AndDeltaAccumulationRegression) {
    Input input;

    // Initial position at (10, 10) via hover motion (Cb = 35)
    input.BeginFrame();
    input.Feed("\x1b[<35;11;11M");

    // SGR 1006 mouse motion at (16, 13): Cb = 35 (32 | 3) -> 0-based (15, 12)
    input.BeginFrame();
    input.Feed("\x1b[<35;16;13M");

    // Position updated to 0-based (15, 12)
    TVK_EXPECT_EQ(input.GetMousePos().x, 15);
    TVK_EXPECT_EQ(input.GetMousePos().y, 12);
    TVK_EXPECT_EQ(input.GetMouseMoveDelta().x, 5);
    TVK_EXPECT_EQ(input.GetMouseMoveDelta().y, 2);

    // Cb = 35 must NOT decode as MouseButton::Button4!
    TVK_EXPECT_FALSE(input.IsMouseButtonDown(MouseButton::Button4));
    TVK_EXPECT_FALSE(input.IsMouseButtonPressed(MouseButton::Button4));
    TVK_EXPECT_FALSE(input.IsMouseButtonDown(MouseButton::Left));
    TVK_EXPECT_FALSE(input.IsMouseButtonDown(MouseButton::Right));
    TVK_EXPECT_FALSE(input.IsMouseButtonDown(MouseButton::Middle));

    // Verify event queue event has heldButton == MouseButton::None
    bool foundMouseMove = false;
    const InputEvent* events = input.GetEventQueue();
    for (size_t i = 0; i < input.GetEventQueueSize(); ++i) {
        if (events[i].type == InputEvent::Type::MouseMove) {
            TVK_EXPECT_EQ(events[i].mouseMove.heldButton, MouseButton::None);
            foundMouseMove = true;
            break;
        }
    }
    TVK_EXPECT_TRUE(foundMouseMove);

    // Second motion packet within the same frame to (19, 17) -> 0-based (18, 16)
    // Cumulative delta should be (5 + 3, 2 + 4) = (8, 6)
    input.Feed("\x1b[<35;19;17M");
    TVK_EXPECT_EQ(input.GetMousePos().x, 18);
    TVK_EXPECT_EQ(input.GetMousePos().y, 16);
    TVK_EXPECT_EQ(input.GetMouseMoveDelta().x, 8);
    TVK_EXPECT_EQ(input.GetMouseMoveDelta().y, 6);
}

// ============================================================================
// 20. REGRESSION: Kitty Keyboard Protocol Key Repeat (:2u)
// ============================================================================
TVK_TEST(Input_KittyKeyRepeatRegression) {
    Input input;

    // 20.1 Initial Press: \x1b[97;1:1u (Press 'a')
    input.BeginFrame();
    input.Feed("\x1b[97;1:1u");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::A));
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::A));

    // 20.2 Repeat Event: \x1b[97;1:2u (Repeat 'a')
    input.BeginFrame();
    input.Feed("\x1b[97;1:2u");
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::A));
    // Repeat must NOT set single-frame initial press edge
    TVK_EXPECT_FALSE(input.IsKeyPressed(KeyCode::A));

    // Verify event queue contains KeyEventType::Repeat
    bool foundRepeatEvent = false;
    const InputEvent* events = input.GetEventQueue();
    for (size_t i = 0; i < input.GetEventQueueSize(); ++i) {
        if (events[i].type == InputEvent::Type::Key &&
            events[i].key.code == KeyCode::A &&
            events[i].key.type == KeyEventType::Repeat) {
            foundRepeatEvent = true;
            break;
        }
    }
    TVK_EXPECT_TRUE(foundRepeatEvent);

    // 20.3 Release Event: \x1b[97;1:3u (Release 'a')
    input.BeginFrame();
    input.Feed("\x1b[97;1:3u");
    TVK_EXPECT_FALSE(input.IsKeyDown(KeyCode::A));
    TVK_EXPECT_TRUE(input.IsKeyReleased(KeyCode::A));
}

// ============================================================================
// 21. REGRESSION: Legacy Decay Timer While Kitty Protocol Is Active
// ============================================================================
TVK_TEST(Input_DecayTimerWithKittyActiveRegression) {
    Input input;
    input.SetKeyHoldDecaySeconds(0.200);

    // Feed Kitty press -> activates Kitty protocol
    input.BeginFrame();
    input.Feed("\x1b[97u");
    TVK_EXPECT_TRUE(input.IsKittyProtocolActive());
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::A));

    // Feed legacy Up arrow press at t = 1.0s
    input.BeginFrame();
    input.Feed("\x1b[A");
    input.Update(1.000);
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::Up));

    // At t = 1.300s (300 ms later): Up arrow must decay because it has no Kitty release
    input.BeginFrame();
    input.Update(1.300);
    TVK_EXPECT_FALSE(input.IsKeyDown(KeyCode::Up));
    TVK_EXPECT_TRUE(input.IsKeyReleased(KeyCode::Up));

    // Meanwhile, Kitty 'a' still has explicit release capability and remains down!
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::A));

    // Explicit release for 'a' arrives
    input.BeginFrame();
    input.Feed("\x1b[97:3u");
    TVK_EXPECT_FALSE(input.IsKeyDown(KeyCode::A));
}

// ============================================================================
// 22. REGRESSION: Engine::Run Per-Frame Queued Event Dispatch
// ============================================================================
struct TestLifecycleQueuedEvent {
    int payload = 0;
};

TVK_TEST(Core_Engine_QueuedEventDispatchRegression) {
    EngineConfig cfg;
    cfg.headless = true;
    cfg.targetFps = 0; // Uncapped for fast test

    Engine engine(cfg);

    struct HeadlessAutoExitApp : public Application {
        Engine* enginePtr = nullptr;
        int receivedPayload = 0;
        int frameCount = 0;

        bool OnInit(Engine& engine) override {
            enginePtr = &engine;
            engine.GetEventBus().Subscribe<TestLifecycleQueuedEvent>([this](const TestLifecycleQueuedEvent& e) {
                receivedPayload = e.payload;
            });
            engine.GetEventBus().Enqueue(TestLifecycleQueuedEvent{999});
            return true;
        }

        void OnUpdate(double) override {
            ++frameCount;
            if (frameCount >= 2) {
                enginePtr->RequestExit();
            }
        }
    };

    HeadlessAutoExitApp autoApp;
    bool ok = engine.Run(autoApp);
    TVK_EXPECT_TRUE(ok);
    TVK_EXPECT_EQ(autoApp.receivedPayload, 999);
}

int main() {
    return TVK_RUN_ALL_TESTS();
}
