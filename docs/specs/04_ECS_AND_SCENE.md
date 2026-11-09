# Specification 04 — Entity-Component-System (ECS) & Hierarchical Scene Graph

- **Layers Covered**: Layer 4 (`tvk/scene`)
- **Target Roadmap Phase**: **Phase 4 Implementation Blueprint**
- **Dependencies**: Layer 0 (`tvk/core`), Layer 2 (`tvk/math`), Layer 3 (`tvk/renderer`)
- **Industry Benchmark References**: EnTT (paged sparse-set `ComponentPool<T>`, smallest-pool lead `View<Ts...>`), Flecs & Bevy (deferred `EntityCommandBuffer`), Our Machinery (topological parent-before-child `TransformSystem`).

---

## 1. 64-Bit Generational `Entity` Identifier (`include/tvk/scene/Entity.h`)

To support up to $2^{32} - 1$ concurrent entities with zero ABA/use-after-free bugs when entities are destroyed and recycled, `Entity` is a 64-bit value (`sizeof(Entity) == 8`, `alignof(Entity) == 8`):

```cpp
#pragma once
#include <cstdint>

namespace tvk {

struct alignas(8) Entity {
    uint64_t id = 0; // [63..32]: 32-bit generation (valid >= 1); [31..0]: 32-bit sparse slot index

    constexpr Entity() noexcept = default;
    constexpr explicit Entity(uint64_t raw) noexcept : id(raw) {}
    constexpr Entity(uint32_t index, uint32_t generation) noexcept
        : id((static_cast<uint64_t>(generation) << 32ull) | static_cast<uint64_t>(index)) {}

    [[nodiscard]] constexpr bool IsValid() const noexcept { return (id >> 32ull) != 0ull; }
    [[nodiscard]] constexpr uint32_t Index() const noexcept { return static_cast<uint32_t>(id & 0xFFFFFFFFull); }
    [[nodiscard]] constexpr uint32_t Generation() const noexcept { return static_cast<uint32_t>(id >> 32ull); }

    constexpr bool operator==(const Entity& o) const noexcept { return id == o.id; }
    constexpr bool operator!=(const Entity& o) const noexcept { return id != o.id; }
};

inline constexpr Entity kNullEntity{};

} // namespace tvk
```

---

## 2. Paged Sparse-Set `ComponentPool<T>` (`include/tvk/scene/ComponentPool.h`)

### 2.1 Memory Layout & Paged Sparse Array Rationale
A naive flat sparse array for $N$ entities allocates $4 \times N$ bytes per component pool even if only 3 entities own that component. TVK Engine uses **1024-entry sparse pages** (`4 KB` per page, allocated lazily only when an entity in that page index range adds component `T`) paired with contiguous dense `Entity` and `T` vectors:

```text
Sparse Pages (4 KB per 1024 entities)          Dense Packed Arrays (Contiguous in L1/L2 Cache)
┌──────────────┐                               ┌───────┬───────┬───────┬───────┐
│ Page 0       │──► [ .. | 2 | .. | 0 | .. ]   │ Ent 9 │ Ent 2 │ Ent 5 │ Ent 1 │ m_DenseEntities
├──────────────┤           │        │          ├───────┼───────┼───────┼───────┤
│ Page 1 (null)│           │        └─────────►│ Cmp 9 │ Cmp 2 │ Cmp 5 │ Cmp 1 │ m_DenseComponents
└──────────────┘           └──────────────────►└───────┴───────┴───────┴───────┘
                                                 idx 0   idx 1   idx 2   idx 3
```

### 2.2 Complete C++ Interface & $O(1)$ Swap-and-Pop Removal

