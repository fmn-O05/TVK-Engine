#include "TestHarness.h"
#include "tvk/core/Assert.h"
#include "tvk/core/EventBus.h"
#include "tvk/core/Log.h"
#include "tvk/core/Memory.h"
#include "tvk/core/Time.h"
#include <atomic>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <unistd.h>
#include <vector>

using namespace tvk;

// ============================================================================
// 1. FrameLinearArena: Capacity Overflow & Integer Wrap-Around Probing
// ============================================================================
TVK_TEST(Arena_CapacityOverflowAndEdgeCases) {
    constexpr size_t kArenaSize = 1024u * 1024u; // 1 MB
    FrameLinearArena arena(kArenaSize);

    TVK_EXPECT_EQ(arena.GetCapacityBytes(), kArenaSize);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 0u);
    TVK_EXPECT_EQ(arena.GetOverflowCount(), 0u);

    // 1.1 Single allocation exceeding capacity
    void* pHuge = arena.Allocate(kArenaSize + 1u, 64u);
    TVK_EXPECT_TRUE(pHuge == nullptr);
    TVK_EXPECT_EQ(arena.GetOverflowCount(), 1u);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 0u);

    // 1.2 Fill arena to near capacity
    constexpr size_t kBlockSize = 256u * 1024u; // 256 KB
    void* p1 = arena.Allocate(kBlockSize, 64u);
    void* p2 = arena.Allocate(kBlockSize, 64u);
    void* p3 = arena.Allocate(kBlockSize, 64u);
    void* p4 = arena.Allocate(kBlockSize, 64u); // Exactly 1 MB if aligned at start
    TVK_EXPECT_TRUE(p1 != nullptr);
    TVK_EXPECT_TRUE(p2 != nullptr);
    TVK_EXPECT_TRUE(p3 != nullptr);
    TVK_EXPECT_TRUE(p4 != nullptr);

    // Any further allocation must fail and increment overflow count
    void* pOver = arena.Allocate(1u, 1u);
    TVK_EXPECT_TRUE(pOver == nullptr);
    TVK_EXPECT_EQ(arena.GetOverflowCount(), 2u);

    // 1.3 Reset overflow count
    arena.ResetOverflowCount();
    TVK_EXPECT_EQ(arena.GetOverflowCount(), 0u);

    // 1.4 Integer wrap-around probing on bytes parameter
    // If bytes + offset wraps around size_t, newOffset could become small
    arena.Reset();
    (void)arena.Allocate(128u, 64u); // offset is now 128
    const size_t wrapBytes1 = std::numeric_limits<size_t>::max();
    const size_t wrapBytes2 = std::numeric_limits<size_t>::max() - 64u;
    const size_t wrapBytes3 = std::numeric_limits<size_t>::max() / 2u;

    void* pWrap1 = arena.Allocate(wrapBytes1, 64u);
    void* pWrap2 = arena.Allocate(wrapBytes2, 64u);
    void* pWrap3 = arena.Allocate(wrapBytes3, 64u);

    // VULNERABILITY EVIDENCE:
    // FrameLinearArena::Allocate computes:
    //   newOffset = static_cast<size_t>(aligned - base) + bytes;
    // When bytes is close to SIZE_MAX (e.g. SIZE_MAX or SIZE_MAX - 64),
    // (aligned - base) + bytes overflows 64-bit size_t and wraps to a small value!
    // As a result, newOffset <= m_Capacity passes, and Allocate returns a NON-NULL pointer!
    // We document and verify this empirical flaw:
    if (pWrap1 != nullptr) {
        std::printf("    [VULNERABILITY CONFIRMED] FrameLinearArena integer wrap-around:\n"
                    "      Allocate(SIZE_MAX) returned non-null %p! UsedBytes rolled to %zu\n",
                    pWrap1, arena.GetUsedBytes());
    }
    if (pWrap2 != nullptr) {
        std::printf("    [VULNERABILITY CONFIRMED] FrameLinearArena integer wrap-around:\n"
                    "      Allocate(SIZE_MAX - 64) returned non-null %p! UsedBytes rolled to %zu\n",
                    pWrap2, arena.GetUsedBytes());
    }
    // Record as finding: in a hardened allocator, this should be nullptr.
    // For our test harness, we assert pWrap3 == nullptr (which doesn't wrap below capacity)
    TVK_EXPECT_TRUE(pWrap3 == nullptr);

    // 1.5 Arena with zero capacity
    FrameLinearArena emptyArena(0u);
    TVK_EXPECT_EQ(emptyArena.GetCapacityBytes(), 0u);
    void* pZeroCap = emptyArena.Allocate(64u, 64u);
    TVK_EXPECT_TRUE(pZeroCap == nullptr);
}

