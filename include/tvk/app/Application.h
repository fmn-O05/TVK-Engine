#pragma once

namespace tvk {

class Engine;

/// Abstract base class defining the 6-stage lifecycle of a TVK application.
class Application {
public:
    virtual ~Application() = default;

    /// Called once during Engine::Run before entering the main loop.
    /// @param engine Reference to the hosting Engine instance.
    /// @return true to proceed with execution; false to abort startup immediately.
    virtual bool OnInit(Engine& engine) {
        (void)engine;
        return true;
    }

    /// Fixed-rate simulation step (e.g. 60 Hz).
    /// Called 0, 1, or multiple times per presented frame depending on accumulated time.
    /// Suitable for deterministic physics, movement, and collision resolution.
    /// @param fixedDeltaTime Duration of each fixed step in seconds (e.g. 1/60s).
    virtual void OnFixedUpdate(double fixedDeltaTime) {
        (void)fixedDeltaTime;
    }

    /// Variable-rate frame update called once per presentation frame.
    /// Suitable for non-physical animations, camera smoothing, and UI state updates.
    /// @param deltaTime Elapsed duration since previous presentation frame in seconds.
    virtual void OnUpdate(double deltaTime) {
        (void)deltaTime;
    }

    /// 3D render extraction and command submission pass.
    /// Called once per frame prior to rasterization and terminal presentation.
    /// @param alpha Interpolation factor in [0.0, 1.0] between previous and current
    ///              physics states: alpha = accumulator / fixedDeltaTime.
    virtual void OnRender(double alpha) {
        (void)alpha;
    }

    /// Immediate-mode 2D UI and HUD drawing pass.
    /// Called after 3D scene commands are recorded, prior to presentation.
    virtual void OnUI() {}

    /// Cleanup hook called upon engine exit after the main loop terminates,
    /// immediately before subsystems are torn down.
    virtual void OnShutdown() {}
};

} // namespace tvk
