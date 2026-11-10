#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>
#include <vector>
#include "tvk/core/Assert.h"
#include "tvk/core/Handle.h"

namespace tvk {

// ============================================================================
// 1. FrameLinearArena (64-byte Cache-Aligned Linear Bump Allocator)
// ============================================================================
class alignas(64) FrameLinearArena {
public:
    explicit FrameLinearArena(size_t capacityBytes = 2u * 1024u * 1024u)
        : m_Capacity(capacityBytes) {
        if (m_Capacity > 0) {
            m_Buffer = static_cast<uint8_t*>(::operator new(m_Capacity, std::align_val_t{64}, std::nothrow));
        }
    }

    ~FrameLinearArena() {
        if (m_Buffer) {
            ::operator delete(m_Buffer, std::align_val_t{64});
            m_Buffer = nullptr;
        }
    }

    FrameLinearArena(const FrameLinearArena&) = delete;
    FrameLinearArena& operator=(const FrameLinearArena&) = delete;
    FrameLinearArena(FrameLinearArena&&) = delete;
    FrameLinearArena& operator=(FrameLinearArena&&) = delete;

    /// Bump pointer allocation with customizable alignment (default 64 bytes)
    [[nodiscard]] void* Allocate(size_t bytes, size_t alignment = 64u) noexcept {
        if (bytes == 0 || !m_Buffer) return nullptr;

        // Fast-path overflow check before any pointer or alignment arithmetic:
        // If requested bytes exceeds total capacity or remaining capacity, fail immediately.
        if (bytes > m_Capacity || bytes > m_Capacity - m_Offset) {
            ++m_OverflowCount;
            return nullptr;
        }

        // Normalize alignment: minimum 1
        if (alignment == 0u) {
            alignment = 1u;
        }

        const uintptr_t base = reinterpret_cast<uintptr_t>(m_Buffer);
        const uintptr_t current = base + m_Offset;
        const uintptr_t alignMask = static_cast<uintptr_t>(alignment - 1u);

        // Guard against pointer addition wrap-around
        if (UINTPTR_MAX - current < alignMask) {
            ++m_OverflowCount;
            return nullptr;
        }

        const uintptr_t aligned = (current + alignMask) & ~alignMask;
        if (aligned < current) {
            ++m_OverflowCount;
            return nullptr;
        }

        const size_t alignedOffset = static_cast<size_t>(aligned - base);
        if (alignedOffset > m_Capacity || bytes > m_Capacity - alignedOffset) {
            ++m_OverflowCount;
            return nullptr;
        }

        const size_t newOffset = alignedOffset + bytes;
        m_Offset = newOffset;
        if (m_Offset > m_PeakOffset) {
            m_PeakOffset = m_Offset;
        }
        return reinterpret_cast<void*>(aligned);
    }

    /// Alias for Allocate(bytes, alignment)
    [[nodiscard]] void* AllocateBytes(size_t bytes, size_t alignment = 64u) noexcept {
        return Allocate(bytes, alignment);
    }

    /// Typed array allocation helper
    template <typename T>
    [[nodiscard]] T* Allocate(size_t count = 1u) noexcept {
        if (count == 0u) return nullptr;
        if constexpr (sizeof(T) > 0u) {
            if (count > SIZE_MAX / sizeof(T)) {
                ++m_OverflowCount;
                return nullptr;
            }
        }
        constexpr size_t itemAlign = alignof(T) > 16u ? alignof(T) : 16u;
        void* raw = Allocate(sizeof(T) * count, itemAlign);
        return static_cast<T*>(raw);
    }

    /// Resets bump offset to 0 at frame boundary without releasing backing memory
    void Reset() noexcept {
        m_Offset = 0u;
    }

    /// Telemetry & inspection
    [[nodiscard]] size_t GetUsedBytes() const noexcept { return m_Offset; }
    [[nodiscard]] size_t GetPeakBytes() const noexcept { return m_PeakOffset; }
    [[nodiscard]] size_t GetHighWaterMark() const noexcept { return m_PeakOffset; }
    [[nodiscard]] size_t GetCapacityBytes() const noexcept { return m_Capacity; }
    [[nodiscard]] uint32_t GetOverflowCount() const noexcept { return m_OverflowCount; }

    void ResetHighWaterMark() noexcept { m_PeakOffset = m_Offset; }
    void ResetOverflowCount() noexcept { m_OverflowCount = 0u; }

private:
    uint8_t* m_Buffer = nullptr;
    size_t m_Capacity = 0u;
    size_t m_Offset = 0u;
    size_t m_PeakOffset = 0u;
    uint32_t m_OverflowCount = 0u;
};

// ============================================================================
// 2. PoolAllocator<T> (Generational Fixed-Size Block Pool)
// ============================================================================
template <typename T>
class PoolAllocator {
public:
    using HandleType = Handle<T>;

    explicit PoolAllocator(size_t capacity = 1024u) {
        m_Slots.resize(capacity);
        m_FreeIndices.reserve(capacity);
        for (size_t i = 0; i < capacity; ++i) {
            m_FreeIndices.push_back(static_cast<uint16_t>(capacity - 1u - i));
        }
    }

