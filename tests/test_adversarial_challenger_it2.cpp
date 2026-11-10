#include "TestHarness.h"
#include "tvk/app/Application.h"
#include "tvk/app/Engine.h"
#include "tvk/app/EngineConfig.h"
#include "tvk/core/Assert.h"
#include "tvk/core/EventBus.h"
#include "tvk/core/Log.h"
#include "tvk/core/Memory.h"
#include "tvk/core/Time.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

using namespace tvk;

// ============================================================================
// SUITE 1: FrameLinearArena - Integer Wrap-Around & Extreme Sizes
// ============================================================================
TVK_TEST(Arena_Allocate_ExtremeSizesAndWrapAround) {
    constexpr size_t kArenaSize = 1024u * 1024u; // 1 MB
    FrameLinearArena arena(kArenaSize);

    TVK_EXPECT_EQ(arena.GetCapacityBytes(), kArenaSize);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 0u);
    TVK_EXPECT_EQ(arena.GetOverflowCount(), 0u);

    // 1.1 SIZE_MAX allocation on empty arena
    void* pMax = arena.Allocate(std::numeric_limits<size_t>::max(), 64u);
    TVK_EXPECT_TRUE(pMax == nullptr);
    TVK_EXPECT_EQ(arena.GetOverflowCount(), 1u);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 0u);

    // 1.2 SIZE_MAX - 64 on empty arena
    void* pMaxMinus64 = arena.Allocate(std::numeric_limits<size_t>::max() - 64u, 64u);
    TVK_EXPECT_TRUE(pMaxMinus64 == nullptr);
    TVK_EXPECT_EQ(arena.GetOverflowCount(), 2u);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 0u);

    // 1.3 SIZE_MAX / 2
    void* pHalfMax = arena.Allocate(std::numeric_limits<size_t>::max() / 2u, 64u);
    TVK_EXPECT_TRUE(pHalfMax == nullptr);
    TVK_EXPECT_EQ(arena.GetOverflowCount(), 3u);

    // 1.4 Advance arena to offset 128
    void* pInitial = arena.Allocate(128u, 64u);
    TVK_EXPECT_TRUE(pInitial != nullptr);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 128u);

    // Now test integer wrap-around when offset > 0
    // In old code: (aligned - base) + SIZE_MAX wrapped around to 127, returning valid pointer!
    void* pWrapMax = arena.Allocate(std::numeric_limits<size_t>::max(), 64u);
    TVK_EXPECT_TRUE(pWrapMax == nullptr);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 128u); // MUST NOT roll back offset!

    void* pWrapMinus64 = arena.Allocate(std::numeric_limits<size_t>::max() - 64u, 64u);
    TVK_EXPECT_TRUE(pWrapMinus64 == nullptr);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 128u);

    void* pWrapMinus128 = arena.Allocate(std::numeric_limits<size_t>::max() - 128u, 64u);
    TVK_EXPECT_TRUE(pWrapMinus128 == nullptr);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 128u);

    void* pWrapMinus1000 = arena.Allocate(std::numeric_limits<size_t>::max() - 1000u, 64u);
    TVK_EXPECT_TRUE(pWrapMinus1000 == nullptr);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 128u);

    // 1.5 Allocate exactly remaining bytes
    size_t remaining = kArenaSize - 128u;
    void* pFill = arena.Allocate(remaining, 64u);
    TVK_EXPECT_TRUE(pFill != nullptr);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), kArenaSize);

    // Arena completely full: any further allocation must return nullptr
    void* pFullPlus1 = arena.Allocate(1u, 1u);
    TVK_EXPECT_TRUE(pFullPlus1 == nullptr);
    void* pFullWrap = arena.Allocate(std::numeric_limits<size_t>::max(), 64u);
    TVK_EXPECT_TRUE(pFullWrap == nullptr);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), kArenaSize);
}