```cpp
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>
#include "tvk/scene/Entity.h"

namespace tvk {

class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual bool Remove(Entity e) noexcept = 0;
    virtual bool Has(Entity e) const noexcept = 0;
    virtual void Clear() noexcept = 0;
    [[nodiscard]] virtual size_t Size() const noexcept = 0;
    [[nodiscard]] virtual const std::vector<Entity>& GetEntities() const noexcept = 0;
};

template <typename T>
class ComponentPool final : public IComponentPool {
public:
    static constexpr uint32_t kPageSize = 1024u;
    static constexpr uint32_t kInvalidDense = 0xFFFFFFFFu;
    using SparsePage = std::array<uint32_t, kPageSize>;

    template <typename... Args>
    T& Emplace(Entity e, Args&&... args) {
        const uint32_t sparseIdx = e.Index();
        const uint32_t pageIdx = sparseIdx / kPageSize;
        const uint32_t offset = sparseIdx % kPageSize;

        if (pageIdx >= m_SparsePages.size()) {
            m_SparsePages.resize(pageIdx + 1u);
        }
        if (!m_SparsePages[pageIdx]) {
            m_SparsePages[pageIdx] = std::make_unique<SparsePage>();
            m_SparsePages[pageIdx]->fill(kInvalidDense);
        }

        uint32_t& denseSlot = (*m_SparsePages[pageIdx])[offset];
        if (denseSlot != kInvalidDense && m_DenseEntities[denseSlot] == e) {
            m_DenseComponents[denseSlot] = T(std::forward<Args>(args)...);
            return m_DenseComponents[denseSlot];
        }

        denseSlot = static_cast<uint32_t>(m_DenseComponents.size());
        m_DenseEntities.push_back(e);
        m_DenseComponents.emplace_back(std::forward<Args>(args)...);
        return m_DenseComponents.back();
    }

    bool Remove(Entity e) noexcept override {
        if (!Has(e)) return false;
        const uint32_t sparseIdx = e.Index();
        uint32_t& removedDenseIdx = (*m_SparsePages[sparseIdx / kPageSize])[sparseIdx % kPageSize];
        const uint32_t lastDenseIdx = static_cast<uint32_t>(m_DenseComponents.size() - 1u);

        if (removedDenseIdx != lastDenseIdx) {
            const Entity movedEntity = m_DenseEntities[lastDenseIdx];
            m_DenseComponents[removedDenseIdx] = std::move(m_DenseComponents[lastDenseIdx]);
            m_DenseEntities[removedDenseIdx] = movedEntity;
            const uint32_t movedSparse = movedEntity.Index();
            (*m_SparsePages[movedSparse / kPageSize])[movedSparse % kPageSize] = removedDenseIdx;
        }

        removedDenseIdx = kInvalidDense;
        m_DenseComponents.pop_back();
        m_DenseEntities.pop_back();
        return true;
    }

    [[nodiscard]] bool Has(Entity e) const noexcept override {
        if (!e.IsValid()) return false;
        const uint32_t sparseIdx = e.Index();
        const uint32_t pageIdx = sparseIdx / kPageSize;
        if (pageIdx >= m_SparsePages.size() || !m_SparsePages[pageIdx]) return false;
        const uint32_t denseIdx = (*m_SparsePages[pageIdx])[sparseIdx % kPageSize];
        return denseIdx < m_DenseEntities.size() && m_DenseEntities[denseIdx] == e;
    }

    [[nodiscard]] T* TryGet(Entity e) noexcept {
        if (!Has(e)) return nullptr;
        const uint32_t sparseIdx = e.Index();
        return &m_DenseComponents[(*m_SparsePages[sparseIdx / kPageSize])[sparseIdx % kPageSize]];
    }

    [[nodiscard]] const T* TryGet(Entity e) const noexcept {
        if (!Has(e)) return nullptr;
        const uint32_t sparseIdx = e.Index();
        return &m_DenseComponents[(*m_SparsePages[sparseIdx / kPageSize])[sparseIdx % kPageSize]];
    }

    void Clear() noexcept override {
        m_SparsePages.clear();
        m_DenseEntities.clear();
        m_DenseComponents.clear();
    }

    [[nodiscard]] size_t Size() const noexcept override { return m_DenseComponents.size(); }
    [[nodiscard]] const std::vector<Entity>& GetEntities() const noexcept override { return m_DenseEntities; }
    [[nodiscard]] std::vector<T>& GetData() noexcept { return m_DenseComponents; }

private:
    std::vector<std::unique_ptr<SparsePage>> m_SparsePages;
    std::vector<Entity> m_DenseEntities;
    std::vector<T> m_DenseComponents;
};

} // namespace tvk
```

---

## 3. Smallest-Pool Lead `View<Ts...>` & Deferred `EntityCommandBuffer` (`include/tvk/scene/Registry.h`)

### 3.1 Lead-Pool Multi-Component View Algorithm
When querying `Registry::View<A, B, C>::Each(fn)`:
1. Retrieve `ComponentPool<A>*`, `ComponentPool<B>*`, `ComponentPool<C>*`. If any pool pointer is `nullptr` or has `Size() == 0`, return immediately ($O(1)$ empty query).
2. Select the **lead pool** $P_{\text{min}}$ with the minimum `Size()` among `{poolA, poolB, poolC}`:
   $$P_{\text{min}} = \arg\min_{P \in \{P_A, P_B, P_C\}} |P|$$
