# Specification 05 — 3D/2.5D Physics, Collision Detection, Sequential Impulse Solver & Terminal Mouse Ray Picking

- **Layers Covered**: Layer 5 (`tvk/physics`)
- **Target Roadmap Phase**: **Phase 5 Implementation Blueprint**
- **Dependencies**: Layer 0 (`tvk/core`), Layer 2 (`tvk/math`), Layer 3 (`tvk/renderer/Camera.h`), Layer 4 (`tvk/scene`)
- **Industry Benchmark References**: Box2D-lite (Erin Catto sequential impulse solver, warm-starting & Baumgarte stabilization), Jolt Physics & Christer Ericson (*Real-Time Collision Detection*).

---

## 1. Colliders & RigidBody Data Layouts (`include/tvk/physics/Colliders.h`, `RigidBody.h`)

### 1.1 Collider Shapes & Collision Filtering (`sizeof(ColliderComponent) == 48 B`)

```cpp
#pragma once
#include <cstdint>
#include "tvk/math/Math.h"

namespace tvk {

enum class ColliderType : uint8_t {
    Sphere  = 0,
    AABB    = 1,
    OBB     = 2,
    Capsule = 3
};

struct ColliderComponent {
    ColliderType type       = ColliderType::Sphere;
    bool         isTrigger  = false;     // If true, emits CollisionEvent without physical impulse response
    uint16_t     layerMask  = 0x0001u;   // Bitmask of layers this collider belongs to
    uint16_t     collidesWith = 0xFFFFu; // Bitmask of layers this collider can collide with
    uint16_t     reserved   = 0u;
    Vec3         offset     = {0.0f, 0.0f, 0.0f}; // Local-space center offset
    union {
        struct { float radius; } sphere;
        struct { Vec3 halfExtents; } aabb;
        struct { Vec3 halfExtents; } obb; // Uses entity's TransformComponent::rotation
        struct { float radius; float halfHeight; } capsule; // Aligned along local +Y axis
    };
};

} // namespace tvk
```
Two colliders $A$ and $B$ are tested if and only if:
$$(\text{A.layerMask} \mathbin{\&} \text{B.collidesWith}) \neq 0 \;\land\; (\text{B.layerMask} \mathbin{\&} \text{A.collidesWith}) \neq 0$$

---

### 1.2 `RigidBodyComponent` Layout (`sizeof(RigidBodyComponent) == 64 B`, `alignas(16)`)

```cpp
enum class BodyType : uint8_t {
    Static    = 0, // invMass = 0, never moves
    Kinematic = 1, // invMass = 0, moved explicitly by gameplay velocity/transform
    Dynamic   = 2  // Driven by forces, gravity, and collision impulses
};

struct alignas(16) RigidBodyComponent {
    BodyType type            = BodyType::Dynamic;
    bool     isSleeping      = false;
    uint16_t flags           = 0;
    float    mass            = 1.0f;
    float    invMass         = 1.0f;  // 0.0f for Static / Kinematic
    float    restitution     = 0.3f;  // Coefficient of restitution e in [0, 1]
    float    friction        = 0.5f;  // Coulomb friction coefficient mu >= 0
    float    linearDamping   = 0.02f;
    float    angularDamping  = 0.05f;
    float    gravityScale    = 1.0f;
    Vec3     linearVelocity  = {0.0f, 0.0f, 0.0f};
    Vec3     angularVelocity = {0.0f, 0.0f, 0.0f};
    Vec3     forceAccum      = {0.0f, 0.0f, 0.0f};
};
```

---

## 2. Broadphase: Arena-Allocated `SpatialHashGrid` (`include/tvk/physics/SpatialHash.h`)

To avoid heap allocations during `PhysicsWorld::Step(fixedDt)`, the broadphase spatial hash grid allocates its bucket heads and linked nodes directly from `FrameLinearArena`:

1. **Cell Coordinate Mapping**:
   Given cell size $s > 0$ (default $s = 4.0\text{ m}$), a world-space point $(x, y, z)$ maps to integer grid cell:
   $$c_x = \lfloor x / s \rfloor, \quad c_y = \lfloor y / s \rfloor, \quad c_z = \lfloor z / s \rfloor$$
2. **Spatial Hash Function (Teschner et al.)**:
   $$h(c_x, c_y, c_z) = \left((c_x \cdot 73856093\text{u}) \oplus (c_y \cdot 19349663\text{u}) \oplus (c_z \cdot 83492791\text{u})\right) \bmod N_{\text{buckets}}$$
   where $N_{\text{buckets}} = 4096$ (power of two so $\bmod N_{\text{buckets}}$ is `& 4095u`).
3. **AABB Insertion & Candidate Pair Generation**:
   - Each collider computes its world-space `AABB` and inserts its entity index into all overlapped grid cells $[c_{\min}, c_{\max}]$.
   - Candidate pairs $(i, j)$ with $i < j$ that pass the layer mask filter and `AABB::Intersects` check are deduplicated and forwarded to narrowphase.