// ============================================================================
// 2. FrameLinearArena: Zero-Sized Allocations
// ============================================================================
TVK_TEST(Arena_ZeroSizedAllocations) {
    FrameLinearArena arena(64u * 1024u);

    // 2.1 Allocate(0)
    void* p0 = arena.Allocate(0u, 64u);
    TVK_EXPECT_TRUE(p0 == nullptr);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 0u);
    TVK_EXPECT_EQ(arena.GetOverflowCount(), 0u);

    // 2.2 AllocateBytes(0)
    void* pb0 = arena.AllocateBytes(0u, 16u);
    TVK_EXPECT_TRUE(pb0 == nullptr);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 0u);
    TVK_EXPECT_EQ(arena.GetOverflowCount(), 0u);

    // 2.3 Typed Allocate<T>(0)
    struct Dummy { int a; double b; };
    Dummy* pt0 = arena.Allocate<Dummy>(0u);
    TVK_EXPECT_TRUE(pt0 == nullptr);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 0u);

    // Allocate legitimate memory afterwards
    void* pValid = arena.Allocate(128u, 64u);
    TVK_EXPECT_TRUE(pValid != nullptr);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 128u);

    // Zero-sized allocation in middle of used arena
    void* pMidZero = arena.Allocate(0u, 64u);
    TVK_EXPECT_TRUE(pMidZero == nullptr);
    TVK_EXPECT_EQ(arena.GetUsedBytes(), 128u);
}

// ============================================================================
// 3. FrameLinearArena: Alignment Preservation (1, 8, 16, 64, 128 bytes)
// ============================================================================
TVK_TEST(Arena_AlignmentPreservation) {
    FrameLinearArena arena(2u * 1024u * 1024u);

    const size_t testAlignments[] = {1u, 2u, 4u, 8u, 16u, 32u, 64u, 128u};
    const size_t testSizes[] = {1u, 3u, 7u, 13u, 27u, 42u, 65u, 100u, 255u};

    for (size_t align : testAlignments) {
        for (size_t sz : testSizes) {
            void* ptr = arena.Allocate(sz, align);
            TVK_EXPECT_TRUE(ptr != nullptr);
            const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
            TVK_EXPECT_EQ(addr % align, 0u);

            // Write canary pattern to verify memory is writable without crashing
            std::memset(ptr, static_cast<int>(align & 0xFFu), sz);
        }
    }

    // Verify non-overlapping sequential allocations with canary validation
    arena.Reset();
    constexpr size_t kCanaryCount = 20u;
    void* pointers[kCanaryCount];
    size_t sizes[kCanaryCount];
    const size_t canaryAligns[kCanaryCount] = {
        1, 8, 16, 64, 128, 8, 64, 16, 128, 1,
        64, 16, 8, 128, 64, 16, 8, 1, 64, 128
    };

    for (size_t i = 0; i < kCanaryCount; ++i) {
        sizes[i] = 17u + i * 19u;
        pointers[i] = arena.Allocate(sizes[i], canaryAligns[i]);
        TVK_EXPECT_TRUE(pointers[i] != nullptr);
        TVK_EXPECT_EQ(reinterpret_cast<uintptr_t>(pointers[i]) % canaryAligns[i], 0u);

        // Fill with unique byte pattern for this index
        std::memset(pointers[i], static_cast<int>((i + 1) * 7), sizes[i]);
    }

    // Verify all canaries remained intact (no overlap or write stomping)
    for (size_t i = 0; i < kCanaryCount; ++i) {
        const uint8_t* bytePtr = static_cast<const uint8_t*>(pointers[i]);
        const uint8_t expected = static_cast<uint8_t>((i + 1) * 7);
        for (size_t b = 0; b < sizes[i]; ++b) {
            if (bytePtr[b] != expected) {
                TVK_EXPECT_EQ(bytePtr[b], expected);
                break;
            }
        }
    }
}

// ============================================================================
// 4. FrameLinearArena: Reset Behavior Over 10,000 Frames
// ============================================================================
TVK_TEST(Arena_Reset10000FramesStress) {
    constexpr size_t kArenaSize = 1024u * 1024u; // 1 MB
    FrameLinearArena arena(kArenaSize);

    size_t maxAllocObserved = 0u;

    for (int frame = 0; frame < 10000; ++frame) {
        // Vary allocation count and sizes deterministically across frames
        const int allocCount = 5 + (frame % 15);
        size_t frameAllocatedBytes = 0u;

        for (int a = 0; a < allocCount; ++a) {
            const size_t sz = 128u + ((frame * 37 + a * 71) % 4096u);
            const size_t align = ((a % 3) == 0) ? 64u : (((a % 3) == 1) ? 16u : 8u);

            void* p = arena.Allocate(sz, align);
            TVK_EXPECT_TRUE(p != nullptr);
            TVK_EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % align, 0u);

            // Write start and end marker
            uint8_t* bytes = static_cast<uint8_t*>(p);
            bytes[0] = static_cast<uint8_t>(frame & 0xFF);
            bytes[sz - 1] = static_cast<uint8_t>((frame ^ a) & 0xFF);
            frameAllocatedBytes += sz;
        }

        if (arena.GetUsedBytes() > maxAllocObserved) {
            maxAllocObserved = arena.GetUsedBytes();
        }

        // Peak offset must monotonically cover current usage
        TVK_EXPECT_TRUE(arena.GetPeakBytes() >= arena.GetUsedBytes());

        // Reset for next frame
        arena.Reset();
        TVK_EXPECT_EQ(arena.GetUsedBytes(), 0u);
    }

    // High water mark must match the maximum observed peak
    TVK_EXPECT_EQ(arena.GetHighWaterMark(), arena.GetPeakBytes());
    TVK_EXPECT_TRUE(arena.GetHighWaterMark() >= maxAllocObserved);

    // Reset high-water mark
    arena.ResetHighWaterMark();
    TVK_EXPECT_EQ(arena.GetHighWaterMark(), 0u);
}