3. Iterate linearly over `const Entity e : P_min->GetEntities()`:
   - Check `Has(e)` on the remaining $k - 1$ pools in $O(1)$ per pool.
   - If present in all pools, invoke `fn(e, *poolA->TryGet(e), *poolB->TryGet(e), *poolC->TryGet(e))`.

### 3.2 Deferred `EntityCommandBuffer`
Adding or destroying entities/components inside a `View::Each` loop would invalidate dense iterators via swap-and-pop. `EntityCommandBuffer` records structural mutations (`DestroyEntity(Entity)`, `AddComponent<T>(Entity, T)`, `RemoveComponent<T>(Entity)`) into a linear command buffer and flushes them deterministically at the end of the simulation phase (`Scene::FlushCommandBuffer()`).

---

## 4. Built-In Components & Hierarchical Transform Propagation (`include/tvk/scene/Components.h`, `Scene.h`)

### 4.1 Core Engine Components Layout

```cpp
struct TransformComponent {
    Vec3 position      = {0.0f, 0.0f, 0.0f};
    Quat rotation      = {0.0f, 0.0f, 0.0f, 1.0f};
    Vec3 scale         = {1.0f, 1.0f, 1.0f};
    Mat4 localMatrix   = Mat4Identity();
    Mat4 prevWorldMat  = Mat4Identity(); // Snapshot before FixedUpdate for alpha interpolation
    Mat4 worldMatrix   = Mat4Identity();
    bool dirty         = true;
};

struct HierarchyComponent {
    Entity   parent      = kNullEntity;
    Entity   firstChild  = kNullEntity;
    Entity   nextSibling = kNullEntity;
    Entity   prevSibling = kNullEntity;
    uint16_t depth       = 0; // Root = 0; child.depth = parent.depth + 1
};
```

### 4.2 Single-Pass Topological `TransformSystem`
Rather than recursive pointer-chasing tree traversals that miss CPU caches:
1. Root entities (`HierarchyComponent::parent == kNullEntity` or no `HierarchyComponent`) have `depth = 0` and compute `worldMatrix = ComposeTRS(position, rotation, scale)`.
2. Child entities are bucketed or sorted by ascending `HierarchyComponent::depth` ($d = 1, 2, \dots, d_{\max}$).
3. Iterating in ascending `depth` order guarantees that `parent`'s `worldMatrix` is **always up-to-date before any child** evaluates:
   $$M_{\text{world}}(\text{child}) = M_{\text{world}}(\text{parent}) \cdot M_{\text{local}}(\text{child})$$

### 4.3 Fixed-Timestep Render Interpolation (`RenderSystem::ExtractRenderCommands(cmd, alpha)`)
For dynamic entities updated in `OnFixedUpdate(fixedDt)`, `RenderSystem` linearly blends between `prevWorldMat` and `worldMatrix` using $\alpha = \text{accumulator} / \text{fixedDt} \in [0, 1)$:
$$M_{\text{render}} = (1.0f - \alpha) \cdot M_{\text{prevWorld}} + \alpha \cdot M_{\text{world}}$$
before submitting `cmd.DrawMesh(mesh, M_render, material, color)`.

---

## 5. Verification Criteria (`test_ecs`)

1. **Generational Recycling**: Creating 1,000 entities, destroying all 1,000, and creating 1,000 new entities reuses indices `0..999` with `Generation() == 2`, while all old `Entity` handles return `Registry::IsAlive(old) == false`.
2. **Swap-and-Pop Integrity**: Removing component `T` from the middle of a 500-entity `ComponentPool<T>` preserves $O(1)$ lookup accuracy for the last entity swapped into the vacated slot.
3. **Lead-Pool `View<A, B, C>`**: When pool `A` has 10,000 entities and pool `B` has 5 entities, `View<A, B>::Each` performs exactly 5 sparse lookups into pool `A`.
4. **3-Level Hierarchy Propagation**: Translating a grandparent entity by `(+10, 0, 0)` and rotating a parent entity by $90^\circ$ around `+Y` produces the exact analytical world position on the grandchild entity after a single `Scene::UpdateHierarchy()` call.
