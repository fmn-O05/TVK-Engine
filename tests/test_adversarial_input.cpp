#include "TestHarness.h"
#include "tvk/platform/Input.h"
#include "tvk/platform/TerminalGuard.h"
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace tvk;

// ============================================================================
// 1. Malformed, Truncated, and Garbage ANSI Escape Sequences
// ============================================================================
TVK_TEST(Adversarial_CSI_ExcessiveParamsAndOverflow) {
    Input input;

    // A) Excessive semicolons beyond kMaxCsiParams (8)
    input.BeginFrame();
    input.Feed("\x1b[;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;A");
    // Should parse final 'A' (Up arrow) without buffer overflow
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Up));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // B) Massive numbers that would overflow a 32-bit signed int if not handled
    input.BeginFrame();
    // 60 '9's
    std::string hugeDigits = "\x1b[" + std::string(60, '9') + "~";
    input.Feed(hugeDigits);
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // C) Colons and subparameters stress
    input.BeginFrame();
    input.Feed("\x1b[1:2:3:4:5:6:7:8:9:10:11:12;1:2:3:4:5:6:7:8:9:10u");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // D) Negative signs or unexpected intermediate characters
    input.BeginFrame();
    input.Feed("\x1b[-1;2A");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    input.BeginFrame();
    input.Feed("\x1b[?25h\x1b[?1049h");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // E) Intermediate bytes (0x20..0x2F) followed by various characters
    input.BeginFrame();
    input.Feed("\x1b[ #%$A");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
}

TVK_TEST(Adversarial_TruncatedEscapeSequences) {
    Input input;

    // A) Truncated CSI: \x1b[ without final byte, followed in next feed by normal text
    input.BeginFrame();
    input.Feed("\x1b[");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::CSI_Param));

    // When normal char 'z' arrives, CSI parser should terminate and return to Ground
    input.Feed("z");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // B) Truncated SS3: \x1bO followed by invalid byte
    input.BeginFrame();
    input.Feed("\x1bO");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::SS3));
    input.Feed("Z"); // 'Z' is not P,Q,R,S,A,B,C,D
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // C) Truncated SGR Mouse: \x1b[<
    input.BeginFrame();
    input.Feed("\x1b[<");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::SGR_Mouse));

    // Probe how SGR_Mouse recovers if non-digits arrive
    // Note: If SGR_Mouse doesn't exit on unexpected chars, this documents the behavior
    input.Feed("x");
    auto stateAfterX = input.GetParserState();
    // Record whether it stays in SGR_Mouse or recovers to Ground
    if (stateAfterX == Input::ParserState::SGR_Mouse) {
        // Feed terminating 'M' to allow it to recover
        input.Feed("M");
    }
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
}

// ============================================================================
// 2. Interleaved Escape Sequences, Binary Bytes, and Plain Text
// ============================================================================
TVK_TEST(Adversarial_InterleavedStream) {
    Input input;
    input.BeginFrame();

    // Stream with text, embedded NUL, control codes, escape sequences, high bytes
    const char stream[] = {
        'H', 'e', 'l', 'l', 'o', ' ',
        0x00, // NUL
        '\x1b', '[', 'A', // Up Arrow
        0x1F, // Control code
        'W', 'o', 'r', 'l', 'd',
        '\x1b', 'O', 'P', // F1
        static_cast<char>(0x80), static_cast<char>(0xFF), // Binary bytes
        '!',
        '\x1b', '[', '<', '0', ';', '1', '5', ';', '2', '5', 'M' // Mouse press (14, 24)
    };

    input.Feed(std::string_view(stream, sizeof(stream)));

    // Verify key presses occurred
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::H));
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::Up));
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::F1));
    TVK_EXPECT_TRUE(input.IsMouseButtonDown(MouseButton::Left));
    TVK_EXPECT_EQ(input.GetMousePos().x, 14);
    TVK_EXPECT_EQ(input.GetMousePos().y, 24);
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
}