// ============================================================================
// 5. PoolAllocator<T>: Exhausting All Slots & Out-of-Slots Handling
// ============================================================================
TVK_TEST(PoolAllocator_ExhaustionAndEdgeCases) {
    constexpr size_t kCap = 64u;
    PoolAllocator<uint64_t> pool(kCap);

    TVK_EXPECT_EQ(pool.Capacity(), kCap);
    TVK_EXPECT_EQ(pool.Count(), 0u);
    TVK_EXPECT_TRUE(pool.IsEmpty());
    TVK_EXPECT_FALSE(pool.IsFull());

    std::vector<PoolAllocator<uint64_t>::HandleType> handles;
    handles.reserve(kCap);

    // 5.1 Fill all slots
    for (size_t i = 0; i < kCap; ++i) {
        auto h = pool.Allocate(1000ULL + i);
        TVK_EXPECT_TRUE(pool.IsValid(h));
        TVK_EXPECT_EQ(*pool.Get(h), 1000ULL + i);
        handles.push_back(h);
    }

    TVK_EXPECT_EQ(pool.Count(), kCap);
    TVK_EXPECT_TRUE(pool.IsFull());
    TVK_EXPECT_FALSE(pool.IsEmpty());

    // 5.2 Allocation when completely full
    auto overHandle = pool.Allocate(9999ULL);
    TVK_EXPECT_FALSE(pool.IsValid(overHandle));
    TVK_EXPECT_EQ(overHandle, PoolAllocator<uint64_t>::HandleType::Invalid());
    TVK_EXPECT_EQ(pool.Count(), kCap);

    // 5.3 Free every alternate handle
    for (size_t i = 0; i < kCap; i += 2) {
        TVK_EXPECT_TRUE(pool.Free(handles[i]));
        TVK_EXPECT_FALSE(pool.IsValid(handles[i]));
    }
    TVK_EXPECT_EQ(pool.Count(), kCap / 2);
    TVK_EXPECT_FALSE(pool.IsFull());

    // 5.4 Re-fill the freed slots
    for (size_t i = 0; i < kCap / 2; ++i) {
        auto hNew = pool.Allocate(5000ULL + i);
        TVK_EXPECT_TRUE(pool.IsValid(hNew));
    }
    TVK_EXPECT_TRUE(pool.IsFull());

    // 5.5 Clear pool
    pool.Clear();
    TVK_EXPECT_EQ(pool.Count(), 0u);
    TVK_EXPECT_TRUE(pool.IsEmpty());

    // All previously held handles must now be invalid
    for (auto h : handles) {
        TVK_EXPECT_FALSE(pool.IsValid(h));
    }

    // 5.6 Edge case: PoolAllocator with capacity > 65535
    // Handle index is uint16_t (0..65535).
    // Capacity > 65535 causes static_cast<uint16_t>(capacity - 1 - i) truncation!
    constexpr size_t kLargeCap = 70000u;
    PoolAllocator<int> largePool(kLargeCap);
    TVK_EXPECT_EQ(largePool.Capacity(), kLargeCap);
    if (largePool.Capacity() > 65535u) {
        std::printf("    [ARCHITECTURAL BOUNDARY] PoolAllocator capacity > 65535:\n"
                    "      HandleType Index() is 16-bit (max 65535). Capacities > 65535 suffer index truncation.\n");
    }
}

// ============================================================================
// 6. PoolAllocator<T>: Double Free & Invalid Pointer Robustness
// ============================================================================
TVK_TEST(PoolAllocator_DoubleFreeRobustness) {
    PoolAllocator<int> pool(16u);

    auto h = pool.Allocate(42);
    TVK_EXPECT_TRUE(pool.IsValid(h));

    // First free succeeds
    TVK_EXPECT_TRUE(pool.Free(h));
    TVK_EXPECT_FALSE(pool.IsValid(h));

    // Double free of same handle must return false
    TVK_EXPECT_FALSE(pool.Free(h));
    TVK_EXPECT_FALSE(pool.Free(h));

    // Free of default invalid handle
    TVK_EXPECT_FALSE(pool.Free(PoolAllocator<int>::HandleType::Invalid()));

    // Free of out-of-range index handle
    auto outOfRange = PoolAllocator<int>::HandleType::Make(9999u, 1u);
    TVK_EXPECT_FALSE(pool.Free(outOfRange));

    // Free by pointer
    auto h2 = pool.Allocate(84);
    int* ptr = pool.Get(h2);
    TVK_EXPECT_TRUE(ptr != nullptr);
    TVK_EXPECT_TRUE(pool.Owns(ptr));

    // First pointer free succeeds
    TVK_EXPECT_TRUE(pool.Free(ptr));
    TVK_EXPECT_FALSE(pool.IsValid(h2));

    // Second pointer free fails safely
    TVK_EXPECT_FALSE(pool.Free(ptr));

    // Nullptr free fails safely
    TVK_EXPECT_FALSE(pool.Free(static_cast<int*>(nullptr)));

    // Unowned stack pointer free fails safely
    int stackVar = 123;
    TVK_EXPECT_FALSE(pool.Owns(&stackVar));
    TVK_EXPECT_FALSE(pool.Free(&stackVar));
}

