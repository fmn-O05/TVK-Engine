#include "tvk/TVK.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

using namespace tvk;

namespace sandbox {

    enum class AsteroidShape {
        CubeMesh,
        Sphere,
        Capsule
    };

    tvk::Mesh CreateDemoCube() {
        std::vector<tvk::Vertex> verts = {
            {{-1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {120, 0, 0, 255}},
            {{ 1.0f, -1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {120, 0, 0, 255}},
            {{ 1.0f,  1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {120, 0, 0, 255}},
            {{-1.0f,  1.0f, -1.0f}, {0.0f, 0.0f, -1.0f}, {120, 0, 0, 255}},
            {{ 1.0f, -1.0f,  1.0f}, {0.0f, 0.0f,  1.0f}, {145, 0, 0, 255}},
            {{-1.0f, -1.0f,  1.0f}, {0.0f, 0.0f,  1.0f}, {145, 0, 0, 255}},
            {{-1.0f,  1.0f,  1.0f}, {0.0f, 0.0f,  1.0f}, {145, 0, 0, 255}},
            {{ 1.0f,  1.0f,  1.0f}, {0.0f, 0.0f,  1.0f}, {145, 0, 0, 255}},
            {{-1.0f, -1.0f,  1.0f}, {-1.0f, 0.0f, 0.0f}, {100, 0, 0, 255}},
            {{-1.0f, -1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {100, 0, 0, 255}},
            {{-1.0f,  1.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {100, 0, 0, 255}},
            {{-1.0f,  1.0f,  1.0f}, {-1.0f, 0.0f, 0.0f}, {100, 0, 0, 255}},
            {{ 1.0f, -1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {155, 0, 0, 255}},
            {{ 1.0f, -1.0f,  1.0f}, {1.0f, 0.0f, 0.0f}, {155, 0, 0, 255}},
            {{ 1.0f,  1.0f,  1.0f}, {1.0f, 0.0f, 0.0f}, {155, 0, 0, 255}},
            {{ 1.0f,  1.0f, -1.0f}, {1.0f, 0.0f, 0.0f}, {155, 0, 0, 255}},
            {{-1.0f,  1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {130, 0, 0, 255}},
            {{ 1.0f,  1.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {130, 0, 0, 255}},
            {{ 1.0f,  1.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, {130, 0, 0, 255}},
            {{-1.0f,  1.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, {130, 0, 0, 255}},
            {{-1.0f, -1.0f,  1.0f}, {0.0f, -1.0f, 0.0f}, {80, 0, 0, 255}},
            {{ 1.0f, -1.0f,  1.0f}, {0.0f, -1.0f, 0.0f}, {80, 0, 0, 255}},
            {{ 1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {80, 0, 0, 255}},
            {{-1.0f, -1.0f, -1.0f}, {0.0f, -1.0f, 0.0f}, {80, 0, 0, 255}},
        };

        std::vector<uint32_t> indices = {
            0, 1, 2,  2, 3, 0,
            4, 5, 6,  6, 7, 4,
            8, 9, 10, 10, 11, 8,
            12, 13, 14, 14, 15, 12,
            16, 17, 18, 18, 19, 16,
            20, 21, 22, 22, 23, 20
        };

        return tvk::Mesh(std::move(verts), std::move(indices));
    }

    tvk::Mat4 CalculateTransform(tvk::Quat rot, tvk::Vec3 pos, float scale) {
        tvk::Mat4 rs = tvk::QuatToMat4(rot);
        for(int r = 0; r < 3; ++r) {
            rs.m[r][0] *= scale;
            rs.m[r][1] *= scale;
            rs.m[r][2] *= scale;
        }
        rs.m[0][3] = pos.x;
        rs.m[1][3] = pos.y;
        rs.m[2][3] = pos.z;
        return rs;
    }

    class AsteroidFieldScene {
    public:
        AsteroidFieldScene(int asteroidCount, float fieldRadius)
            : m_AsteroidCount(asteroidCount), m_FieldRadius(fieldRadius)
        {
            m_SharedCubeMesh = CreateDemoCube();

            struct TempAst {
                AsteroidShape shape;
                tvk::Vec3 position;
                tvk::Quat rotation;
                tvk::Vec3 angularVelocity;
                tvk::Vec3 linearVelocity;
                float scale;
                uint8_t baseShade;
                tvk::PipelineState pso;
            };
            std::vector<TempAst> temps;
            temps.reserve(static_cast<size_t>(asteroidCount));

            std::mt19937 rng(42);
            std::uniform_real_distribution<float> distPos(-fieldRadius, fieldRadius);
            std::uniform_real_distribution<float> distRot(-1.0f, 1.0f);
            std::uniform_real_distribution<float> distVel(-2.5f, 2.5f);
            std::uniform_real_distribution<float> distScale(0.8f, 3.5f);
            std::uniform_int_distribution<int> distShade(60, 155);
            std::uniform_int_distribution<int> distShape(0, 2);
            std::uniform_int_distribution<int> distBool(0, 1);

            for (int i = 0; i < asteroidCount; ++i) {
                TempAst ast;
                ast.shape = static_cast<AsteroidShape>(distShape(rng));
                ast.position = tvk::Vec3{distPos(rng), distPos(rng) * 0.4f, distPos(rng)};

                tvk::Quat r = {distRot(rng), distRot(rng), distRot(rng), distRot(rng)};
                ast.rotation = tvk::QuatNormalize(r);

                ast.angularVelocity = tvk::Vec3{distVel(rng), distVel(rng), distVel(rng)};
                ast.linearVelocity = tvk::Vec3{distVel(rng) * 0.5f, distVel(rng) * 0.2f, distVel(rng) * 0.5f};
                ast.scale = distScale(rng);
                ast.baseShade = static_cast<uint8_t>(distShade(rng));

                ast.pso.cullMode = distBool(rng) ? tvk::CullMode::Back : tvk::CullMode::None;
                ast.pso.depthOp = tvk::DepthOp::Less;

                temps.push_back(ast);
            }

            std::sort(temps.begin(), temps.end(), [](const TempAst& a, const TempAst& b) {
                if (a.pso.cullMode != b.pso.cullMode) return a.pso.cullMode < b.pso.cullMode;
                if (a.pso.depthOp != b.pso.depthOp) return a.pso.depthOp < b.pso.depthOp;
                return a.shape < b.shape;
            });

            m_Shapes.reserve(static_cast<size_t>(asteroidCount));
            m_PSOs.reserve(static_cast<size_t>(asteroidCount));
            m_Positions.reserve(static_cast<size_t>(asteroidCount));
            m_Rotations.reserve(static_cast<size_t>(asteroidCount));
            m_AngularVelocity.reserve(static_cast<size_t>(asteroidCount));
            m_LinearVelocity.reserve(static_cast<size_t>(asteroidCount));
            m_Scales.reserve(static_cast<size_t>(asteroidCount));
            m_Shades.reserve(static_cast<size_t>(asteroidCount));

            for(const auto& t : temps) {
                m_Shapes.push_back(t.shape);
                m_PSOs.push_back(t.pso);
                m_Positions.push_back(t.position);
                m_Rotations.push_back(t.rotation);
                m_AngularVelocity.push_back(t.angularVelocity);
                m_LinearVelocity.push_back(t.linearVelocity);
                m_Scales.push_back(t.scale);
                m_Shades.push_back(t.baseShade);
            }
        }

        void Update(float dt, float totalTime, tvk::Camera& outCamera) {
            for (size_t i = 0; i < static_cast<size_t>(m_AsteroidCount); ++i) {
                m_Positions[i].x += m_LinearVelocity[i].x * dt;
                m_Positions[i].y += m_LinearVelocity[i].y * dt;
                m_Positions[i].z += m_LinearVelocity[i].z * dt;

                m_Rotations[i] = tvk::QuatIntegrate(m_Rotations[i], m_AngularVelocity[i], dt);

                if (m_Positions[i].x > m_FieldRadius) m_Positions[i].x -= m_FieldRadius * 2.0f;
                if (m_Positions[i].x < -m_FieldRadius) m_Positions[i].x += m_FieldRadius * 2.0f;

                if (m_Positions[i].y > m_FieldRadius) m_Positions[i].y -= m_FieldRadius * 2.0f;
                if (m_Positions[i].y < -m_FieldRadius) m_Positions[i].y += m_FieldRadius * 2.0f;

                if (m_Positions[i].z > m_FieldRadius) m_Positions[i].z -= m_FieldRadius * 2.0f;
                if (m_Positions[i].z < -m_FieldRadius) m_Positions[i].z += m_FieldRadius * 2.0f;
            }

            float camYaw = totalTime * 0.35f;
            float camPitch = 0.2f + 0.3f * std::sin(totalTime * 0.5f);
            outCamera.Orbit(camYaw, camPitch);
            outCamera.Zoom(60.0f + 20.0f * std::sin(totalTime * 0.2f));
        }

        void EncodeCommands(tvk::CommandBuffer& cmd) const {
            cmd.Begin();

            tvk::PipelineState last_pso;
            bool pso_init = false;

            for (size_t i = 0; i < static_cast<size_t>(m_AsteroidCount); ++i) {
                if (!pso_init || m_PSOs[i].cullMode != last_pso.cullMode || m_PSOs[i].depthOp != last_pso.depthOp) {
                    cmd.BindPipeline(m_PSOs[i]);
                    last_pso = m_PSOs[i];
                    pso_init = true;
                }

                tvk::Mat4 transform = CalculateTransform(m_Rotations[i], m_Positions[i], m_Scales[i]);

                if (m_Shapes[i] == AsteroidShape::CubeMesh) {
                    cmd.DrawMesh(m_SharedCubeMesh, transform);
                } else if (m_Shapes[i] == AsteroidShape::Sphere) {
                    cmd.DrawSphere(m_Positions[i], m_Scales[i], m_Shades[i]);
                } else if (m_Shapes[i] == AsteroidShape::Capsule) {
                    tvk::Vec3 off = {0.0f, m_Scales[i] * 1.5f, 0.0f};
                    tvk::Vec3 a = tvk::VectorTransform(off, transform);
                    off.y = -off.y;
                    tvk::Vec3 b = tvk::VectorTransform(off, transform);
                    cmd.DrawCapsule(a, b, m_Scales[i] * 0.5f, m_Shades[i]);
                }
            }
            cmd.End();
        }

    private:
        tvk::Mesh m_SharedCubeMesh;
        int m_AsteroidCount;
        float m_FieldRadius;

        std::vector<AsteroidShape> m_Shapes;
        std::vector<tvk::PipelineState> m_PSOs;
        std::vector<tvk::Vec3> m_Positions;
        std::vector<tvk::Quat> m_Rotations;
        std::vector<tvk::Vec3> m_AngularVelocity;
        std::vector<tvk::Vec3> m_LinearVelocity;
        std::vector<float> m_Scales;
        std::vector<uint8_t> m_Shades;
    };

}

int main(int argc, char **argv) {
    int maxFrames = 5000;
    bool headless = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            maxFrames = std::atoi(argv[++i]);
        } else if (std::strcmp(argv[i], "--headless") == 0) {
            headless = true;
        }
    }

    tvk::RendererDescriptor desc;
    desc.EnableAdaptiveDetail = false;
    desc.InitialQuality = tvk::QualityPreset::Balanced;
    desc.HeadlessFallback = headless;

    tvk::Renderer renderer(desc);
    tvk::Camera camera;

    sandbox::AsteroidFieldScene scene(100, 35.0f);

    float totalTime = 0.0f;
    const float dt = 0.016f;

    auto lastTime = std::chrono::high_resolution_clock::now();
    int frames = 0;
    float timeAccumulator = 0.0f;
    int currentFps = 0;
    float currentMs = 0.0f;

    tvk::CommandBuffer cmd;

    bool updateHUD = false;

    for (int frame = 0; frame < maxFrames; ++frame) {
        char ch = 0;
        bool shouldQuit = false;
        while (tvk::pal::PollKey(ch)) {
            if (ch == 'q' || ch == 'Q' || ch == 27) {
                shouldQuit = true;
            }
        }
        if (shouldQuit)
            break;

        auto currentTime = std::chrono::high_resolution_clock::now();
        float realDt = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;

        frames++;
        timeAccumulator += realDt;
        if (timeAccumulator >= 1.0f) {
            currentFps = frames;
            currentMs = (timeAccumulator * 1000.0f) / frames;
            frames = 0;
            timeAccumulator = 0.0f;
            updateHUD = true;
        }

        tvk::Vec3 light_pos = {
            40.0f * std::cos(totalTime * 0.3f),
            30.0f,
            40.0f * std::sin(totalTime * 0.3f)
        };

        scene.Update(dt, totalTime, camera);
        scene.EncodeCommands(cmd);

        renderer.BeginFrame(camera);
        renderer.SetGlobalLight(light_pos);
        renderer.Execute(cmd);
        renderer.Present();
        renderer.UpdateTerminalSize();

        if (!headless && updateHUD && currentFps > 0) {
            char hud[128];
            int len = std::snprintf(
                hud, sizeof(hud),
                "\x1b[1;1H\x1b[38;2;0;255;128m[ FPS: %4d | %.2f ms | ASTEROIDS: 100 ]\x1b[0m",
                currentFps, currentMs);
            if (len > 0) {
                tvk::pal::FastPrint(hud, static_cast<unsigned int>(len));
            }
            updateHUD = false;
        }

        totalTime += dt;
    }

    return 0;
}
