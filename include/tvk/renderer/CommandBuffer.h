#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>
#include "tvk/math/Math.h"
#include "tvk/renderer/Mesh.h"

namespace tvk {

    class Camera;

    enum class CullMode : uint8_t { None, Back, Front };
    enum class DepthOp : uint8_t  {
        Less = 0,
        Always = 1,
        Equal = 2,
        Greater = 3,
        LessOrEqual = 4,
        GreaterOrEqual = 5,
        NotEqual = 6,
        Never = 7
    };

    enum class ShadingModel : uint8_t { Unlit, Flat, Gouraud, BlinnPhong, Toon };
    enum class LightType : uint8_t { Directional, Point, Spot };

    struct MaterialState {
        float ambient   = 0.15f;
        float diffuse   = 0.85f;
        float specular  = 0.40f;
        float shininess = 32.0f;
        ShadingModel shadingModel = ShadingModel::BlinnPhong;
        int toonSteps = 3;
        float toonSpecularThreshold = 0.5f;
    };

    struct Light {
        LightType type = LightType::Point;
        Vec3 position {0.0f, 100.0f, 0.0f};
        Vec3 direction {0.0f, -1.0f, 0.0f};
        Color color {255, 255, 255, 255};
        float intensity = 1.0f;
        float range = 1000.0f;
        float spotInnerCutoff = 0.95f; // cos of inner cone angle
        float spotOuterCutoff = 0.85f; // cos of outer cone angle
        float attenConstant = 1.0f;
        float attenLinear = 0.09f;
        float attenQuadratic = 0.032f;
    };

    struct PipelineState {
        CullMode      cullMode   = CullMode::Back;
        DepthOp       depthOp    = DepthOp::Less;
        MaterialState material;
        uint8_t       pass       = 1;
        uint8_t       layer      = 0;
        uint32_t      materialId = 0;
    };

    enum class CommandType {
        DrawMesh,
        DrawSphere,
        DrawCylinder,
        DrawCapsule,
        DrawGround,
        DrawLine,
    };

    struct SortKey {
        union {
            uint64_t key = 0;
            uint64_t value;
        };

        static constexpr uint64_t kPassShift     = 60u;
        static constexpr uint64_t kLayerShift    = 56u;
        static constexpr uint64_t kDepthOpShift  = 52u;
        static constexpr uint64_t kMaterialShift = 32u;
        static constexpr uint64_t kDepthShift    = 16u;
        static constexpr uint64_t kSeqShift      = 0u;

        static constexpr uint64_t kPassMask      = 0x0Full;
        static constexpr uint64_t kLayerMask     = 0x0Full;
        static constexpr uint64_t kDepthOpMask   = 0x0Full;
        static constexpr uint64_t kMaterialMask  = 0x0FFFFFull;
        static constexpr uint64_t kDepthMask     = 0xFFFFull;
        static constexpr uint64_t kSeqMask       = 0xFFFFull;

        constexpr SortKey() noexcept : key(0) {}
        constexpr SortKey(uint64_t k) noexcept : key(k) {}

        [[nodiscard]] static constexpr uint64_t EncodeSortKey(
            uint8_t pass, uint8_t layer, DepthOp depthOp,
            uint32_t materialId, uint16_t quantizedDepth, uint16_t sequence) noexcept {
            return (static_cast<uint64_t>(pass & kPassMask) << kPassShift)
                 | (static_cast<uint64_t>(layer & kLayerMask) << kLayerShift)
                 | (static_cast<uint64_t>(static_cast<uint8_t>(depthOp) & kDepthOpMask) << kDepthOpShift)
                 | (static_cast<uint64_t>(materialId & kMaterialMask) << kMaterialShift)
                 | (static_cast<uint64_t>(quantizedDepth & kDepthMask) << kDepthShift)
                 | (static_cast<uint64_t>(sequence & kSeqMask) << kSeqShift);
        }

        [[nodiscard]] static constexpr SortKey Encode(
            uint8_t pass, uint8_t layer, DepthOp depthOp,
            uint32_t materialId, uint16_t quantizedDepth, uint16_t sequence) noexcept {
            return SortKey(EncodeSortKey(pass, layer, depthOp, materialId, quantizedDepth, sequence));
        }

        [[nodiscard]] static constexpr uint64_t Pack(
            uint8_t pass, uint8_t layer, DepthOp depthOp,
            uint32_t materialId, uint16_t quantizedDepth, uint16_t sequence) noexcept {
            return EncodeSortKey(pass, layer, depthOp, materialId, quantizedDepth, sequence);
        }