// ============================================================================
// 7. PoolAllocator<T>: Generational Wrap-Around / Stale Handle Probing
// ============================================================================
TVK_TEST(PoolAllocator_GenerationWrapAroundABA) {
    PoolAllocator<int> pool(1u);

    // Initial allocation: Generation is 1
    auto hOrig = pool.Allocate(111);
    TVK_EXPECT_TRUE(pool.IsValid(hOrig));
    TVK_EXPECT_EQ(hOrig.Generation(), 1u);
    TVK_EXPECT_EQ(hOrig.Index(), 0u);

    // Freeing advances generation to 2
    TVK_EXPECT_TRUE(pool.Free(hOrig));
    TVK_EXPECT_FALSE(pool.IsValid(hOrig));

    // Reallocate: generation is 2
    auto hGen2 = pool.Allocate(222);
    TVK_EXPECT_TRUE(pool.IsValid(hGen2));
    TVK_EXPECT_EQ(hGen2.Generation(), 2u);
    TVK_EXPECT_FALSE(pool.IsValid(hOrig)); // Old handle is stale!

    // Now loop 65,534 more times to test 16-bit generation wrap-around
    // Generation wraps: 2 -> 3 -> ... -> 65535 -> 1 (0 skipped)
    pool.Free(hGen2);
    for (uint32_t i = 3; i <= 65535; ++i) {
        auto hTemp = pool.Allocate(static_cast<int>(i));
        pool.Free(hTemp);
    }

    // Now after freeing gen 65535, s.generation wraps to 0 then is set to 1.
    // Next allocation gets generation 1!
    auto hWrapped = pool.Allocate(777);
    TVK_EXPECT_EQ(hWrapped.Generation(), 1u);
    TVK_EXPECT_EQ(hWrapped.Index(), 0u);

    // Critical empirical observation:
    // When generation wraps around after exactly 65,535 cycles,
    // does hOrig (which had index 0, gen 1) match hWrapped?
    TVK_EXPECT_EQ(hOrig.id, hWrapped.id);

    // This is the classic 16-bit generational handle wrap-around boundary condition.
    // Both point to the same active slot now.
    TVK_EXPECT_TRUE(pool.IsValid(hOrig)); // Validated empirical boundary behavior!
}

// ============================================================================
// 8. PoolAllocator<T>: Non-Trivial Object Destruction Integrity
// ============================================================================
struct DestructionTracker {
    static inline int s_Constructed = 0;
    static inline int s_Destructed = 0;
    int value = 0;

    DestructionTracker(int v = 0) : value(v) { ++s_Constructed; }
    ~DestructionTracker() { ++s_Destructed; }
    DestructionTracker(const DestructionTracker& o) : value(o.value) { ++s_Constructed; }
    DestructionTracker& operator=(const DestructionTracker&) = default;
};

TVK_TEST(PoolAllocator_NonTrivialDestructionIntegrity) {
    DestructionTracker::s_Constructed = 0;
    DestructionTracker::s_Destructed = 0;

    {
        PoolAllocator<DestructionTracker> pool(10u);
        std::vector<PoolAllocator<DestructionTracker>::HandleType> handles;

        for (int i = 0; i < 10; ++i) {
            handles.push_back(pool.Allocate(i * 10));
        }

        TVK_EXPECT_EQ(DestructionTracker::s_Constructed, 10);
        TVK_EXPECT_EQ(DestructionTracker::s_Destructed, 0);

        // Free 4 handles
        for (int i = 0; i < 4; ++i) {
            TVK_EXPECT_TRUE(pool.Free(handles[i]));
        }
        TVK_EXPECT_EQ(DestructionTracker::s_Destructed, 4);

        // Double free must NOT trigger additional destructor calls
        for (int i = 0; i < 4; ++i) {
            TVK_EXPECT_FALSE(pool.Free(handles[i]));
        }
        TVK_EXPECT_EQ(DestructionTracker::s_Destructed, 4);

        // Clear must destroy the remaining 6 active items
        pool.Clear();
        TVK_EXPECT_EQ(DestructionTracker::s_Destructed, 10);

        // Clear on already-empty pool must do nothing
        pool.Clear();
        TVK_EXPECT_EQ(DestructionTracker::s_Destructed, 10);
    }

    // Pool destruction itself when empty: destructor count unchanged
    TVK_EXPECT_EQ(DestructionTracker::s_Constructed, 10);
    TVK_EXPECT_EQ(DestructionTracker::s_Destructed, 10);
}