// ============================================================================
// SUITE 2: FrameLinearArena - Large Alignments (128, 256, 512, 1024, 4096)
// ============================================================================
TVK_TEST(Arena_Allocate_LargeAlignments) {
    constexpr size_t kArenaSize = 4u * 1024u * 1024u; // 4 MB
    FrameLinearArena arena(kArenaSize);

    const size_t largeAlignments[] = {64u, 128u, 256u, 512u, 1024u, 2048u, 4096u};

    for (size_t align : largeAlignments) {
        arena.Reset();
        // Cause misalignment first
        void* pOdd = arena.Allocate(13u, 1u);
        TVK_EXPECT_TRUE(pOdd != nullptr);

        // Allocate with large alignment
        void* pAligned = arena.Allocate(512u, align);
        TVK_EXPECT_TRUE(pAligned != nullptr);

        const uintptr_t addr = reinterpret_cast<uintptr_t>(pAligned);
        TVK_EXPECT_EQ(addr % align, 0u);

        // Write test canary across full buffer to verify ASan validity
        std::memset(pAligned, static_cast<int>(align & 0xFFu), 512u);
        const uint8_t* bytes = static_cast<const uint8_t*>(pAligned);
        for (size_t i = 0; i < 512u; ++i) {
            TVK_EXPECT_EQ(bytes[i], static_cast<uint8_t>(align & 0xFFu));
        }
    }

    // Alignment 0 edge case (normalized to 1)
    arena.Reset();
    void* pAlign0 = arena.Allocate(64u, 0u);
    TVK_EXPECT_TRUE(pAlign0 != nullptr);

    // Alignment 1
    void* pAlign1 = arena.Allocate(64u, 1u);
    TVK_EXPECT_TRUE(pAlign1 != nullptr);

    // Extreme alignment wrap-around: alignment = SIZE_MAX
    void* pAlignMax = arena.Allocate(64u, std::numeric_limits<size_t>::max());
    TVK_EXPECT_TRUE(pAlignMax == nullptr);

    // Alignment larger than arena capacity
    void* pAlignTooBig = arena.Allocate(64u, 8u * 1024u * 1024u);
    // Alignment alone could push past capacity
    if (pAlignTooBig != nullptr) {
        const uintptr_t addr = reinterpret_cast<uintptr_t>(pAlignTooBig);
        TVK_EXPECT_EQ(addr % (8u * 1024u * 1024u), 0u);
    }
}

// ============================================================================
// SUITE 3: FrameLinearArena - Typed Allocations & Boundary Stress
// ============================================================================
TVK_TEST(Arena_TypedAllocationsAndStress) {
    FrameLinearArena arena(1024u * 1024u);

    // Typed allocation count overflow: count * sizeof(uint64_t) wraps around SIZE_MAX
    constexpr size_t overflowCount = (std::numeric_limits<size_t>::max() / sizeof(uint64_t)) + 1u;
    uint64_t* pOverflow = arena.Allocate<uint64_t>(overflowCount);
    TVK_EXPECT_TRUE(pOverflow == nullptr);

    // Count = 0
    uint64_t* pZero = arena.Allocate<uint64_t>(0u);
    TVK_EXPECT_TRUE(pZero == nullptr);

    // Valid typed allocation
    uint64_t* pValid = arena.Allocate<uint64_t>(100u);
    TVK_EXPECT_TRUE(pValid != nullptr);
    for (size_t i = 0; i < 100u; ++i) {
        pValid[i] = 0xDEADBEEFCAFEBABEULL + i;
    }
    for (size_t i = 0; i < 100u; ++i) {
        TVK_EXPECT_EQ(pValid[i], 0xDEADBEEFCAFEBABEULL + i);
    }

    // 10,000 cycles of frame allocation and reset stress
    for (int frame = 0; frame < 10000; ++frame) {
        arena.Reset();
        TVK_EXPECT_EQ(arena.GetUsedBytes(), 0u);

        void* ptr128 = arena.Allocate(128u, 128u);
        void* ptr256 = arena.Allocate(256u, 256u);
        TVK_EXPECT_TRUE(ptr128 != nullptr);
        TVK_EXPECT_TRUE(ptr256 != nullptr);
        TVK_EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr128) % 128u, 0u);
        TVK_EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr256) % 256u, 0u);
    }
}

