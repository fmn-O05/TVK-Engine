#include "TestHarness.h"
#include "tvk/platform/Input.h"
#include "tvk/platform/TerminalGuard.h"
#include "tvk/platform/PAL.h"
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using namespace tvk;

// ============================================================================
// SUITE 1: VERIFICATION OF THE 4 PREVIOUSLY FAILING VULNERABILITIES
// ============================================================================

// --- Vulnerability 1: Signed Integer Overflow in Decimal Parsing ---
TVK_TEST(Challenger_V1_SignedIntegerOverflowStress) {
    Input input;

    // 1.1 Massive decimal digit sequence in CSI parameter
    input.BeginFrame();
    std::string hugeDigits = "\x1b[";
    for (int i = 0; i < 200; ++i) {
        hugeDigits += "9";
    }
    hugeDigits += ";";
    for (int i = 0; i < 200; ++i) {
        hugeDigits += "9";
    }
    hugeDigits += "m";
    input.Feed(hugeDigits);
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // 1.2 Boundary check around 10^8 and INT_MAX (99999999, 100000000, 2147483647)
    input.BeginFrame();
    input.Feed("\x1b[99999999;100000000;2147483647;999999999999u");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // 1.3 Subparameters with massive digits
    input.BeginFrame();
    input.Feed("\x1b[1:99999999999999999999:888888888888888888u");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // 1.4 SGR Mouse with massive coordinates
    input.BeginFrame();
    input.Feed("\x1b[<999999999999999999;999999999999999999;999999999999999999M");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
    TVK_EXPECT_TRUE(input.GetMousePos().x >= 0);
    TVK_EXPECT_TRUE(input.GetMousePos().y >= 0);

    // 1.5 Rapid interleaved overflow probes
    for (int i = 0; i < 100; ++i) {
        input.BeginFrame();
        std::string s = "\x1b[" + std::to_string(i) + std::string(30, '7') + "A";
        input.Feed(s);
        TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Up));
    }
}

// --- Vulnerability 2: SGR Mouse State Trap on Malformed Sequences ---
TVK_TEST(Challenger_V2_SgrMouseTrapAvoidanceAndRecovery) {
    Input input;

    // 2.1 Malformed SGR followed by printable letters: \x1b[<garbage
    input.BeginFrame();
    input.Feed("\x1b[<garbage");
    // State must NOT be trapped in SGR_Mouse!
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
    // Keystroke 'e' (the last letter of 'garbage') must be processed
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::E));

    // 2.2 Malformed SGR followed by Escape sequence: \x1b[<\x1b[A
    input.BeginFrame();
    input.Feed("\x1b[<");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::SGR_Mouse));
    // ESC arrives while in SGR_Mouse: should recover and process Up Arrow
    input.Feed("\x1b[A");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Up));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // 2.3 Malformed SGR with punctuation: \x1b[<#@!$w
    input.BeginFrame();
    input.Feed("\x1b[<#@!$w");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::W));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // 2.4 Verify letter 'M' after malformed SGR does NOT generate phantom mouse click
    input.BeginFrame();
    input.Feed("\x1b[<invalid");
    input.Feed("M");
    // 'M' should register as Shift+M key, NOT Left Mouse button down!
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::M));
    TVK_EXPECT_FALSE(input.IsMouseButtonDown(MouseButton::Left));

    // 2.5 Truncated SGR across chunk boundaries: \x1b[<0;10 followed by 'w'
    input.BeginFrame();
    input.Feed("\x1b[<0;10");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::SGR_Mouse));
    input.Feed("w");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::W));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
}