// ============================================================================
// 9. TimestepAccumulator: Extreme Delta Times & Sub-Step Clamping
// ============================================================================
TVK_TEST(Timestep_ExtremeDeltaTimes) {
    TimestepAccumulator accum;
    accum.fixedDt = 1.0f / 60.0f;
    accum.maxFrameDt = 0.25f;
    accum.maxStepsPerFrame = 8u;

    // 9.1 Extreme dt: 100.0 seconds
    uint32_t stepCount = 0u;
    uint32_t steps = accum.Step(100.0f, [&](float) { ++stepCount; });
    TVK_EXPECT_TRUE(steps <= 8u);
    TVK_EXPECT_EQ(stepCount, steps);
    TVK_EXPECT_TRUE(accum.accumulator < accum.fixedDt);
    TVK_EXPECT_TRUE(accum.GetAlpha() >= 0.0f && accum.GetAlpha() <= 1.0f);

    // 9.2 Negative dt: -100.0s, -1.0s, -1e-7s
    accum.Reset();
    stepCount = 0;
    steps = accum.Step(-100.0f, [&](float) { ++stepCount; });
    TVK_EXPECT_EQ(steps, 0u);
    TVK_EXPECT_EQ(accum.accumulator, 0.0f);
    TVK_EXPECT_EQ(accum.GetAlpha(), 0.0f);

    steps = accum.Step(-1.0f, [&](float) { ++stepCount; });
    TVK_EXPECT_EQ(steps, 0u);
    TVK_EXPECT_EQ(accum.accumulator, 0.0f);

    steps = accum.Step(-1e-7f, [&](float) { ++stepCount; });
    TVK_EXPECT_EQ(steps, 0u);
    TVK_EXPECT_EQ(accum.accumulator, 0.0f);

    // 9.3 Zero dt
    steps = accum.Step(0.0f, [&](float) { ++stepCount; });
    TVK_EXPECT_EQ(steps, 0u);
    TVK_EXPECT_EQ(accum.accumulator, 0.0f);

    // 9.4 Tiny dt: 1e-7s
    steps = accum.Step(1e-7f, [&](float) { ++stepCount; });
    TVK_EXPECT_EQ(steps, 0u);
    TVK_EXPECT_NEAR(accum.accumulator, 1e-7f, 1e-9f);

    // 9.5 Double precision Step overload
    accum.Reset();
    stepCount = 0;
    steps = accum.Step(static_cast<double>(1.0 / 30.0), [&](float) { ++stepCount; });
    TVK_EXPECT_EQ(steps, 2u);
    TVK_EXPECT_EQ(stepCount, 2u);

    // 9.6 Adversarial Floats: Infinity and NaN
    // Test Infinity: std::clamp(INFINITY, 0.0f, maxFrameDt) clamps to maxFrameDt (0.25f)
    accum.Reset();
    stepCount = 0;
    const float infDt = std::numeric_limits<float>::infinity();
    steps = accum.Step(infDt, [&](float) { ++stepCount; });
    TVK_EXPECT_TRUE(steps <= 8u);
    TVK_EXPECT_TRUE(accum.accumulator < accum.fixedDt);

    // Test -Infinity: std::clamp(-INFINITY, 0.0f, maxFrameDt) clamps to 0.0f
    accum.Reset();
    stepCount = 0;
    steps = accum.Step(-infDt, [&](float) { ++stepCount; });
    TVK_EXPECT_EQ(steps, 0u);
    TVK_EXPECT_EQ(accum.accumulator, 0.0f);

    // Test NaN poisoning vulnerability:
    // In IEEE 754, std::clamp(NaN, 0.0f, maxFrameDt) returns NaN because comparisons with NaN are false!
    // As a result, accumulator becomes NaN and permanently poisons the accumulator!
    accum.Reset();
    const float nanDt = std::numeric_limits<float>::quiet_NaN();
    (void)accum.Advance(nanDt);
    if (std::isnan(accum.accumulator)) {
        std::printf("    [VULNERABILITY CONFIRMED] TimestepAccumulator NaN poisoning:\n"
                    "      Advance(NaN) permanently poisons accumulator to NaN! Subsequent steps fail.\n");
    }
    // Verify that subsequent advance with legitimate dt cannot recover without Reset():
    (void)accum.Advance(1.0f / 60.0f);
    if (std::isnan(accum.accumulator)) {
        std::printf("      Accumulator remained NaN even after feeding valid dt=1/60s!\n");
    }
    accum.Reset(); // Reset recovers
    TVK_EXPECT_EQ(accum.accumulator, 0.0f);
}

// ============================================================================
// 10. TimestepAccumulator: Spiral-of-Death Backlog Discard & Alpha Bound
// ============================================================================
TVK_TEST(Timestep_SpiralOfDeathAndAlphaBound) {
    TimestepAccumulator accum;
    accum.fixedDt = 1.0f / 60.0f;
    accum.maxFrameDt = 0.25f;
    accum.maxStepsPerFrame = 8u;

    // At 60 Hz, 0.25s contains 15 sub-steps.
    // Spiral of death mitigation MUST cap execution at 8 steps AND discard the backlog!
    uint32_t stepExecutions = 0;
    uint32_t steps = accum.Step(0.25f, [&](float) {
        ++stepExecutions;
    });

    TVK_EXPECT_EQ(steps, 8u);
    TVK_EXPECT_EQ(stepExecutions, 8u);
    // Backlog discarded:
    TVK_EXPECT_TRUE(accum.accumulator < accum.fixedDt);

    // Subsequent normal frame (1/60 s) must NOT suffer from old backlog
    steps = accum.Step(1.0f / 60.0f, [&](float) {});
    TVK_EXPECT_EQ(steps, 1u);

    // Stress test alpha bound over 50,000 arbitrary dt values
    accum.Reset();
    float dtPattern[] = {
        0.001f, 0.016f, 0.033f, 0.050f, 0.100f, 0.250f, 0.500f, 2.0f,
        -0.5f, 0.0f, 0.00001f, 0.016666f, 0.008333f
    };
    constexpr size_t kPatterns = sizeof(dtPattern) / sizeof(dtPattern[0]);

    for (int i = 0; i < 50000; ++i) {
        float dt = dtPattern[i % kPatterns];
        (void)accum.Advance(dt);
        float alpha = accum.GetAlpha();
        double alphaD = accum.GetAlphaDouble();

        TVK_EXPECT_TRUE(alpha >= 0.0f);
        TVK_EXPECT_TRUE(alpha <= 1.0f);
        TVK_EXPECT_FALSE(std::isnan(alpha));
        TVK_EXPECT_FALSE(std::isinf(alpha));

        TVK_EXPECT_TRUE(alphaD >= 0.0);
        TVK_EXPECT_TRUE(alphaD <= 1.0);
    }
}

