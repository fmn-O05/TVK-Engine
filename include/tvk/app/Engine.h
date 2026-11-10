#pragma once
#include "tvk/app/Application.h"
#include "tvk/app/EngineConfig.h"
#include "tvk/core/EventBus.h"
#include "tvk/core/JobSystem.h"
#include "tvk/core/Memory.h"
#include "tvk/core/Stats.h"
#include "tvk/core/Time.h"
#include "tvk/platform/Input.h"
#include "tvk/platform/TerminalGuard.h"
#include "tvk/renderer/CommandBuffer.h"
#include "tvk/renderer/Renderer.h"
#include <atomic>
#include <memory>

namespace tvk {

/// Master engine runtime coordinating platform, input, memory arenas, simulation timestep,
/// event bus, software rasterizer, presenter, and application lifecycle.
class Engine {
public:
    explicit Engine(const EngineConfig& config = EngineConfig{});
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept = default;
    Engine& operator=(Engine&&) noexcept = default;

    /// Runs the master application lifecycle loop.
    /// @param app The user application implementation.
    /// @return true on clean shutdown; false if initialization failed.
    bool Run(Application& app);

    /// Signals the engine to exit at the end of the current frame.
    void RequestExit() noexcept;

    /// Checks whether the engine main loop is actively running.
    [[nodiscard]] bool IsRunning() const noexcept;

    // Subsystem Accessors
    [[nodiscard]] EngineConfig& GetConfig() noexcept { return m_Config; }
    [[nodiscard]] const EngineConfig& GetConfig() const noexcept { return m_Config; }

    [[nodiscard]] Input& GetInput() noexcept { return m_Input; }
    [[nodiscard]] const Input& GetInput() const noexcept { return m_Input; }

    [[nodiscard]] Renderer* GetRenderer() noexcept { return m_Renderer.get(); }
    [[nodiscard]] const Renderer* GetRenderer() const noexcept { return m_Renderer.get(); }

    [[nodiscard]] FrameLinearArena& GetFrameArena() noexcept { return m_FrameArena; }
    [[nodiscard]] EventBus& GetEventBus() noexcept { return m_EventBus; }
    [[nodiscard]] JobSystem& GetJobSystem() noexcept { return m_JobSystem; }
    [[nodiscard]] CommandBuffer& GetCommandBuffer() noexcept { return m_CommandBuffer; }
    [[nodiscard]] const FrameStats& GetFrameStats() const noexcept { return m_LastFrameStats; }

private:
    void SetupSubsystems();
    void TeardownSubsystems();
    void ProcessInputEvents();
    void HandleTerminalResize();

private:
    EngineConfig m_Config;
    std::atomic<bool> m_Running{false};
    std::atomic<bool> m_ExitRequested{false};

    // Subsystems
    JobSystem m_JobSystem;
    FrameLinearArena m_FrameArena;
    TimestepAccumulator m_Accumulator;
    EventBus m_EventBus;
    Input m_Input;
    CommandBuffer m_CommandBuffer;
    FrameStats m_LastFrameStats{};

    std::unique_ptr<TerminalGuard> m_TerminalGuard;
    std::unique_ptr<Renderer> m_Renderer;
};

} // namespace tvk
