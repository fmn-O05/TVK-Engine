#include "tvk/app/Engine.h"
#include "tvk/core/Log.h"
#include "tvk/platform/PAL.h"
#include <chrono>
#include <thread>

namespace tvk {

Engine::Engine(const EngineConfig& config)
    : m_Config(config),
      m_JobSystem(config.numWorkerThreads > 0 ? config.numWorkerThreads : static_cast<int>(std::thread::hardware_concurrency())),
      m_FrameArena(config.frameArenaCapacity) {
    m_Accumulator.fixedDt = static_cast<float>(config.fixedTimestep);
    m_Accumulator.maxFrameDt = 0.25f;
    m_Accumulator.maxStepsPerFrame = 8u;
}

Engine::~Engine() {
    TeardownSubsystems();
}

void Engine::RequestExit() noexcept {
    m_ExitRequested.store(true, std::memory_order_release);
}

bool Engine::IsRunning() const noexcept {
    return m_Running.load(std::memory_order_acquire);
}

void Engine::SetupSubsystems() {
    // 1. TerminalGuard initialization (active unless headless)
    if (!m_Config.headless) {
        TerminalGuardOptions guardOpts;
        guardOpts.alternateScreen = true;
        guardOpts.hideCursor = true;
        guardOpts.kittyKeyboard = m_Config.enableKittyKeyboard;
        guardOpts.mouseTracking = m_Config.enableMouse;
        m_TerminalGuard = std::make_unique<TerminalGuard>(guardOpts);
    }

    // 2. Renderer initialization
    RendererDescriptor desc;
    desc.InitialQuality = m_Config.initialQuality;
    desc.Blitter = m_Config.initialBlitter;
    desc.HeadlessFallback = m_Config.headless;
    desc.FallbackWidth = static_cast<int>(m_Config.fallbackWidth);
    desc.FallbackHeight = static_cast<int>(m_Config.fallbackHeight);
    m_Renderer = std::make_unique<Renderer>(desc);

    // Initial query of terminal size
    if (m_Renderer) {
        m_Renderer->UpdateTerminalSize();
    }
}

void Engine::TeardownSubsystems() {
    m_Renderer.reset();
    m_TerminalGuard.reset(); // RAII cleanly restores cooked terminal mode
}

void Engine::ProcessInputEvents() {
    // Reset transient single-frame key and mouse state (pressed/released edges)
    m_Input.BeginFrame();

    // Drain non-blocking POSIX stdin if interactive
    if (!m_Config.headless) {
        char buffer[4096];
        size_t bytesRead = pal::ReadInput(buffer, sizeof(buffer));
        if (bytesRead > 0) {
            m_Input.Feed(std::string_view(buffer, bytesRead));
        }
    }

    // Advance decay timers for non-Kitty terminals
    uint64_t nowNs = pal::GetTimestampNs();
    m_Input.Update(static_cast<double>(nowNs) * 1e-9);

    // Automatic exit condition handling
    if (m_Config.exitOnEscape && m_Input.IsKeyPressed(KeyCode::Escape)) {
        RequestExit();
    }
    if (m_Config.exitOnQ && m_Input.IsKeyPressed(KeyCode::Q)) {
        RequestExit();
    }
}

void Engine::HandleTerminalResize() {
    if (pal::ConsumeResizeEvent()) {
        if (m_Renderer) {
            m_Renderer->UpdateTerminalSize();
        }
        int w = 0, h = 0;
        if (pal::TryGetTerminalSize(w, h)) {
            m_EventBus.Publish(WindowResizeEvent{w, h});
        }
    }
}

bool Engine::Run(Application& app) {
    SetupSubsystems();

    // Lifecycle: OnInit
    if (!app.OnInit(*this)) {
        Log::Print(LogLevel::Error, "Application OnInit returned false; terminating startup.");
        TeardownSubsystems();
        return false;
    }

    m_Running.store(true, std::memory_order_release);
    m_ExitRequested.store(false, std::memory_order_release);

    uint64_t lastFrameNs = pal::GetTimestampNs();
    uint64_t frameIndex = 0;

    // Master Frame Loop
    while (m_Running.load(std::memory_order_acquire) &&
           !m_ExitRequested.load(std::memory_order_acquire)) {
        Log::SetCurrentFrame(frameIndex++);
        uint64_t frameStartNs = pal::GetTimestampNs();
        double frameDt = static_cast<double>(frameStartNs - lastFrameNs) * 1e-9;
        lastFrameNs = frameStartNs;

        // Stage 1: Input Drain & FSM Update
        ProcessInputEvents();
        HandleTerminalResize();

        // Dispatch queued events accumulated during input or previous frame stages
        m_EventBus.DispatchQueued();

        if (m_ExitRequested.load(std::memory_order_acquire)) break;

        // Stage 2: Fixed-Timestep Simulation Loop
        uint32_t numSteps = m_Accumulator.Advance(static_cast<float>(frameDt));
        for (uint32_t step = 0; step < numSteps; ++step) {
            app.OnFixedUpdate(m_Config.fixedTimestep);
        }

        // Compute render interpolation alpha
        double alpha = static_cast<double>(m_Accumulator.GetAlpha());

        // Stage 3: Variable-Rate Frame Update
        app.OnUpdate(frameDt);

        // Stage 4: Render Extraction
        m_CommandBuffer.Begin();
        app.OnRender(alpha);
        m_CommandBuffer.End();

        // Stage 5: Immediate-Mode UI Pass
        app.OnUI();

        // Stage 6: Command Execution & Presentation
        if (m_Renderer) {
            m_Renderer->Execute(m_CommandBuffer);
            m_Renderer->Present();
            m_LastFrameStats = m_Renderer->GetFrameStats();
        }

        // Stage 7: Frame Linear Arena Reset
        m_FrameArena.Reset();

        // Stage 8: Frame Pacing & FPS Limiting
        uint64_t frameEndNs = pal::GetTimestampNs();
        uint64_t frameElapsedNs = frameEndNs - frameStartNs;

        if (m_Config.targetFps > 0) {
            uint64_t targetNs = 1000000000ULL / m_Config.targetFps;
            if (frameElapsedNs < targetNs) {
                uint64_t sleepNs = targetNs - frameElapsedNs;
                std::this_thread::sleep_for(std::chrono::nanoseconds(sleepNs));
            }
        }
    }

    // Lifecycle: OnShutdown
    app.OnShutdown();

    m_Running.store(false, std::memory_order_release);
    TeardownSubsystems();
    return true;
}

} // namespace tvk