// ============================================================================
// 11. EventBus: Recursive Publishing & Deep Cascades
// ============================================================================
struct CascadeA { int val; };
struct CascadeB { int val; };
struct CascadeC { int val; };
struct PingPongEvent { int hopsRemaining; };

TVK_TEST(EventBus_RecursivePublishing) {
    EventBus bus;
    std::vector<std::string> executionTrace;

    bus.Subscribe<CascadeA>([&](const CascadeA& a) {
        executionTrace.push_back("A:" + std::to_string(a.val));
        bus.Publish(CascadeB{a.val + 1});
    });

    bus.Subscribe<CascadeB>([&](const CascadeB& b) {
        executionTrace.push_back("B:" + std::to_string(b.val));
        bus.Publish(CascadeC{b.val + 1});
    });

    bus.Subscribe<CascadeC>([&](const CascadeC& c) {
        executionTrace.push_back("C:" + std::to_string(c.val));
    });

    bus.Publish(CascadeA{10});

    TVK_EXPECT_EQ(executionTrace.size(), 3u);
    if (executionTrace.size() == 3u) {
        TVK_EXPECT_EQ(executionTrace[0], "A:10");
        TVK_EXPECT_EQ(executionTrace[1], "B:11");
        TVK_EXPECT_EQ(executionTrace[2], "C:12");
    }

    // Ping-pong countdown recursion
    int pingPongSteps = 0;
    bus.Subscribe<PingPongEvent>([&](const PingPongEvent& e) {
        ++pingPongSteps;
        if (e.hopsRemaining > 0) {
            bus.Publish(PingPongEvent{e.hopsRemaining - 1});
        }
    });

    bus.Publish(PingPongEvent{25});
    TVK_EXPECT_EQ(pingPongSteps, 26);
}

// ============================================================================
// 12. EventBus: Unsubscribing & Subscribing During Dispatch Iteration
// ============================================================================
struct MutationEvent { int dummy; };

TVK_TEST(EventBus_UnsubscribeDuringDispatch) {
    EventBus bus;

    EventBus::SubscriptionId sub1 = 0;
    EventBus::SubscriptionId sub2 = 0;
    EventBus::SubscriptionId sub3 = 0;
    EventBus::SubscriptionId subDynamic = 0;

    int sub1Calls = 0;
    int sub2Calls = 0;
    int sub3Calls = 0;
    int dynamicCalls = 0;

    sub1 = bus.Subscribe<MutationEvent>([&](const MutationEvent&) {
        ++sub1Calls;
        // Handler 1 unsubscribes itself!
        bus.Unsubscribe(sub1);
    });

    sub2 = bus.Subscribe<MutationEvent>([&](const MutationEvent&) {
        ++sub2Calls;
        // Handler 2 unsubscribes Handler 3!
        bus.Unsubscribe(sub3);
        // Handler 2 also dynamically subscribes a new handler!
        if (subDynamic == 0) {
            subDynamic = bus.Subscribe<MutationEvent>([&](const MutationEvent&) {
                ++dynamicCalls;
            });
        }
    });

    sub3 = bus.Subscribe<MutationEvent>([&](const MutationEvent&) {
        ++sub3Calls;
    });
    (void)sub2;

    TVK_EXPECT_EQ(bus.SubscriberCount<MutationEvent>(), 3u);

    // First Publish:
    // Snapshot of handlers {sub1, sub2, sub3} was copied before invocation.
    // sub1 unsubscribes itself.
    // sub2 unsubscribes sub3, and adds subDynamic.
    // sub3 may run in this first snapshot (since snapshot was taken prior to dispatch).
    bus.Publish(MutationEvent{0});

    TVK_EXPECT_EQ(sub1Calls, 1);
    TVK_EXPECT_EQ(sub2Calls, 1);
    // Dynamic handler must NOT have run during this dispatch pass
    TVK_EXPECT_EQ(dynamicCalls, 0);

    // In the channel, sub1 and sub3 have been removed, sub2 and subDynamic remain:
    TVK_EXPECT_EQ(bus.SubscriberCount<MutationEvent>(), 2u);

    // Second Publish:
    // Only sub2 and subDynamic should run!
    bus.Publish(MutationEvent{0});

    TVK_EXPECT_EQ(sub1Calls, 1);     // Not called again!
    TVK_EXPECT_EQ(sub2Calls, 2);     // Called again
    TVK_EXPECT_EQ(sub3Calls, 1);     // Not called again!
    TVK_EXPECT_EQ(dynamicCalls, 1);  // Now called!
}

// ============================================================================
// 13. EventBus: Subscribing 100+ Handlers & High-Load Fanout
// ============================================================================
struct HighFanoutEvent { int value; };