// --- Vulnerability 3: CSI Intermediate State Trap on Characters < 0x40 ---
TVK_TEST(Challenger_V3_CsiIntermediateTrapAvoidance) {
    Input input;

    // 3.1 Space (0x20) triggers CSI_Intermediate, then digits arriving
    input.BeginFrame();
    input.Feed("\x1b[ ");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::CSI_Intermediate));
    // Sending digits '1'..'5' must transition back to Ground without trapping
    input.Feed("12345");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Num5));

    // 3.2 Space (0x20) followed by Escape sequence: \x1b[ \x1b[A
    input.BeginFrame();
    input.Feed("\x1b[ ");
    input.Feed("\x1b[A");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Up));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // 3.3 Control characters < 0x20 in CSI_Intermediate (Tab, Enter, Backspace)
    input.BeginFrame();
    input.Feed("\x1b[ ");
    input.Feed("\t");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Tab));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    input.BeginFrame();
    input.Feed("\x1b[ ");
    input.Feed("\n");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Enter));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // 3.4 Punctuation < 0x40 (e.g. '?', ';', '<', '=')
    input.BeginFrame();
    input.Feed("\x1b[ ?z");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Z));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // 3.5 Valid intermediate sequence terminated properly: \x1b[ "p
    input.BeginFrame();
    input.Feed("\x1b[ \"p");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
}

// --- Vulnerability 4: Kitty PUA Functional Key Mapping ---
TVK_TEST(Challenger_V4_KittyPuaFunctionalKeyMapping) {
    Input input;

    struct PuaKeyMapping {
        uint32_t codepoint;
        KeyCode expectedKey;
        const char* name;
    };

    const PuaKeyMapping kMappings[] = {
        {57344, KeyCode::Escape, "Escape"},
        {57345, KeyCode::Enter, "Enter"},
        {57346, KeyCode::Tab, "Tab"},
        {57347, KeyCode::Backspace, "Backspace"},
        {57348, KeyCode::Insert, "Insert"},
        {57349, KeyCode::Delete, "Delete"},
        {57350, KeyCode::Left, "Left"},
        {57351, KeyCode::Right, "Right"},
        {57352, KeyCode::Up, "Up"},
        {57353, KeyCode::Down, "Down"},
        {57354, KeyCode::PageUp, "PageUp"},
        {57355, KeyCode::PageDown, "PageDown"},
        {57356, KeyCode::Home, "Home"},
        {57357, KeyCode::End, "End"},
        {57358, KeyCode::CapsLock, "CapsLock"},
        {57359, KeyCode::ScrollLock, "ScrollLock"},
        {57360, KeyCode::NumLock, "NumLock"},
        {57361, KeyCode::PrintScreen, "PrintScreen"},
        {57362, KeyCode::Pause, "Pause"},
        // Function keys
        {57376, KeyCode::F1, "F1"},
        {57377, KeyCode::F2, "F2"},
        {57378, KeyCode::F3, "F3"},
        {57379, KeyCode::F4, "F4"},
        {57380, KeyCode::F5, "F5"},
        {57381, KeyCode::F6, "F6"},
        {57382, KeyCode::F7, "F7"},
        {57383, KeyCode::F8, "F8"},
        {57384, KeyCode::F9, "F9"},
        {57385, KeyCode::F10, "F10"},
        {57386, KeyCode::F11, "F11"},
        {57387, KeyCode::F12, "F12"},
        // Keypad keys
        {57399, KeyCode::KP0, "KP0"},
        {57400, KeyCode::KP1, "KP1"},
        {57401, KeyCode::KP2, "KP2"},
        {57402, KeyCode::KP3, "KP3"},
        {57403, KeyCode::KP4, "KP4"},
        {57404, KeyCode::KP5, "KP5"},
        {57405, KeyCode::KP6, "KP6"},
        {57406, KeyCode::KP7, "KP7"},
        {57407, KeyCode::KP8, "KP8"},
        {57408, KeyCode::KP9, "KP9"},
        {57409, KeyCode::KPDecimal, "KPDecimal"},
        {57410, KeyCode::KPDivide, "KPDivide"},
        {57411, KeyCode::KPMultiply, "KPMultiply"},
        {57412, KeyCode::KPSubtract, "KPSubtract"},
        {57413, KeyCode::KPAdd, "KPAdd"},
        {57414, KeyCode::KPEnter, "KPEnter"},
        {57415, KeyCode::KPEqual, "KPEqual"},
    };

    for (const auto& m : kMappings) {
        // Press event
        input.BeginFrame();
        std::string pressSeq = "\x1b[" + std::to_string(m.codepoint) + "u";
        input.Feed(pressSeq);
        TVK_EXPECT_TRUE(input.IsKeyPressed(m.expectedKey));
        TVK_EXPECT_TRUE(input.IsKeyDown(m.expectedKey));
        // Up arrow must NOT register as Backspace!
        if (m.expectedKey != KeyCode::Backspace) {
            TVK_EXPECT_FALSE(input.IsKeyPressed(KeyCode::Backspace));
        }

        // Release event via :3u
        input.BeginFrame();
        std::string releaseSeq = "\x1b[" + std::to_string(m.codepoint) + ":3u";
        input.Feed(releaseSeq);
        TVK_EXPECT_TRUE(input.IsKeyReleased(m.expectedKey));
        TVK_EXPECT_FALSE(input.IsKeyDown(m.expectedKey));
    }

    // 4.2 Kitty repeat event (:2u) verification
    input.BeginFrame();
    input.Feed("\x1b[57352:1u"); // Press Up
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Up));
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::Up));

    input.BeginFrame();
    input.Feed("\x1b[57352:2u"); // Repeat Up
    // Key is still down, but wasPressedThisFrame is FALSE!
    TVK_EXPECT_FALSE(input.IsKeyPressed(KeyCode::Up));
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::Up));

    input.BeginFrame();
    input.Feed("\x1b[57352:3u"); // Release Up
    TVK_EXPECT_TRUE(input.IsKeyReleased(KeyCode::Up));
    TVK_EXPECT_FALSE(input.IsKeyDown(KeyCode::Up));
}