// ============================================================================
// SUITE 4: TimestepAccumulator - NaN, Inf & Negative Robustness
// ============================================================================
TVK_TEST(Timestep_NaN_And_Inf_Hardening) {
    TimestepAccumulator accum;
    accum.fixedDt = 1.0f / 60.0f;
    accum.maxFrameDt = 0.25f;
    accum.maxStepsPerFrame = 8u;

    // 4.1 Quiet NaN
    const float qnan = std::numeric_limits<float>::quiet_NaN();
    uint32_t steps = accum.Advance(qnan);
    TVK_EXPECT_EQ(steps, 0u);
    TVK_EXPECT_FALSE(std::isnan(accum.accumulator));
    TVK_EXPECT_EQ(accum.accumulator, 0.0f);
    TVK_EXPECT_FALSE(std::isnan(accum.GetAlpha()));
    TVK_EXPECT_EQ(accum.GetAlpha(), 0.0f);

    // 4.2 Signaling NaN
    const float snan = std::numeric_limits<float>::signaling_NaN();
    steps = accum.Advance(snan);
    TVK_EXPECT_EQ(steps, 0u);
    TVK_EXPECT_FALSE(std::isnan(accum.accumulator));
    TVK_EXPECT_EQ(accum.accumulator, 0.0f);

    // 4.3 Double NaN
    const double dnan = std::numeric_limits<double>::quiet_NaN();
    steps = accum.Advance(dnan);
    TVK_EXPECT_EQ(steps, 0u);
    TVK_EXPECT_FALSE(std::isnan(accum.accumulator));

    // 4.4 Subsequent valid step works immediately without needing Reset()
    steps = accum.Advance(1.0f / 60.0f);
    TVK_EXPECT_EQ(steps, 1u);
    TVK_EXPECT_FALSE(std::isnan(accum.accumulator));

    // 4.5 Positive Infinity
    accum.Reset();
    const float posInf = std::numeric_limits<float>::infinity();
    steps = accum.Advance(posInf);
    // PosInf is sanitized or clamped; in current code posInf is non-finite -> sanitized to 0.0f
    TVK_EXPECT_TRUE(steps <= 8u);
    TVK_EXPECT_FALSE(std::isinf(accum.accumulator));
    TVK_EXPECT_FALSE(std::isnan(accum.accumulator));
    TVK_EXPECT_TRUE(accum.GetAlpha() >= 0.0f && accum.GetAlpha() <= 1.0f);

    // 4.6 Negative Infinity
    accum.Reset();
    const float negInf = -std::numeric_limits<float>::infinity();
    steps = accum.Advance(negInf);
    TVK_EXPECT_EQ(steps, 0u);
    TVK_EXPECT_EQ(accum.accumulator, 0.0f);

    // 4.7 Negative delta times
    const float negativeDts[] = {-0.0001f, -1.0f, -100.0f, -1e10f};
    for (float negDt : negativeDts) {
        accum.Reset();
        steps = accum.Advance(negDt);
        TVK_EXPECT_EQ(steps, 0u);
        TVK_EXPECT_EQ(accum.accumulator, 0.0f);
        TVK_EXPECT_EQ(accum.GetAlpha(), 0.0f);
    }

    // 4.8 Self-healing from externally corrupted state
    accum.accumulator = std::numeric_limits<float>::quiet_NaN();
    steps = accum.Advance(1.0f / 60.0f);
    TVK_EXPECT_FALSE(std::isnan(accum.accumulator));
    TVK_EXPECT_EQ(steps, 1u);

    accum.accumulator = -50.0f;
    steps = accum.Advance(1.0f / 60.0f);
    TVK_EXPECT_TRUE(accum.accumulator >= 0.0f);
    TVK_EXPECT_EQ(steps, 1u);

    accum.accumulator = std::numeric_limits<float>::infinity();
    steps = accum.Advance(1.0f / 60.0f);
    TVK_EXPECT_FALSE(std::isinf(accum.accumulator));
    TVK_EXPECT_EQ(steps, 1u);
}

// ============================================================================
// SUITE 5: TimestepAccumulator - Extreme Frame Times (dt = 100s, 1000s)
// ============================================================================
TVK_TEST(Timestep_ExtremeFrameTimes) {
    TimestepAccumulator accum;
    accum.fixedDt = 1.0f / 60.0f;
    accum.maxFrameDt = 0.25f;
    accum.maxStepsPerFrame = 8u;

    // 5.1 Extreme frame time dt = 100.0s
    uint32_t stepCount = 0;
    uint32_t steps = accum.Step(100.0f, [&](float dt) {
        TVK_EXPECT_EQ(dt, accum.fixedDt);
        ++stepCount;
    });

    TVK_EXPECT_EQ(steps, 8u);
    TVK_EXPECT_EQ(stepCount, 8u);
    // Backlog must be discarded: accumulator < fixedDt
    TVK_EXPECT_TRUE(accum.accumulator < accum.fixedDt);
    TVK_EXPECT_TRUE(accum.GetAlpha() >= 0.0f && accum.GetAlpha() <= 1.0f);

    // 5.2 Extreme frame time dt = 1000.0s
    stepCount = 0;
    steps = accum.Step(1000.0, [&](float) {
        ++stepCount;
    });
    TVK_EXPECT_EQ(steps, 8u);
    TVK_EXPECT_EQ(stepCount, 8u);
    TVK_EXPECT_TRUE(accum.accumulator < accum.fixedDt);

    // 5.3 100,000 steps adversarial stress test
    accum.Reset();
    const float mixedDts[] = {
        0.016f, std::numeric_limits<float>::quiet_NaN(), -10.0f, 100.0f,
        0.033f, std::numeric_limits<float>::infinity(), -0.001f, 0.00001f,
        0.25f, 0.5f, 1000.0f, -std::numeric_limits<float>::infinity()
    };
    constexpr size_t numPatterns = sizeof(mixedDts) / sizeof(mixedDts[0]);

    for (int i = 0; i < 100000; ++i) {
        float dt = mixedDts[i % numPatterns];
        (void)accum.Advance(dt);
        float alpha = accum.GetAlpha();
        double alphaD = accum.GetAlphaDouble();

        TVK_EXPECT_FALSE(std::isnan(alpha));
        TVK_EXPECT_FALSE(std::isinf(alpha));
        TVK_EXPECT_TRUE(alpha >= 0.0f && alpha <= 1.0f);

        TVK_EXPECT_FALSE(std::isnan(alphaD));
        TVK_EXPECT_FALSE(std::isinf(alphaD));
        TVK_EXPECT_TRUE(alphaD >= 0.0 && alphaD <= 1.0);
    }
}