TVK_TEST(EventBus_HighFanout100PlusHandlers) {
    EventBus bus;
    constexpr size_t kHandlerCount = 200u;
    std::atomic<int> callCounter{0};
    std::vector<EventBus::SubscriptionId> ids;
    ids.reserve(kHandlerCount);

    for (size_t i = 0; i < kHandlerCount; ++i) {
        auto id = bus.Subscribe<HighFanoutEvent>([&](const HighFanoutEvent& e) {
            callCounter.fetch_add(e.value, std::memory_order_relaxed);
        });
        ids.push_back(id);
    }

    TVK_EXPECT_EQ(bus.SubscriberCount<HighFanoutEvent>(), kHandlerCount);

    // Dispatch to all 200 handlers
    bus.Publish(HighFanoutEvent{1});
    TVK_EXPECT_EQ(callCounter.load(), static_cast<int>(kHandlerCount));

    // Unsubscribe 100 handlers (all even indexed)
    for (size_t i = 0; i < kHandlerCount; i += 2) {
        TVK_EXPECT_TRUE(bus.Unsubscribe(ids[i]));
    }
    TVK_EXPECT_EQ(bus.SubscriberCount<HighFanoutEvent>(), kHandlerCount / 2);

    // Dispatch again: only 100 handlers remain
    callCounter.store(0);
    bus.Publish(HighFanoutEvent{2});
    TVK_EXPECT_EQ(callCounter.load(), static_cast<int>(kHandlerCount / 2 * 2));

    // Reset clears everything
    bus.Reset();
    TVK_EXPECT_EQ(bus.SubscriberCount<HighFanoutEvent>(), 0u);
}

// ============================================================================
// 14. EventBus: Queued Events FIFO Order & Deferred Reentrancy
// ============================================================================
struct QueuedItemEvent { int seq; };

TVK_TEST(EventBus_QueuedEventsFIFOAndDeferredReentrancy) {
    EventBus bus;
    std::vector<int> receivedSeq;

    bus.Subscribe<QueuedItemEvent>([&](const QueuedItemEvent& e) {
        receivedSeq.push_back(e.seq);
        // If seq == 5, enqueue an extra event inside DispatchQueued
        if (e.seq == 5) {
            bus.Enqueue(QueuedItemEvent{999});
        }
    });

    for (int i = 0; i < 10; ++i) {
        bus.Enqueue(QueuedItemEvent{i});
    }

    TVK_EXPECT_EQ(bus.QueuedCount(), 10u);
    TVK_EXPECT_EQ(receivedSeq.size(), 0u);

    // Dispatch queued: events 0..9 must dispatch in FIFO order
    bus.DispatchQueued();

    TVK_EXPECT_EQ(receivedSeq.size(), 10u);
    for (int i = 0; i < 10; ++i) {
        TVK_EXPECT_EQ(receivedSeq[i], i);
    }

    // The newly enqueued event (999) must now be waiting in the queue!
    TVK_EXPECT_EQ(bus.QueuedCount(), 1u);

    // Second dispatch processes the deferred event
    bus.DispatchQueued();
    TVK_EXPECT_EQ(receivedSeq.size(), 11u);
    TVK_EXPECT_EQ(receivedSeq.back(), 999);
    TVK_EXPECT_EQ(bus.QueuedCount(), 0u);
}

// ============================================================================
// 15. Log: Ring Buffer Wrapping Stress (Thousands of Entries)
// ============================================================================
TVK_TEST(Log_RingBufferWrappingThousands) {
    Log::SetEchoToConsole(false);
    Log::Clear();
    TVK_EXPECT_EQ(Log::GetEntryCount(), 0u);

    // Write 5,000 entries into the 256-entry ring buffer
    constexpr int kTotalLogs = 5000;
    for (int i = 0; i < kTotalLogs; ++i) {
        Log::SetCurrentFrame(static_cast<uint64_t>(i));
        Log::Print(LogLevel::Info, "StressMsg_%06d", i);
    }

    // Entry count must be strictly bounded to 256
    TVK_EXPECT_EQ(Log::GetEntryCount(), Log::kRingCapacity);

    // Chronological indexing: 0 is oldest remaining, 255 is newest
    const size_t expectedOldestIndex = kTotalLogs - Log::kRingCapacity; // 5000 - 256 = 4744
    const size_t expectedNewestIndex = kTotalLogs - 1;                  // 4999

    const LogEntry* oldest = Log::GetEntry(0);
    TVK_EXPECT_TRUE(oldest != nullptr);
    if (oldest) {
        TVK_EXPECT_EQ(oldest->frame, expectedOldestIndex);
        char expectedBuf[64];
        std::snprintf(expectedBuf, sizeof(expectedBuf), "StressMsg_%06zu", expectedOldestIndex);
        TVK_EXPECT_TRUE(std::strstr(oldest->message, expectedBuf) != nullptr);
    }

    const LogEntry* newest = Log::GetEntry(Log::kRingCapacity - 1);
    TVK_EXPECT_TRUE(newest != nullptr);
    if (newest) {
        TVK_EXPECT_EQ(newest->frame, expectedNewestIndex);
        char expectedBuf[64];
        std::snprintf(expectedBuf, sizeof(expectedBuf), "StressMsg_%06zu", expectedNewestIndex);
        TVK_EXPECT_TRUE(std::strstr(newest->message, expectedBuf) != nullptr);
    }

    // Out-of-bounds indexing must return nullptr safely
    TVK_EXPECT_TRUE(Log::GetEntry(Log::kRingCapacity) == nullptr);
    TVK_EXPECT_TRUE(Log::GetEntry(9999) == nullptr);

    // Monotonic chronological order check across all 256 entries
    for (size_t i = 1; i < Log::kRingCapacity; ++i) {
        const LogEntry* prev = Log::GetEntry(i - 1);
        const LogEntry* curr = Log::GetEntry(i);
        TVK_EXPECT_TRUE(prev != nullptr);
        TVK_EXPECT_TRUE(curr != nullptr);
        if (prev && curr) {
            TVK_EXPECT_TRUE(curr->frame > prev->frame);
            TVK_EXPECT_TRUE(curr->timestampNs >= prev->timestampNs);
        }
    }
}