---

## 3. Narrowphase: Analytical Contact Manifold Generation (`include/tvk/physics/Narrowphase.h`)

Each narrowphase test populates a `ContactManifold`:
```cpp
struct ContactManifold {
    Entity a = kNullEntity;
    Entity b = kNullEntity;
    Vec3   normal = {0.0f, 1.0f, 0.0f}; // Points from A to B
    Vec3   contactPoint = {0.0f, 0.0f, 0.0f};
    float  penetration = 0.0f;          // Positive overlap depth (> 0)
    float  accumNormalImpulse = 0.0f;
    float  accumTangentImpulse[2] = {0.0f, 0.0f};
};
```

### Analytical Intersection Equations
1. **Sphere ($C_A, r_A$) vs. Sphere ($C_B, r_B$)**:
   $$\vec{d} = C_B - C_A, \quad L^2 = \|\vec{d}\|^2, \quad R = r_A + r_B$$
   Hit iff $L^2 < R^2$. With $L = \sqrt{L^2}$:
   $$\hat{n} = \begin{cases} \vec{d} / L & L > 10^{-6} \\ (0, 1, 0) & L \le 10^{-6} \end{cases}, \quad \text{penetration} = R - L, \quad \vec{p}_c = C_A + \hat{n} \left(r_A - \frac{\text{penetration}}{2}\right)$$
2. **AABB vs. AABB (3-Axis SAT)**:
   Compute overlap on each axis $k \in \{x, y, z\}$:
   $$o_k = (h_{A, k} + h_{B, k}) - |C_{B, k} - C_{A, k}|$$
   Hit iff $o_x > 0 \land o_y > 0 \land o_z > 0$. The contact normal $\hat{n}$ aligns with the axis of minimum overlap $\arg\min_k o_k$, pointing from $A$ to $B$, with $\text{penetration} = \min(o_x, o_y, o_z)$.
3. **Sphere ($C_S, r_S$) vs. AABB ($B_{\min}, B_{\max}$)**:
   Clamp sphere center to AABB to find closest point $\vec{q} = \operatorname{clamp}(C_S, B_{\min}, B_{\max})$, and evaluate $\vec{d} = C_S - \vec{q}$. If $\|\vec{d}\|^2 < r_S^2$, contact normal is $\hat{n} = \operatorname{normalize}(\vec{d})$ (or face normal of deepest interior containment if $C_S$ is strictly inside the AABB).
4. **Capsule ($\vec{a}_1 \to \vec{b}_1, r_1$) vs. Sphere / Capsule / AABB**:
   Find closest points $(\vec{p}_1, \vec{p}_2)$ between the capsule medial line segment(s) $\vec{a} + t(\vec{b} - \vec{a}),\; t \in [0, 1]$ and the target primitive, reducing the test to Sphere($\vec{p}_1, r_1$) vs. Sphere($\vec{p}_2, r_2$).
5. **OBB vs. OBB (15-Axis Separating Axis Theorem)**:
   Tests the 3 face normals of $A$, 3 face normals of $B$, and 9 cross-product edge axes $\vec{u}_{A,i} \times \vec{u}_{B,j}$, selecting the axis of minimum positive penetration.

---

## 4. Sequential Impulse Solver with Baumgarte Stabilization (`include/tvk/physics/PhysicsWorld.h`)

Each fixed timestep $\Delta t = \text{fixedDt}$ (`1/60 s`) executes in 4 deterministic stages:

1. **Symplectic / Semi-Implicit Euler Velocity Integration**:
   For every dynamic non-sleeping body:
   $$\vec{v} \leftarrow \left(\vec{v} + (\vec{g} \cdot \text{gravityScale} + \vec{F} \cdot m^{-1}) \Delta t\right) \cdot \frac{1}{1 + \text{linearDamping} \cdot \Delta t}$$
2. **Broadphase & Narrowphase Manifold Generation**:
   Populates `std::vector<ContactManifold> m_Manifolds` and emits `CollisionEvent` for triggers and new impacts.