// ============================================================================
// SUITE 6: EventBus - Deep Reentrancy & Queued Dispatching
// ============================================================================
struct ReentrantEventA { int depth; };
struct ReentrantEventB { int depth; };
struct QueuedChainEvent { int step; };

TVK_TEST(EventBus_ReentrantAndQueuedChains) {
    EventBus bus;
    std::vector<int> chainLog;

    // Mutually reentrant events A <-> B
    bus.Subscribe<ReentrantEventA>([&](const ReentrantEventA& a) {
        chainLog.push_back(a.depth);
        if (a.depth > 0) {
            bus.Publish(ReentrantEventB{a.depth - 1});
        }
    });

    bus.Subscribe<ReentrantEventB>([&](const ReentrantEventB& b) {
        chainLog.push_back(b.depth);
        if (b.depth > 0) {
            bus.Publish(ReentrantEventA{b.depth - 1});
        }
    });

    bus.Publish(ReentrantEventA{10});
    // Should log 10, 9, 8, ..., 0: total 11 entries
    TVK_EXPECT_EQ(chainLog.size(), 11u);
    TVK_EXPECT_EQ(chainLog[0], 10);
    TVK_EXPECT_EQ(chainLog[10], 0);

    // Queued event reentrancy: an event queued inside DispatchQueued must not loop infinitely
    std::vector<int> queuedLog;
    bus.Subscribe<QueuedChainEvent>([&](const QueuedChainEvent& e) {
        queuedLog.push_back(e.step);
        if (e.step < 5) {
            bus.Enqueue(QueuedChainEvent{e.step + 1});
        }
    });

    bus.Enqueue(QueuedChainEvent{1});
    TVK_EXPECT_EQ(bus.QueuedCount(), 1u);

    // Pass 1: should dispatch step 1 only; step 2 is enqueued for next pass
    bus.DispatchQueued();
    TVK_EXPECT_EQ(queuedLog.size(), 1u);
    TVK_EXPECT_EQ(queuedLog[0], 1);
    TVK_EXPECT_EQ(bus.QueuedCount(), 1u);

    // Pass 2: should dispatch step 2
    bus.DispatchQueued();
    TVK_EXPECT_EQ(queuedLog.size(), 2u);
    TVK_EXPECT_EQ(queuedLog[1], 2);

    // Pass 3, 4, 5
    bus.DispatchQueued(); // step 3
    bus.DispatchQueued(); // step 4
    bus.DispatchQueued(); // step 5 (does not enqueue further)
    TVK_EXPECT_EQ(queuedLog.size(), 5u);
    TVK_EXPECT_EQ(bus.QueuedCount(), 0u);

    // Pass 6: empty queue
    bus.DispatchQueued();
    TVK_EXPECT_EQ(queuedLog.size(), 5u);
}

// ============================================================================
// SUITE 7: Engine - EventBus Reentrancy & Queued Dispatching in Active Loop
// ============================================================================
struct FrameStartEvent { int frame; };
struct FramePingEvent { int counter; };
struct FramePongEvent { int counter; };
struct FrameQueuedMsg { int id; };

class AdversarialStressApplication : public Application {
public:
    Engine* engine = nullptr;
    int fixedUpdateCount = 0;
    int updateCount = 0;
    int renderCount = 0;
    int pingCount = 0;
    int pongCount = 0;
    int queuedReceivedCount = 0;
    std::vector<int> queuedReceivedIds;

