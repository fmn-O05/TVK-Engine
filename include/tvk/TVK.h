#pragma once

// ============================================================================
// TVK Engine — Umbrella Header
// ============================================================================

// Layer 0: Core
#include "tvk/core/Types.h"
#include "tvk/core/Stats.h"
#include "tvk/core/JobSystem.h"

// Layer 1: Platform Abstraction Layer (PAL)
#include "tvk/platform/PAL.h"

// Layer 2: Math & SIMD Kernels
#include "tvk/math/Math.h"
#include "tvk/math/MathSIMD.h"

// Layer 3: 3D Software Renderer & Terminal Presenter
#include "tvk/renderer/Camera.h"
#include "tvk/renderer/CommandBuffer.h"
#include "tvk/renderer/Fog.h"
#include "tvk/renderer/Mesh.h"
#include "tvk/renderer/Primitives.h"
#include "tvk/renderer/Rasterizer.h"
#include "tvk/renderer/Renderer.h"
#include "tvk/renderer/TerminalPresenter.h"