    ~PoolAllocator() {
        Clear();
    }

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    PoolAllocator(PoolAllocator&&) noexcept = default;
    PoolAllocator& operator=(PoolAllocator&&) noexcept = default;

    /// Allocates an element from the pool and initializes it in-place
    template <typename... Args>
    [[nodiscard]] HandleType Allocate(Args&&... args) {
        if (m_FreeIndices.empty()) {
            return HandleType::Invalid();
        }

        const uint16_t idx = m_FreeIndices.back();
        m_FreeIndices.pop_back();

        Slot& s = m_Slots[idx];
        new (&s.storage) T(std::forward<Args>(args)...);
        s.active = true;
        ++m_ActiveCount;

        return HandleType::Make(idx, s.generation);
    }

    template <typename... Args>
    [[nodiscard]] HandleType Create(Args&&... args) {
        return Allocate(std::forward<Args>(args)...);
    }

    /// Frees an element by generational handle
    bool Free(HandleType handle) {
        if (!IsValid(handle)) return false;

        const uint16_t idx = handle.Index();
        Slot& s = m_Slots[idx];

        reinterpret_cast<T*>(&s.storage)->~T();
        s.active = false;
        ++s.generation;
        if (s.generation == 0u) s.generation = 1u; // Prevent 0 generation sentinel

        m_FreeIndices.push_back(idx);
        --m_ActiveCount;
        return true;
    }

    bool Destroy(HandleType handle) { return Free(handle); }

    /// Frees an element by raw pointer
    bool Free(T* ptr) {
        if (!ptr) return false;
        HandleType h = GetHandle(ptr);
        return Free(h);
    }

    /// Validates if handle is currently active and matches current generation
    [[nodiscard]] bool IsValid(HandleType handle) const noexcept {
        if (!handle.IsValid()) return false;
        const uint16_t idx = handle.Index();
        if (idx >= m_Slots.size()) return false;
        const Slot& s = m_Slots[idx];
        return s.active && s.generation == handle.Generation();
    }

    /// Retrieves pointer to active object; returns nullptr if handle is stale or invalid
    [[nodiscard]] T* Get(HandleType handle) noexcept {
        if (!IsValid(handle)) return nullptr;
        return reinterpret_cast<T*>(&m_Slots[handle.Index()].storage);
    }

    [[nodiscard]] const T* Get(HandleType handle) const noexcept {
        if (!IsValid(handle)) return nullptr;
        return reinterpret_cast<const T*>(&m_Slots[handle.Index()].storage);
    }

    /// Resolves handle from raw pointer
    [[nodiscard]] HandleType GetHandle(const T* ptr) const noexcept {
        if (!ptr || !Owns(ptr)) return HandleType::Invalid();

        const uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
        const uintptr_t base = reinterpret_cast<uintptr_t>(m_Slots.data());
        const size_t slotSize = sizeof(Slot);
        const size_t idx = (p - base) / slotSize;

        if (idx >= m_Slots.size()) return HandleType::Invalid();
        const Slot& s = m_Slots[idx];
        if (!s.active || reinterpret_cast<const void*>(&s.storage) != static_cast<const void*>(ptr)) {
            return HandleType::Invalid();
        }
        return HandleType::Make(static_cast<uint16_t>(idx), s.generation);
    }

    /// Checks whether pointer resides within pool memory
    [[nodiscard]] bool Owns(const T* ptr) const noexcept {
        if (!ptr || m_Slots.empty()) return false;
        const void* p = static_cast<const void*>(ptr);
        const void* start = static_cast<const void*>(m_Slots.data());
        const void* end = static_cast<const void*>(m_Slots.data() + m_Slots.size());
        return p >= start && p < end;
    }

    void Clear() {
        for (size_t i = 0; i < m_Slots.size(); ++i) {
            Slot& s = m_Slots[i];
            if (s.active) {
                reinterpret_cast<T*>(&s.storage)->~T();
                s.active = false;
                ++s.generation;
                if (s.generation == 0u) s.generation = 1u;
            }
        }
        m_FreeIndices.clear();
        for (size_t i = 0; i < m_Slots.size(); ++i) {
            m_FreeIndices.push_back(static_cast<uint16_t>(m_Slots.size() - 1u - i));
        }
        m_ActiveCount = 0u;
    }

    [[nodiscard]] size_t Count() const noexcept { return m_ActiveCount; }
    [[nodiscard]] size_t Capacity() const noexcept { return m_Slots.size(); }
    [[nodiscard]] bool IsFull() const noexcept { return m_ActiveCount >= m_Slots.size(); }
    [[nodiscard]] bool IsEmpty() const noexcept { return m_ActiveCount == 0u; }

private:
    struct Slot {
        alignas(alignof(T)) uint8_t storage[sizeof(T)];
        uint16_t generation = 1u;
        bool active = false;
    };

    std::vector<Slot> m_Slots;
    std::vector<uint16_t> m_FreeIndices;
    size_t m_ActiveCount = 0u;
};

} // namespace tvk
