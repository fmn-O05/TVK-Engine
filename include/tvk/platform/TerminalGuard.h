#pragma once
#include <utility>
#include "tvk/platform/PAL.h"

namespace tvk {

using TerminalGuardOptions = TerminalConfig;

/// Move-only RAII guard wrapping terminal raw mode, alternate screen buffer,
/// mouse tracking, and Kitty keyboard protocol setup and restoration.
class TerminalGuard {
public:
    TerminalGuard() : TerminalGuard(TerminalConfig{}) {}

    explicit TerminalGuard(TerminalConfig config)
        : m_Config(config), m_Active(false) {
        if (pal::SetupTerminal(m_Config)) {
            m_Active = true;
        }
    }

    ~TerminalGuard() {
        Restore();
    }

    // Non-copyable
    TerminalGuard(const TerminalGuard&) = delete;
    TerminalGuard& operator=(const TerminalGuard&) = delete;

    // Move-only
    TerminalGuard(TerminalGuard&& other) noexcept
        : m_Config(other.m_Config), m_Active(other.m_Active) {
        other.m_Active = false;
    }

    TerminalGuard& operator=(TerminalGuard&& other) noexcept {
        if (this != &other) {
            if (m_Active && !other.m_Active) {
                Restore();
            }
            m_Config = other.m_Config;
            m_Active = other.m_Active;
            other.m_Active = false;
        }
        return *this;
    }

    [[nodiscard]] bool IsActive() const noexcept { return m_Active; }
    [[nodiscard]] const TerminalConfig& GetConfig() const noexcept { return m_Config; }

    /// Explicitly restores cooked terminal mode and exits private buffer
    void Restore() noexcept {
        if (m_Active) {
            pal::RestoreTerminal();
            m_Active = false;
        }
    }

private:
    TerminalConfig m_Config{};
    bool m_Active = false;
};

} // namespace tvk