// ============================================================================
// SUITE 2: RAPID MOUSE MOVES, DRAGS, AND BOUNDARIES
// ============================================================================
TVK_TEST(Challenger_RapidMouseMovesAndDeltaAccumulation) {
    Input input;

    // 2.1 Intra-frame delta accumulation: 50 small moves in one frame
    input.BeginFrame();
    // Initial pos at (10, 10) -> SGR 1-based (11, 11)
    input.Feed("\x1b[<35;11;11M");
    TVK_EXPECT_EQ(input.GetMousePos().x, 10);
    TVK_EXPECT_EQ(input.GetMousePos().y, 10);

    // Clear frame delta while preserving mouse pos at (10, 10)
    input.BeginFrame();
    TVK_EXPECT_EQ(input.GetMousePos().x, 10);
    TVK_EXPECT_EQ(input.GetMousePos().y, 10);
    TVK_EXPECT_EQ(input.GetMouseMoveDelta().x, 0);
    TVK_EXPECT_EQ(input.GetMouseMoveDelta().y, 0);

    // Now feed 50 moves of (+2, +3) each
    for (int i = 1; i <= 50; ++i) {
        int x = 10 + i * 2;
        int y = 10 + i * 3;
        std::string move = "\x1b[<35;" + std::to_string(x + 1) + ";" + std::to_string(y + 1) + "M";
        input.Feed(move);
    }

    TVK_EXPECT_EQ(input.GetMousePos().x, 110);
    TVK_EXPECT_EQ(input.GetMousePos().y, 160);
    // Total delta accumulated across the frame: 100 in X, 150 in Y
    TVK_EXPECT_EQ(input.GetMouseMoveDelta().x, 100);
    TVK_EXPECT_EQ(input.GetMouseMoveDelta().y, 150);

    // 2.2 Hover move (Cb = 35) must NOT register as button press
    TVK_EXPECT_FALSE(input.IsMouseButtonDown(MouseButton::Left));
    TVK_EXPECT_FALSE(input.IsMouseButtonDown(MouseButton::Right));
    TVK_EXPECT_FALSE(input.IsMouseButtonDown(MouseButton::Middle));
    TVK_EXPECT_FALSE(input.IsMouseButtonDown(MouseButton::Button4));

    // 2.3 Mouse drag with Left Button down (Cb = 32)
    input.BeginFrame();
    // Press Left Button at (110, 160)
    input.Feed("\x1b[<0;111;161M");
    TVK_EXPECT_TRUE(input.IsMouseButtonDown(MouseButton::Left));
    TVK_EXPECT_TRUE(input.IsMouseButtonPressed(MouseButton::Left));

    // Drag move (Cb = 32 = 32 + 0 for Left button)
    input.Feed("\x1b[<32;121;171M");
    TVK_EXPECT_TRUE(input.IsMouseButtonDown(MouseButton::Left));
    TVK_EXPECT_EQ(input.GetMousePos().x, 120);
    TVK_EXPECT_EQ(input.GetMousePos().y, 170);

    // Release Left button (b == 'm')
    input.Feed("\x1b[<0;121;171m");
    TVK_EXPECT_FALSE(input.IsMouseButtonDown(MouseButton::Left));
    TVK_EXPECT_TRUE(input.IsMouseButtonReleased(MouseButton::Left));

    // 2.4 Rapid wheel scroll flood
    input.BeginFrame();
    for (int i = 0; i < 20; ++i) {
        input.Feed("\x1b[<64;10;10M"); // Scroll Up (+1)
    }
    for (int i = 0; i < 5; ++i) {
        input.Feed("\x1b[<65;10;10M"); // Scroll Down (-1)
    }
    // Net wheel delta: +20 - 5 = +15
    TVK_EXPECT_EQ(input.GetMouseWheelDelta(), 15);
}