3. **8-Iteration Sequential Impulse Contact Resolution (Erin Catto Box2D-lite)**:
   With Baumgarte factor $\beta = 0.2$, penetration slop $\delta_{\text{slop}} = 0.01\text{ m}$, effective mass $m_{\text{eff}} = (m_A^{-1} + m_B^{-1})^{-1}$, relative velocity $\vec{v}_{\text{rel}} = \vec{v}_B - \vec{v}_A$, and combined restitution $e = \max(e_A, e_B)$:
   - **Bias Velocity**:
     $$v_{\text{bias}} = -\frac{\beta}{\Delta t} \max(0.0f, \text{penetration} - \delta_{\text{slop}})$$
   - **Normal Impulse Increment & Accumulated Clamping**:
     $$\Delta j_n = m_{\text{eff}} \left(-(1 + e)(\vec{v}_{\text{rel}} \cdot \hat{n}) - v_{\text{bias}}\right)$$
     $$j_n^{\text{new}} = \max(0.0f, j_n^{\text{old}} + \Delta j_n), \quad \delta j_n = j_n^{\text{new}} - j_n^{\text{old}}, \quad j_n^{\text{old}} \leftarrow j_n^{\text{new}}$$
     $$\vec{v}_A \mathrel{-}= m_A^{-1} \delta j_n \hat{n}, \quad \vec{v}_B \mathrel{+}= m_B^{-1} \delta j_n \hat{n}$$
   - **Coulomb Tangent Friction Impulse**:
     Construct orthonormal tangent basis $(\hat{t}_1, \hat{t}_2)$ via `BuildOrthonormalBasis(n, t1, t2)`. Clamp accumulated tangent impulse $j_{t,k} \in [-\mu j_n, +\mu j_n]$ where $\mu = \sqrt{\mu_A \mu_B}$.
4. **Position Integration**:
   $$\vec{x} \leftarrow \vec{x} + \vec{v} \Delta t$$

---

## 5. Kinematic `CharacterController` & 3D Terminal Mouse Ray Picking (`include/tvk/physics/Raycast.h`)

### 5.1 `ScreenCellToRay` Mathematical Derivation (Left-Handed `+Z` View Space)
When a user clicks terminal cell `(cell_x, cell_y)` in a terminal of dimensions $(\text{term\_w}, \text{term\_h})$:
1. **Convert Cell Center to Normalized Device Coordinates (NDC)**:
   $$x_{\text{ndc}} = \frac{2.0f \cdot (\text{cell\_x} + 0.5f)}{\text{term\_w}} - 1.0f \in [-1, +1]$$
   $$y_{\text{ndc}} = 1.0f - \frac{2.0f \cdot (\text{cell\_y} + 0.5f)}{\text{term\_h}} \in [-1, +1]$$
2. **Construct View-Space Ray via Physical Aspect Ratio $a = \frac{\text{term\_w}}{2.0f \cdot \text{term\_h}}$**:
   - **Perspective Camera** ($\tan(\theta/2) = t$):
     $$\vec{o}_{\text{view}} = (0, 0, 0), \quad \vec{d}_{\text{view}} = \operatorname{normalize}\left(x_{\text{ndc}} \cdot a \cdot t,\; y_{\text{ndc}} \cdot t,\; +1.0f\right)$$
   - **Orthographic Camera** ($\text{half\_h} = h, \text{half\_w} = h \cdot a$):
     $$\vec{o}_{\text{view}} = (x_{\text{ndc}} \cdot h \cdot a,\; y_{\text{ndc}} \cdot h,\; n), \quad \vec{d}_{\text{view}} = (0, 0, +1.0f)$$
3. **Transform View-Space Ray to World Space**:
   Given view matrix $V$ with upper-left orthogonal rotation $R_{3\times 3}$ and camera eye position $\vec{e}_{\text{world}}$:
   $$\vec{o}_{\text{world}} = \vec{e}_{\text{world}} + R_{3\times 3}^T \vec{o}_{\text{view}}, \quad \vec{d}_{\text{world}} = \operatorname{normalize}\left(R_{3\times 3}^T \vec{d}_{\text{view}}\right)$$

### 5.2 Analytical Ray Intersection (`PhysicsWorld::Raycast`)
- **Ray vs. Sphere**: Solves quadratic $\|\vec{o} + t\vec{d} - \vec{c}\|^2 = r^2$ for smallest $t \in [0, t_{\max}]$.
- **Ray vs. AABB (Kay-Kajiya Slab Method)**: Computes $t_{\text{enter}} = \max_k \min(t_{0,k}, t_{1,k})$ and $t_{\text{exit}} = \min_k \max(t_{0,k}, t_{1,k})$; hit iff $t_{\text{enter}} \le t_{\text{exit}} \land t_{\text{exit}} \ge 0$.

---

## 6. Verification Criteria (`test_physics`)

1. **Analytical Manifold Accuracy**: Sphere-Sphere, AABB-AABB, Sphere-AABB, and Capsule-Sphere tests return exact analytical normals and penetration depths.
2. **Restitution & Stacking Stability**:
   - Dropping an elastic sphere ($e = 1.0$) from $y = 10$ onto a static floor at $y = 0$ rebounds to $y \approx 10$.
   - Stacking 5 dynamic boxes ($e = 0.0, \mu = 0.6$) converges to a stable resting stack within 120 fixed steps without sinking or exploding.
3. **`ScreenCellToRay` Center Invariant**:
   - Calling `ScreenCellToRay` at the exact screen center $(\text{term\_w}/2 - 0.5, \text{term\_h}/2 - 0.5)$ produces a world ray direction identical to the camera's forward vector $\vec{f}$ ($\hat{d}_{\text{world}} \cdot \vec{f} > 0.99999$).