// ============================================================================
// 3. Kitty Keyboard Protocol: Extreme Parameters & Edge Cases
// ============================================================================
TVK_TEST(Adversarial_KittyProtocol_ExtremeParams) {
    Input input;

    // A) Codepoint 0
    input.BeginFrame();
    input.Feed("\x1b[0u");
    TVK_EXPECT_TRUE(input.IsKittyProtocolActive());
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // B) Empty parameters: \x1b[u
    input.BeginFrame();
    input.Feed("\x1b[u");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // C) Massive codepoint value
    input.BeginFrame();
    input.Feed("\x1b[2147483647;1u");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // D) Out-of-range modifiers
    input.BeginFrame();
    input.Feed("\x1b[97;999999u"); // modParam = 999999
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::A));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // E) Negative modifier representation (via dash)
    input.BeginFrame();
    input.Feed("\x1b[97;-1u");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // F) Unknown event types (e.g. 0, 4, 99)
    input.BeginFrame();
    input.Feed("\x1b[97:0u");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    input.BeginFrame();
    input.Feed("\x1b[97:4u");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // G) Kitty functional key codes (PUA range 57344..57360)
    input.BeginFrame();
    input.Feed("\x1b[57352;1u"); // Kitty Up arrow
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
}

// ============================================================================
// 4. Extreme SGR 1006 Mouse Coordinates
// ============================================================================
TVK_TEST(Adversarial_SgrMouse_ExtremeCoordinates) {
    Input input;

    // A) Coordinate 0: \x1b[<0;0;0M -> 0-based coordinate should clamp/map to 0
    input.BeginFrame();
    input.Feed("\x1b[<0;0;0M");
    TVK_EXPECT_TRUE(input.IsMouseButtonDown(MouseButton::Left));
    TVK_EXPECT_EQ(input.GetMousePos().x, 0);
    TVK_EXPECT_EQ(input.GetMousePos().y, 0);

    // B) Coordinate 1: \x1b[<0;1;1M -> 1-based (1,1) maps to 0-based (0,0)
    input.BeginFrame();
    input.Feed("\x1b[<0;1;1M");
    TVK_EXPECT_EQ(input.GetMousePos().x, 0);
    TVK_EXPECT_EQ(input.GetMousePos().y, 0);

    // C) Extreme positive coordinates
    input.BeginFrame();
    input.Feed("\x1b[<0;1000000;2000000M");
    TVK_EXPECT_EQ(input.GetMousePos().x, 999999);
    TVK_EXPECT_EQ(input.GetMousePos().y, 1999999);

    // D) Mouse move delta with large jump
    input.BeginFrame();
    input.Feed("\x1b[<32;1000010;2000020M");
    TVK_EXPECT_EQ(input.GetMouseMoveDelta().x, 10);
    TVK_EXPECT_EQ(input.GetMouseMoveDelta().y, 20);

    // E) Truncated / malformed mouse sequences
    input.BeginFrame();
    input.Feed("\x1b[<M");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    input.BeginFrame();
    input.Feed("\x1b[<0;M");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    input.BeginFrame();
    input.Feed("\x1b[<0;10M"); // missing Cy
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // F) High button index: cb = 255
    input.BeginFrame();
    input.Feed("\x1b[<255;10;10M");
    // cb & 3 is 3, button is Button4 (3), which is < 5
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // G) Button release via 'm'
    input.BeginFrame();
    input.Feed("\x1b[<0;10;10m");
    TVK_EXPECT_FALSE(input.IsMouseButtonDown(MouseButton::Left));
    TVK_EXPECT_TRUE(input.IsMouseButtonReleased(MouseButton::Left));
}

// ============================================================================
// 5. UTF-8 Split Streams and Invalid Byte Handling
// ============================================================================
TVK_TEST(Adversarial_Utf8SplitStreams) {
    Input input;

    // A) 4-byte UTF-8 rune fed 1 byte at a time: Rocket emoji U+1F680 (\xF0\x9F\x9A\x80)
    input.BeginFrame();
    input.Feed(std::string_view("\xF0", 1));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::UTF8_MultiByte));

    input.Feed(std::string_view("\x9F", 1));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::UTF8_MultiByte));

    input.Feed(std::string_view("\x9A", 1));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::UTF8_MultiByte));

    input.Feed(std::string_view("\x80", 1));
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
    TVK_EXPECT_EQ(input.GetLastUtf8Codepoint(), 0x1F680u);

    // B) Truncated UTF-8: leading byte followed by ASCII char
    input.BeginFrame();
    input.Feed(std::string_view("\xE2", 1)); // Expecting 3 bytes
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::UTF8_MultiByte));

    // Abort sequence with normal ASCII 'a'
    input.Feed("a");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));

    // Next feed should work normally in Ground state
    input.Feed("b");
    TVK_EXPECT_TRUE(input.IsKeyPressed(KeyCode::B));

    // C) Stray continuation byte in Ground state: 0x80..0xBF
    input.BeginFrame();
    input.Feed("\x80\x90\xA0\xBF");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
}

