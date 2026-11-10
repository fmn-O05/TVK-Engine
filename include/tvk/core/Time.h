#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include "tvk/platform/PAL.h"

namespace tvk {

// ============================================================================
// 1. Monotonic High-Resolution Clock Utilities
// ============================================================================
[[nodiscard]] inline uint64_t GetTimeNs() noexcept {
    return pal::GetTimestampNs();
}

[[nodiscard]] inline double GetTimeSeconds() noexcept {
    return static_cast<double>(GetTimeNs()) * 1e-9;
}

class Clock {
public:
    Clock() noexcept : m_StartNs(GetTimeNs()), m_LastNs(m_StartNs) {}

    void Restart() noexcept {
        m_StartNs = GetTimeNs();
        m_LastNs = m_StartNs;
    }

    [[nodiscard]] uint64_t GetElapsedNs() const noexcept {
        return GetTimeNs() - m_StartNs;
    }

    [[nodiscard]] double GetElapsedSeconds() const noexcept {
        return static_cast<double>(GetElapsedNs()) * 1e-9;
    }

    /// Measures elapsed seconds since last call to Lap() and advances reference
    double Lap() noexcept {
        const uint64_t now = GetTimeNs();
        const double dt = static_cast<double>(now - m_LastNs) * 1e-9;
        m_LastNs = now;
        return dt;
    }

private:
    uint64_t m_StartNs;
    uint64_t m_LastNs;
};

// ============================================================================
// 2. TimestepAccumulator (Gaffer on Games Fixed-Timestep Accumulator)
// ============================================================================
struct TimestepAccumulator {
    float fixedDt = 1.0f / 60.0f;
    float maxFrameDt = 0.25f;
    float accumulator = 0.0f;
    uint32_t maxStepsPerFrame = 8u;

    /// Advances accumulator by raw delta time, enforces clamps, and returns fixed step count
    [[nodiscard]] uint32_t Advance(float rawFrameDt) noexcept {
        // Guard against NaN, Inf, and negative values. Non-finite or negative values are sanitized to 0.0f.
        if (std::isnan(rawFrameDt) || std::isinf(rawFrameDt) || rawFrameDt <= 0.0f) {
            rawFrameDt = 0.0f;
        }

        // Self-heal accumulator if it was previously corrupted
        if (std::isnan(accumulator) || std::isinf(accumulator) || accumulator < 0.0f) {
            accumulator = 0.0f;
        }

        const float clamped = std::clamp(rawFrameDt, 0.0f, maxFrameDt);
        accumulator += clamped;

        uint32_t steps = 0u;
        constexpr float eps = 1e-5f;
        while ((accumulator + eps) >= fixedDt && steps < maxStepsPerFrame) {
            accumulator = std::max(0.0f, accumulator - fixedDt);
            ++steps;
        }

        // Spiral-of-death mitigation: discard remaining backlog if max sub-steps hit
        if (accumulator >= fixedDt) {
            accumulator = 0.0f;
        }

        return steps;
    }

    [[nodiscard]] uint32_t Advance(double rawFrameDt) noexcept {
        return Advance(static_cast<float>(rawFrameDt));
    }

    /// Steps accumulator and invokes callback(fixedDt) for each fixed sub-step
    template <typename Callback>
    uint32_t Step(float rawFrameDt, Callback&& callback) {
        const uint32_t steps = Advance(rawFrameDt);
        for (uint32_t i = 0; i < steps; ++i) {
            callback(fixedDt);
        }
        return steps;
    }

    template <typename Callback>
    uint32_t Step(double rawFrameDt, Callback&& callback) {
        return Step(static_cast<float>(rawFrameDt), std::forward<Callback>(callback));
    }

    /// Computes render interpolation alpha in [0.0, 1.0]
    [[nodiscard]] float GetAlpha() const noexcept {
        if (std::isnan(accumulator) || std::isinf(accumulator) || fixedDt <= 1e-6f) {
            return 0.0f;
        }
        return std::clamp(accumulator / fixedDt, 0.0f, 1.0f);
    }

    [[nodiscard]] double GetAlphaDouble() const noexcept {
        return static_cast<double>(GetAlpha());
    }

    [[nodiscard]] float GetFixedDt() const noexcept { return fixedDt; }
    void SetFixedDt(float dt) noexcept { if (dt > 1e-6f) fixedDt = dt; }

    [[nodiscard]] float GetAccumulator() const noexcept { return accumulator; }

    void Reset() noexcept {
        accumulator = 0.0f;
    }
};

} // namespace tvk