// ============================================================================
// SUITE 3: UTF-8 SPLIT FEEDS ACROSS READ CHUNKS
// ============================================================================
TVK_TEST(Challenger_Utf8SplitFeedsAndEdgeCases) {
    Input input;

    // 3.1 Japanese Kanji: '日' (U+65E5, UTF-8: \xE6\x97\xA5, 3 bytes)
    // Fed byte by byte across separate Feed() calls
    input.BeginFrame();
    input.Feed(std::string_view("\xE6", 1));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::UTF8_MultiByte));

    input.Feed(std::string_view("\x97", 1));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::UTF8_MultiByte));

    input.Feed(std::string_view("\xA5", 1));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
    TVK_EXPECT_EQ(input.GetLastUtf8Codepoint(), 0x65E5u);
    TVK_EXPECT_TRUE(std::strcmp(input.GetLastUtf8Text(), "\xE6\x97\xA5") == 0);

    // 3.2 Musical symbol G clef (U+1D11E, UTF-8: \xF0\x9D\x84\x9E, 4 bytes)
    // Split into 2 chunks of 2 bytes
    input.BeginFrame();
    input.Feed(std::string_view("\xF0\x9D", 2));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::UTF8_MultiByte));

    input.Feed(std::string_view("\x84\x9E", 2));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
    TVK_EXPECT_EQ(input.GetLastUtf8Codepoint(), 0x1D11Eu);

    // 3.3 Interleaved UTF-8 and ANSI sequences
    input.BeginFrame();
    // Feed half of emoji, then an abort with normal key
    input.Feed(std::string_view("\xF0\x9F", 2));
    // Unexpected ASCII 'x' arrives
    input.Feed("x");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // Next escape sequence works immediately
    input.Feed("\x1b[A");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Up));

    // 3.4 Rapid succession of 1000 2-byte UTF-8 characters
    for (int i = 0; i < 1000; ++i) {
        input.BeginFrame();
        // Greek Alpha U+03B1 (\xCE\xB1)
        input.Feed("\xCE\xB1");
        TVK_EXPECT_EQ(input.GetLastUtf8Codepoint(), 0x03B1u);
    }
}