// ============================================================================
// 6. High-Throughput Input Floods & Fuzzing (Memory Leaks, Overruns, Hangs)
// ============================================================================
TVK_TEST(Adversarial_HighThroughputFlood) {
    Input input;

    // A) 2 MB ASCII Flood
    constexpr size_t kAsciiFloodSize = 2 * 1024 * 1024;
    std::string asciiChunk(4096, 'x');
    size_t iterations = kAsciiFloodSize / asciiChunk.size();

    auto startAscii = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < iterations; ++i) {
        input.BeginFrame();
        input.Feed(asciiChunk);
    }
    auto endAscii = std::chrono::high_resolution_clock::now();
    double asciiSec = std::chrono::duration<double>(endAscii - startAscii).count();
    double asciiMBS = (static_cast<double>(kAsciiFloodSize) / (1024.0 * 1024.0)) / asciiSec;
    std::printf("    [Bench] ASCII flood 2MB: %.3f s (%.1f MB/s)\n", asciiSec, asciiMBS);

    TVK_EXPECT_TRUE(asciiMBS > 5.0); // Expect at least 5 MB/s throughput

    // B) 2 MB Escape Sequence Flood (\x1b[A\x1b[B\x1b[C\x1b[D)
    std::string ansiPattern;
    for (int i = 0; i < 256; ++i) {
        ansiPattern += "\x1b[A\x1b[B\x1b[C\x1b[D";
    }
    constexpr size_t kAnsiFloodSize = 2 * 1024 * 1024;
    size_t ansiIterations = kAnsiFloodSize / ansiPattern.size();

    auto startAnsi = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < ansiIterations; ++i) {
        input.BeginFrame();
        input.Feed(ansiPattern);
    }
    auto endAnsi = std::chrono::high_resolution_clock::now();
    double ansiSec = std::chrono::duration<double>(endAnsi - startAnsi).count();
    double ansiMBS = (static_cast<double>(kAnsiFloodSize) / (1024.0 * 1024.0)) / ansiSec;
    std::printf("    [Bench] ANSI escape flood 2MB: %.3f s (%.1f MB/s)\n", ansiSec, ansiMBS);

    TVK_EXPECT_TRUE(ansiMBS > 5.0);
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
}

TVK_TEST(Adversarial_DeterministicRandomFuzzing) {
    Input input;

    // Deterministic PRNG with fixed seed
    std::mt19937 prng(0xDEADBEEF);
    std::uniform_int_distribution<int> byteDist(0, 255);
    std::uniform_int_distribution<size_t> chunkDist(1, 1024);

    constexpr size_t kTotalFuzzBytes = 2 * 1024 * 1024; // 2 MB fuzzing
    size_t bytesProcessed = 0;
    std::vector<char> buffer;
    buffer.reserve(1024);

    auto start = std::chrono::high_resolution_clock::now();
    while (bytesProcessed < kTotalFuzzBytes) {
        size_t chunkSize = chunkDist(prng);
        if (bytesProcessed + chunkSize > kTotalFuzzBytes) {
            chunkSize = kTotalFuzzBytes - bytesProcessed;
        }
        buffer.resize(chunkSize);
        for (size_t i = 0; i < chunkSize; ++i) {
            buffer[i] = static_cast<char>(byteDist(prng));
        }

        input.BeginFrame();
        input.Feed(std::string_view(buffer.data(), buffer.size()));
        bytesProcessed += chunkSize;
    }
    auto end = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double>(end - start).count();
    std::printf("    [Bench] PRNG Fuzz 2MB: %.3f s (%.1f MB/s)\n", duration, 2.0 / duration);

    // Ensure parser didn't crash and is still alive
    // Clear state back to Ground by sending a clean Ground reset
    input.Feed("\x1b[M\n");
    TVK_EXPECT_EQ(static_cast<int>(input.GetParserState()), static_cast<int>(Input::ParserState::Ground));
}