        // Static extraction helpers
        [[nodiscard]] static constexpr uint8_t ExtractPass(uint64_t k) noexcept {
            return static_cast<uint8_t>((k >> kPassShift) & kPassMask);
        }
        [[nodiscard]] static constexpr uint8_t ExtractLayer(uint64_t k) noexcept {
            return static_cast<uint8_t>((k >> kLayerShift) & kLayerMask);
        }
        [[nodiscard]] static constexpr DepthOp ExtractDepthOp(uint64_t k) noexcept {
            return static_cast<DepthOp>((k >> kDepthOpShift) & kDepthOpMask);
        }
        [[nodiscard]] static constexpr uint32_t ExtractMaterialId(uint64_t k) noexcept {
            return static_cast<uint32_t>((k >> kMaterialShift) & kMaterialMask);
        }
        [[nodiscard]] static constexpr uint16_t ExtractDepth(uint64_t k) noexcept {
            return static_cast<uint16_t>((k >> kDepthShift) & kDepthMask);
        }
        [[nodiscard]] static constexpr uint16_t ExtractSequence(uint64_t k) noexcept {
            return static_cast<uint16_t>((k >> kSeqShift) & kSeqMask);
        }

        // Member getters
        [[nodiscard]] constexpr uint8_t GetPass() const noexcept { return ExtractPass(key); }
        [[nodiscard]] constexpr uint8_t GetLayer() const noexcept { return ExtractLayer(key); }
        [[nodiscard]] constexpr DepthOp GetDepthOp() const noexcept { return ExtractDepthOp(key); }
        [[nodiscard]] constexpr uint32_t GetMaterialId() const noexcept { return ExtractMaterialId(key); }
        [[nodiscard]] constexpr uint16_t GetDepth() const noexcept { return ExtractDepth(key); }
        [[nodiscard]] constexpr uint16_t GetSequence() const noexcept { return ExtractSequence(key); }

        constexpr operator uint64_t() const noexcept { return key; }
        constexpr bool operator<(const SortKey& o) const noexcept { return key < o.key; }
        constexpr bool operator<=(const SortKey& o) const noexcept { return key <= o.key; }
        constexpr bool operator>(const SortKey& o) const noexcept { return key > o.key; }
        constexpr bool operator>=(const SortKey& o) const noexcept { return key >= o.key; }
        constexpr bool operator==(const SortKey& o) const noexcept { return key == o.key; }
        constexpr bool operator!=(const SortKey& o) const noexcept { return key != o.key; }
    };

    [[nodiscard]] inline constexpr uint64_t EncodeSortKey(
        uint8_t pass, uint8_t layer, DepthOp depthOp,
        uint32_t materialId, uint16_t quantizedDepth, uint16_t sequence) noexcept {
        return SortKey::EncodeSortKey(pass, layer, depthOp, materialId, quantizedDepth, sequence);
    }

    struct RenderCommand {
        CommandType   type;
        PipelineState pso;

        const Mesh*   mesh      = nullptr;
        Mat4          transform {};

        Vec3          a{}, b{};
        float         radius    = 0.0f;
        uint8_t       shade     = 0;

        Color         color     = {255, 255, 255, 255};
        uint64_t      sortKey   = 0;
    };

    class CommandBuffer {
    public:
        void Begin();
        void End();
        void Reset();
        void Clear() { Reset(); }

        void BindPipeline(const PipelineState& pso);

        void DrawMesh    (const Mesh& mesh, const Mat4& transform, Color color = {255, 255, 255, 255});
        void DrawSphere  (const Vec3& center, float radius, uint8_t shade);
        void DrawSphere  (const Vec3& center, float radius, Color color);
        void DrawCylinder(const Vec3& a, const Vec3& b, float radius, uint8_t shade);
        void DrawCylinder(const Vec3& a, const Vec3& b, float radius, Color color);
        void DrawCapsule (const Vec3& a, const Vec3& b, float radius, uint8_t shade);
        void DrawCapsule (const Vec3& a, const Vec3& b, float radius, Color color);
        void DrawGround  (float y_level, float half_extent, uint8_t shade);
        void DrawGround  (float y_level, float half_extent, Color color);
        void DrawLine    (const Vec3& a, const Vec3& b, Color color = {255, 255, 255, 255});

        void DrawWireBox   (const Vec3& min, const Vec3& max, Color color);
        void DrawWireBox   (const AABB& box, Color color);
        void DrawWireSphere(const Vec3& center, float radius, Color color, int segments = 16);
        void DrawGrid      (float size, float step, Color color);
        void DrawGrid      (float y_level, float half_extent, float grid_spacing, Color color);
        void DrawAxes      (const Mat4& transform, float length = 1.0f);
        void DrawAxes      (const Vec3& origin, float length = 1.0f);
        void DrawFrustum   (const Mat4& invViewProj, Color color);
        void DrawLines     (const Vec3* points, size_t count, Color color);
        void DrawLines     (const std::vector<Vec3>& points, Color color);

        void Sort(const Camera* camera = nullptr);
        void Sort(const Camera& camera) { Sort(&camera); }

        const std::vector<RenderCommand>& GetCommands() const { return m_Commands; }
        std::vector<RenderCommand>& GetCommands() { return m_Commands; }
        uint16_t GetSequence() const { return m_Sequence; }

    private:
        void StampAndPush(RenderCommand& cmd);

        std::vector<RenderCommand> m_Commands;
        PipelineState              m_CurrentPipeline;
        uint16_t                   m_Sequence = 0;
        bool                       m_IsRecording = false;
    };

}
