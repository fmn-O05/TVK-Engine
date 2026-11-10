#pragma once
#include "tvk/core/Types.h"
#include <cstddef>
#include <cstdint>

namespace tvk {

/// Declarative startup configuration for TVK Engine runtime.
struct EngineConfig {
    // Frame Timing & Simulation Pacing
    uint32_t targetFps = 60u;
    double fixedTimestep = 1.0 / 60.0;

    // Terminal & Display Parameters
    BlitterType initialBlitter = BlitterType::Braille;
    QualityPreset initialQuality = QualityPreset::Balanced;
    uint32_t fallbackWidth = 120u;
    uint32_t fallbackHeight = 40u;
    bool headless = false;

    // Input Protocols & Behavior
    bool enableMouse = true;
    bool enableKittyKeyboard = true;
    bool exitOnEscape = true;
    bool exitOnQ = true;

    // Memory & Concurrency
    size_t frameArenaCapacity = 2u * 1024u * 1024u;
    int numWorkerThreads = 0;
};

} // namespace tvk
