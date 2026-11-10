#pragma once
#include <cstddef>
#include <cstdint>

namespace tvk {

/// Configuration options for terminal raw mode, alternate screen, mouse, and Kitty protocol.
struct TerminalConfig {
    bool alternateScreen = true;
    bool hideCursor = true;
    bool mouseTracking = true;
    bool kittyKeyboard = true;

    // Compatibility aliases
    bool enableAlternateScreen = true;
    bool enableKittyKeyboard = true;
    bool enableMouseTracking = true;
};

namespace pal {

bool TryGetTerminalSize(int &out_w, int &out_h);

bool SetupTerminal(const TerminalConfig &config);
bool SetupTerminal(bool mouseTracking = true, bool kittyKeyboard = true);

void RestoreTerminal();

bool IsTerminalSetup();
const TerminalConfig &GetActiveConfig();
const char *GetActiveResumeSeq();
const char *GetActiveResetSeq();

bool ConsumeResizeEvent();

void FastPrint(const char *data, unsigned int size);

uint64_t GetTimestampNs();

bool PollKey(char &out_char);

size_t ReadInput(char *buf, size_t maxLen);

} // namespace pal
} // namespace tvk