    bool OnInit(Engine& e) override {
        engine = &e;
        EventBus& bus = e.GetEventBus();

        // 1. Subscribe Ping -> publishes Pong (reentrant publish inside Engine loop)
        bus.Subscribe<FramePingEvent>([this, &bus](const FramePingEvent& ping) {
            ++pingCount;
            if (ping.counter > 0) {
                bus.Publish(FramePongEvent{ping.counter - 1});
            }
        });

        // 2. Subscribe Pong -> publishes Ping
        bus.Subscribe<FramePongEvent>([this, &bus](const FramePongEvent& pong) {
            ++pongCount;
            if (pong.counter > 0) {
                bus.Publish(FramePingEvent{pong.counter - 1});
            }
        });

        // 3. Subscribe Queued message
        bus.Subscribe<FrameQueuedMsg>([this](const FrameQueuedMsg& msg) {
            ++queuedReceivedCount;
            queuedReceivedIds.push_back(msg.id);
        });

        // Enqueue 3 initial events in OnInit
        bus.Enqueue(FrameQueuedMsg{101});
        bus.Enqueue(FrameQueuedMsg{102});
        bus.Enqueue(FrameQueuedMsg{103});

        return true;
    }

    void OnFixedUpdate(double) override {
        ++fixedUpdateCount;
        // In fixed update, trigger reentrant cascade
        engine->GetEventBus().Publish(FramePingEvent{3}); // Ping(3)->Pong(2)->Ping(1)->Pong(0)

        // Also enqueue a message during fixed update
        engine->GetEventBus().Enqueue(FrameQueuedMsg{200 + fixedUpdateCount});
    }

    void OnUpdate(double) override {
        ++updateCount;

        // Verify allocations in FrameLinearArena
        void* pArena = engine->GetFrameArena().Allocate(256u, 128u);
        TVK_EXPECT_TRUE(pArena != nullptr);
        std::memset(pArena, 0xCC, 256u);

        // Also trigger reentrant ping-pong directly in OnUpdate
        engine->GetEventBus().Publish(FramePingEvent{2});

        // Enqueue another message during update
        engine->GetEventBus().Enqueue(FrameQueuedMsg{300 + updateCount});

        // Sleep 18ms so real-time elapsed exceeds fixedDt (16.6ms) for OnFixedUpdate
        std::this_thread::sleep_for(std::chrono::milliseconds(18));

        // Exit after 5 frames (~90ms)
        if (updateCount >= 5) {
            engine->RequestExit();
        }
    }

    void OnRender(double alpha) override {
        ++renderCount;
        TVK_EXPECT_TRUE(alpha >= 0.0 && alpha <= 1.0);

        // Frame arena allocation with large alignment 256
        void* pRenderBuf = engine->GetFrameArena().Allocate(1024u, 256u);
        TVK_EXPECT_TRUE(pRenderBuf != nullptr);
        TVK_EXPECT_EQ(reinterpret_cast<uintptr_t>(pRenderBuf) % 256u, 0u);
        std::memset(pRenderBuf, 0x77, 1024u);
    }

    void OnUI() override {}
    void OnShutdown() override {}
};

TVK_TEST(Engine_EventBus_Reentrancy_And_QueuedDispatch) {
    EngineConfig cfg;
    cfg.headless = true;
    cfg.targetFps = 0; // Uncapped for fast test execution
    cfg.fixedTimestep = 1.0 / 60.0;
    cfg.frameArenaCapacity = 1024u * 1024u; // 1 MB

    Engine engine(cfg);
    AdversarialStressApplication app;

    bool ok = engine.Run(app);
    TVK_EXPECT_TRUE(ok);

    // Verify lifecycle counts
    TVK_EXPECT_EQ(app.updateCount, 5);
    TVK_EXPECT_EQ(app.renderCount, 5);
    TVK_EXPECT_TRUE(app.fixedUpdateCount >= 1);

    // Verify ping-pong reentrant publishing occurred cleanly without deadlocks
    TVK_EXPECT_TRUE(app.pingCount > 0);
    TVK_EXPECT_TRUE(app.pongCount > 0);

    // Verify queued messages were received in FIFO order
    TVK_EXPECT_TRUE(app.queuedReceivedCount >= 3);
    TVK_EXPECT_EQ(app.queuedReceivedIds[0], 101);
    TVK_EXPECT_EQ(app.queuedReceivedIds[1], 102);
    TVK_EXPECT_EQ(app.queuedReceivedIds[2], 103);

    // Frame arena was reset cleanly every frame
    TVK_EXPECT_EQ(engine.GetFrameArena().GetUsedBytes(), 0u);
    TVK_EXPECT_TRUE(engine.GetFrameArena().GetPeakBytes() > 0u);
    TVK_EXPECT_EQ(engine.GetFrameArena().GetOverflowCount(), 0u);
}

int main() {
    return TVK_RUN_ALL_TESTS();
}