// ============================================================================
// SUITE 4: EXTREME FUZZING UNDER ASAN / UBSAN
// ============================================================================
TVK_TEST(Challenger_ExtremeFuzzingMultiSeed) {
    Input input;

    // Run 3 different PRNG seeds for diverse adversarial byte streams
    const uint32_t seeds[] = {0x1337BEEF, 0xCAFEBABE, 0x00D00D00};

    constexpr size_t kFuzzBytesPerSeed = 1024 * 1024; // 1 MB per seed = 3 MB total
    std::vector<char> buffer(2048);

    for (uint32_t seed : seeds) {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> dist(0, 255);
        std::uniform_int_distribution<size_t> chunkSizes(1, 2048);

        size_t total = 0;
        auto start = std::chrono::high_resolution_clock::now();

        while (total < kFuzzBytesPerSeed) {
            size_t n = chunkSizes(rng);
            if (total + n > kFuzzBytesPerSeed) n = kFuzzBytesPerSeed - total;

            for (size_t i = 0; i < n; ++i) {
                buffer[i] = static_cast<char>(dist(rng));
            }

            input.BeginFrame();
            input.Feed(std::string_view(buffer.data(), n));
            total += n;
        }

        auto finish = std::chrono::high_resolution_clock::now();
        double sec = std::chrono::duration<double>(finish - start).count();
        std::printf("    [Fuzz Seed 0x%08X] Processed %zu bytes in %.3f s (%.1f MB/s)\n",
                    seed, total, sec, (static_cast<double>(total) / (1024.0 * 1024.0)) / sec);

        // Recover to Ground
        input.Feed("\x1b[M\n");
        TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
    }
}

// ============================================================================
// SUITE 5: TERMINAL GUARD MOVE SEMANTICS & ACTIVE PRESERVATION
// ============================================================================
TVK_TEST(Challenger_TerminalGuardPreservation) {
    TerminalConfig cfg;
    cfg.alternateScreen = false;
    cfg.hideCursor = false;
    cfg.mouseTracking = false;
    cfg.kittyKeyboard = false;

    // Verify guard move assignment: active guard assigned to another active guard
    // must NOT restore terminal or drop active flag!
    TerminalGuard g1(cfg);
    TVK_EXPECT_TRUE(g1.IsActive());

    TerminalGuard g2(cfg);
    TVK_EXPECT_TRUE(g2.IsActive());

    // Move assign g2 into g1
    g1 = std::move(g2);
    TVK_EXPECT_TRUE(g1.IsActive());
    TVK_EXPECT_FALSE(g2.IsActive());

    // Explicit restore
    g1.Restore();
    TVK_EXPECT_FALSE(g1.IsActive());
    // Repeated restore is idempotent
    g1.Restore();
    TVK_EXPECT_FALSE(g1.IsActive());
}

// ============================================================================
// SUITE 6: DECAY TIMER AND KEY RELEASE DISAMBIGUATION
// ============================================================================
TVK_TEST(Challenger_DecayTimerBehavior) {
    Input input;
    input.SetKeyHoldDecaySeconds(0.200); // 200 ms decay

    // 6.1 Legacy keypress without explicit release
    input.BeginFrame();
    input.Feed("\x1b[A"); // Up arrow
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::Up));
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Up));

    // Frame at t = 0.100s: decay time not exceeded
    input.BeginFrame();
    input.Update(0.100);
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::Up));
    TVK_EXPECT_FALSE(input.IsKeyReleased(KeyCode::Up));

    // Frame at t = 0.250s: decay time exceeded -> auto-released
    input.BeginFrame();
    input.Update(0.250);
    TVK_EXPECT_FALSE(input.IsKeyDown(KeyCode::Up));
    TVK_EXPECT_TRUE(input.IsKeyReleased(KeyCode::Up));

    // 6.2 Kitty protocol key with explicit release must NOT decay prematurely
    input.BeginFrame();
    input.Feed("\x1b[97u"); // Kitty 'a' press
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::A));
    TVK_EXPECT_TRUE(input.IsKittyProtocolActive());

    // Advance time far past decay timer: t = 5.0s
    input.BeginFrame();
    input.Update(5.000);
    // Because it's a Kitty key with explicit release, it stays down!
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::A));
    TVK_EXPECT_FALSE(input.IsKeyReleased(KeyCode::A));

    // Now explicit release arrives: \x1b[97:3u
    input.BeginFrame();
    input.Feed("\x1b[97:3u");
    TVK_EXPECT_FALSE(input.IsKeyDown(KeyCode::A));
    TVK_EXPECT_TRUE(input.IsKeyReleased(KeyCode::A));

    // 6.3 While Kitty is active, legacy key still decays (hasExplicitRelease == false)
    input.BeginFrame();
    input.Feed("\x1b[B"); // Down arrow via legacy ANSI
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::Down));

    input.BeginFrame();
    input.Update(6.000); // 1.0s elapsed
    // Must decay even though m_KittyProtocolActive is true!
    TVK_EXPECT_FALSE(input.IsKeyDown(KeyCode::Down));
    TVK_EXPECT_TRUE(input.IsKeyReleased(KeyCode::Down));
}