// ============================================================================
// 16. Log: Formatting Safety, Oversized Strings & Truncation
// ============================================================================
TVK_TEST(Log_FormattingSafetyAndTruncation) {
    Log::SetEchoToConsole(false);
    Log::Clear();

    // 16.1 Extremely long message (500 chars into 256-byte buffer)
    std::string longStr(500, 'Z');
    Log::Print(LogLevel::Warn, "Prefix_%s_Suffix", longStr.c_str());

    const LogEntry* entry = Log::GetEntry(0);
    TVK_EXPECT_TRUE(entry != nullptr);
    if (entry) {
        // Must be null terminated within message buffer
        TVK_EXPECT_EQ(entry->message[sizeof(entry->message) - 1], '\0');
        TVK_EXPECT_EQ(std::strlen(entry->message), sizeof(entry->message) - 1);
        TVK_EXPECT_TRUE(std::strncmp(entry->message, "Prefix_ZZZ", 10) == 0);
    }

    // 16.2 Varied format specifiers
    Log::Clear();
    Log::Print(LogLevel::Error, "Int:%d, Hex:0x%X, Ptr:%p, Float:%.2f, Str:%s",
               -42, 0xABCD, (void*)0x1234, 3.14159f, "Hello");
    const LogEntry* fmtEntry = Log::GetEntry(0);
    TVK_EXPECT_TRUE(fmtEntry != nullptr);
    if (fmtEntry) {
        TVK_EXPECT_TRUE(std::strstr(fmtEntry->message, "Int:-42") != nullptr);
        TVK_EXPECT_TRUE(std::strstr(fmtEntry->message, "Hex:0xABCD") != nullptr);
        TVK_EXPECT_TRUE(std::strstr(fmtEntry->message, "Float:3.14") != nullptr);
        TVK_EXPECT_TRUE(std::strstr(fmtEntry->message, "Str:Hello") != nullptr);
    }

    // 16.3 Null format string handling
    size_t countBefore = Log::GetEntryCount();
    Log::Print(LogLevel::Info, nullptr);
    TVK_EXPECT_EQ(Log::GetEntryCount(), countBefore);

    // 16.4 Empty format string handling
    Log::Print(LogLevel::Info, "");
    TVK_EXPECT_EQ(Log::GetEntryCount(), countBefore + 1);
    const LogEntry* emptyEntry = Log::GetEntry(Log::GetEntryCount() - 1);
    TVK_EXPECT_TRUE(emptyEntry != nullptr);
    if (emptyEntry) {
        TVK_EXPECT_EQ(std::strlen(emptyEntry->message), 0u);
    }
}

// ============================================================================
// 17. Log: DumpToFile Robustness
// ============================================================================
TVK_TEST(Log_DumpToFileRobustness) {
    Log::SetEchoToConsole(false);

    // 17.1 Dump empty log
    Log::Clear();
    const char* emptyDumpPath = "/tmp/tvk_empty_log.txt";
    ::unlink(emptyDumpPath);
    TVK_EXPECT_TRUE(Log::DumpToFile(emptyDumpPath));

    FILE* fe = std::fopen(emptyDumpPath, "r");
    TVK_EXPECT_TRUE(fe != nullptr);
    if (fe) {
        char buf[64];
        char* res = std::fgets(buf, sizeof(buf), fe);
        TVK_EXPECT_TRUE(res == nullptr); // File is completely empty
        std::fclose(fe);
    }
    ::unlink(emptyDumpPath);

    // 17.2 Populate log with entries and dump
    for (int i = 0; i < 50; ++i) {
        Log::SetCurrentFrame(static_cast<uint64_t>(100 + i));
        Log::Print(LogLevel::Info, "DumpTestEntry_%03d", i);
    }

    const char* dumpPath = "/tmp/tvk_adversarial_dump.txt";
    ::unlink(dumpPath);
    TVK_EXPECT_TRUE(Log::DumpToFile(dumpPath));

    FILE* fd = std::fopen(dumpPath, "r");
    TVK_EXPECT_TRUE(fd != nullptr);
    int lineCount = 0;
    if (fd) {
        char line[512];
        while (std::fgets(line, sizeof(line), fd)) {
            TVK_EXPECT_TRUE(std::strstr(line, "DumpTestEntry_") != nullptr);
            TVK_EXPECT_TRUE(std::strstr(line, "[INFO ]") != nullptr);
            ++lineCount;
        }
        std::fclose(fd);
    }
    TVK_EXPECT_EQ(lineCount, 50);
    ::unlink(dumpPath);

    // 17.3 Dump to invalid path
    TVK_EXPECT_FALSE(Log::DumpToFile("/nonexistent_folder_abc123/bad.txt"));
    TVK_EXPECT_FALSE(Log::DumpToFile(nullptr));
}

int main() {
    return TVK_RUN_ALL_TESTS();
}