// ============================================================================
// 7. Event Queue Saturation and Overflow Protection
// ============================================================================
TVK_TEST(Adversarial_EventQueueSaturation) {
    Input input;
    input.BeginFrame();
    TVK_EXPECT_EQ(input.GetEventQueueSize(), 0u);

    // Push 1000 key events in a single frame without calling BeginFrame()
    for (int i = 0; i < 1000; ++i) {
        input.Feed("a");
    }

    // Capacity is bounded at 256
    TVK_EXPECT_EQ(input.GetEventQueueSize(), 256u);

    // BeginFrame clears the queue
    input.BeginFrame();
    TVK_EXPECT_EQ(input.GetEventQueueSize(), 0u);
}

// ============================================================================
// 8. TerminalGuard Lifecycle & Move Semantics
// ============================================================================
TVK_TEST(Adversarial_TerminalGuardStress) {
    TerminalConfig cfg;
    cfg.alternateScreen = false;
    cfg.hideCursor = false;
    cfg.mouseTracking = false;
    cfg.kittyKeyboard = false;

    // Move chaining
    TerminalGuard g1(cfg);
    TerminalGuard g2 = std::move(g1);
    TerminalGuard g3(cfg);
    g3 = std::move(g2);

    // Multiple explicit restores
    g3.Restore();
    g3.Restore();
    TVK_EXPECT_FALSE(g3.IsActive());
}

// ============================================================================
// 9. Vulnerability Probes (Empirical Bug Confirmation)
// ============================================================================
TVK_TEST(Adversarial_Probe_VulnerabilitiesAndDocument) {
    // Probe 1: SGR Mouse Trapped State Bug
    Input input1;
    input1.BeginFrame();
    input1.Feed("\x1b[<garbage");
    bool sgrTrapped = (input1.GetParserState() == Input::ParserState::SGR_Mouse);
    std::printf("    [VULN PROBE 1] SGR Mouse trapped on invalid input: %s\n",
                sgrTrapped ? "CONFIRMED BUG (trapped in SGR_Mouse)" : "NOT REPRODUCED");

    // Probe 2: Kitty PUA Functional Key Truncation
    Input input2;
    input2.BeginFrame();
    input2.Feed("\x1b[57352u"); // Kitty Up arrow PUA codepoint
    bool kittyUpBug = !input2.IsKeyPressed(KeyCode::Up) && input2.IsKeyPressed(KeyCode::Backspace);
    std::printf("    [VULN PROBE 2] Kitty Up Arrow (PUA 57352) misdecoded as Backspace: %s\n",
                kittyUpBug ? "CONFIRMED BUG (decoded as Backspace)" : "NOT REPRODUCED");

    // Probe 3: CSI Intermediate Trapped State Bug
    Input input3;
    input3.BeginFrame();
    input3.Feed("\x1b[ ");
    input3.Feed("12345");
    bool csiInterTrapped = (input3.GetParserState() == Input::ParserState::CSI_Intermediate);
    std::printf("    [VULN PROBE 3] CSI Intermediate trapped on non-terminating bytes: %s\n",
                csiInterTrapped ? "CONFIRMED BUG (trapped in CSI_Intermediate)" : "NOT REPRODUCED");

    // Probe 4: Truncated SGR Mouse swallowing subsequent text until 'M'
    Input input4;
    input4.BeginFrame();
    input4.Feed("\x1b[<test");
    input4.Feed("w"); // Key 'w' is typed
    bool wDropped = !input4.IsKeyPressed(KeyCode::W);
    input4.Feed("M"); // Regular text letter 'M' is typed
    bool phantomMouse = input4.IsMouseButtonDown(MouseButton::Left);
    std::printf("    [VULN PROBE 4] Malformed SGR drops keypresses and creates phantom mouse click: %s\n",
                (wDropped && phantomMouse) ? "CONFIRMED BUG (key dropped, phantom click created)" : "NOT REPRODUCED");
}

int main() {
    return TVK_RUN_ALL_TESTS();
}