// ============================================================================
// SUITE 7: ACTION BINDING CAPACITY & EVENT QUEUE BOUNDS
// ============================================================================
TVK_TEST(Challenger_ActionBindingsAndQueueOverflow) {
    Input input;

    // 7.1 Bind 64 actions (capacity limit)
    for (int i = 0; i < 64; ++i) {
        std::string act = "action_" + std::to_string(i);
        input.BindAction(act, KeyCode::Space);
    }
    // Attempt to bind 65th action: should not crash or buffer overflow
    input.BindAction("action_overflow", KeyCode::Space);
    TVK_EXPECT_FALSE(input.IsActionDown("action_overflow"));

    // 7.2 Empty action and oversized action names
    input.BindAction("", KeyCode::Space);
    std::string longAct(50, 'x');
    input.BindAction(longAct, KeyCode::Space);
    TVK_EXPECT_FALSE(input.IsActionDown(""));
    TVK_EXPECT_FALSE(input.IsActionDown(longAct));

    // 7.3 Unbind action
    input.UnbindAction("action_0", KeyCode::Space);
    input.BeginFrame();
    input.Feed(" ");
    TVK_EXPECT_TRUE(input.IsKeyDown(KeyCode::Space));
    TVK_EXPECT_FALSE(input.IsActionDown("action_0"));
    TVK_EXPECT_TRUE(input.IsActionDown("action_1"));

    // 7.4 Event queue bounds (256 max)
    input.BeginFrame();
    for (int i = 0; i < 500; ++i) {
        input.Feed("w");
    }
    TVK_EXPECT_EQ(input.GetEventQueueSize(), 256u);
}

// ============================================================================
// SUITE 8: BYTE-BY-BYTE CSI FRAGMENTATION & STANDALONE ESCAPE
// ============================================================================
TVK_TEST(Challenger_ByteByByteEscapeFragmentation) {
    Input input;

    // 8.1 Lone 0x1B at end-of-buffer is recognized as standalone Escape keypress
    input.BeginFrame();
    input.Feed("\x1b");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Escape));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // 8.2 SGR Mouse starting with CSI prefix "\x1b[<" then fed 1 byte at a time
    input.BeginFrame();
    input.Feed("\x1b[<");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::SGR_Mouse));

    const char mousePayload[] = "0;15;25M";
    for (size_t i = 0; i < sizeof(mousePayload) - 1; ++i) {
        input.Feed(std::string_view(&mousePayload[i], 1));
    }
    TVK_EXPECT_TRUE(input.IsMouseButtonDown(MouseButton::Left));
    TVK_EXPECT_EQ(input.GetMousePos().x, 14);
    TVK_EXPECT_EQ(input.GetMousePos().y, 24);
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // 8.3 Kitty key starting with CSI prefix "\x1b[" then fed 1 byte at a time
    input.BeginFrame();
    input.Feed("\x1b[");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::CSI_Param));

    const char kittyPayload[] = "57352;1u"; // Up Arrow
    for (size_t i = 0; i < sizeof(kittyPayload) - 1; ++i) {
        input.Feed(std::string_view(&kittyPayload[i], 1));
    }
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Up));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
}

int main() {
    return TVK_RUN_ALL_TESTS();
}


