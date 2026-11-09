#pragma once
#include <cstdint>

namespace tvk {
namespace pal {

bool TryGetTerminalSize(int &out_w, int &out_h);

bool SetupTerminal();

void RestoreTerminal();

bool IsTerminalSetup();

bool ConsumeResizeEvent();

void FastPrint(const char *data, unsigned int size);

uint64_t GetTimestampNs();

bool PollKey(char &out_char);

} // namespace pal
} // namespace tvk
