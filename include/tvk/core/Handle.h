#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace tvk {

template <typename Tag>
struct Handle {
  uint32_t id = 0;

  static constexpr Handle Invalid() noexcept { return Handle{0}; }

  static constexpr Handle Make(uint16_t index, uint16_t generation) noexcept {
    return Handle{(static_cast<uint32_t>(generation) << 16) |
                  static_cast<uint32_t>(index)};
  }

  constexpr bool IsValid() const noexcept { return (id >> 16) != 0; }
  constexpr uint16_t Index() const noexcept {
    return static_cast<uint16_t>(id & 0xFFFFu);
  }
  constexpr uint16_t Generation() const noexcept {
    return static_cast<uint16_t>(id >> 16);
  }

  constexpr bool operator==(const Handle &o) const noexcept {
    return id == o.id;
  }
  constexpr bool operator!=(const Handle &o) const noexcept {
    return id != o.id;
  }
};

template <typename T, typename Tag = T>
class HandlePool {
public:
  HandlePool() = default;

  explicit HandlePool(size_t initialCapacity) {
    m_Slots.reserve(initialCapacity);
    m_FreeIndices.reserve(initialCapacity);
  }

  template <typename... Args>
  Handle<Tag> Create(Args &&...args) {
    uint16_t idx = 0;
    if (!m_FreeIndices.empty()) {
      idx = m_FreeIndices.back();
      m_FreeIndices.pop_back();
      Slot &s = m_Slots[idx];
      s.value = T(std::forward<Args>(args)...);
      s.active = true;
      ++m_ActiveCount;
      return Handle<Tag>::Make(idx, s.generation);
    }

    if (m_Slots.size() >= 0xFFFFu) {
      return Handle<Tag>::Invalid();
    }

    idx = static_cast<uint16_t>(m_Slots.size());
    Slot s{T(std::forward<Args>(args)...), 1u, true};
    m_Slots.push_back(std::move(s));
    ++m_ActiveCount;
    return Handle<Tag>::Make(idx, 1u);
  }

  bool Destroy(Handle<Tag> handle) {
    if (!IsValid(handle))
      return false;
    const uint16_t idx = handle.Index();
    Slot &s = m_Slots[idx];
    s.active = false;
    s.value = T{};
    ++s.generation;
    if (s.generation == 0)
      s.generation = 1;
    m_FreeIndices.push_back(idx);
    --m_ActiveCount;
    return true;
  }

  bool IsValid(Handle<Tag> handle) const noexcept {
    if (!handle.IsValid())
      return false;
    const uint16_t idx = handle.Index();
    if (idx >= m_Slots.size())
      return false;
    const Slot &s = m_Slots[idx];
    return s.active && s.generation == handle.Generation();
  }

  T *Get(Handle<Tag> handle) noexcept {
    if (!IsValid(handle))
      return nullptr;
    return &m_Slots[handle.Index()].value;
  }

  const T *Get(Handle<Tag> handle) const noexcept {
    if (!IsValid(handle))
      return nullptr;
    return &m_Slots[handle.Index()].value;
  }

  void Clear() {
    m_FreeIndices.clear();
    for (size_t i = 0; i < m_Slots.size(); ++i) {
      Slot &s = m_Slots[i];
      if (s.active) {
        s.active = false;
        s.value = T{};
        ++s.generation;
        if (s.generation == 0)
          s.generation = 1;
      }
      m_FreeIndices.push_back(static_cast<uint16_t>(m_Slots.size() - 1 - i));
    }
    m_ActiveCount = 0;
  }

  size_t Count() const noexcept { return m_ActiveCount; }
  size_t Capacity() const noexcept { return m_Slots.size(); }

private:
  struct Slot {
    T value{};
    uint16_t generation = 1;
    bool active = false;
  };

  std::vector<Slot> m_Slots;
  std::vector<uint16_t> m_FreeIndices;
  size_t m_ActiveCount = 0;
};

} // namespace tvk
