#include "TestHarness.h"
#include "tvk/TVK.h"
#include <atomic>
#include <vector>

using namespace tvk;

namespace {
struct DummyResource {
  int value = 0;
  DummyResource() = default;
  explicit DummyResource(int v) : value(v) {}
};
struct DummyTag {};
} // namespace

TVK_TEST(Types_ColorAndBlitterRatios) {
  Color c1 = Color::FromRGB(100, 150, 200);
  Color c2 = {255, 128, 64, 255};
  Color mod = Color::Modulate(c1, c2);
  TVK_EXPECT_EQ(mod.r, 100u);
  TVK_EXPECT_EQ(mod.g, static_cast<uint8_t>((150u * 128u) / 255u));
  TVK_EXPECT_EQ(mod.b, static_cast<uint8_t>((200u * 64u) / 255u));
  TVK_EXPECT_TRUE(Color::White().Luminance() >= 250);
  TVK_EXPECT_EQ(Color::Black().Luminance(), 0u);

  int sw = 0, sh = 0;
  GetBlitterSubpixelRatio(BlitterType::Braille, sw, sh);
  TVK_EXPECT_EQ(sw, 2);
  TVK_EXPECT_EQ(sh, 4);

  GetBlitterSubpixelRatio(BlitterType::HalfBlock, sw, sh);
  TVK_EXPECT_EQ(sw, 1);
  TVK_EXPECT_EQ(sh, 2);

  GetBlitterSubpixelRatio(BlitterType::Quadrant, sw, sh);
  TVK_EXPECT_EQ(sw, 2);
  TVK_EXPECT_EQ(sh, 2);

  GetBlitterSubpixelRatio(BlitterType::Sextant, sw, sh);
  TVK_EXPECT_EQ(sw, 2);
  TVK_EXPECT_EQ(sh, 3);

  GetBlitterSubpixelRatio(BlitterType::ASCII, sw, sh);
  TVK_EXPECT_EQ(sw, 1);
  TVK_EXPECT_EQ(sh, 1);
}

TVK_TEST(HandlePool_CreateDestroyReuseAndStaleRejection) {
  HandlePool<DummyResource, DummyTag> pool(8);
  TVK_EXPECT_EQ(pool.Count(), 0u);

  Handle<DummyTag> invalid = Handle<DummyTag>::Invalid();
  TVK_EXPECT_FALSE(invalid.IsValid());
  TVK_EXPECT_FALSE(pool.IsValid(invalid));
  TVK_EXPECT_TRUE(pool.Get(invalid) == nullptr);

  Handle<DummyTag> h1 = pool.Create(42);
  Handle<DummyTag> h2 = pool.Create(99);
  TVK_EXPECT_TRUE(pool.IsValid(h1));
  TVK_EXPECT_TRUE(pool.IsValid(h2));
  TVK_EXPECT_EQ(pool.Count(), 2u);
  TVK_EXPECT_EQ(pool.Get(h1)->value, 42);
  TVK_EXPECT_EQ(pool.Get(h2)->value, 99);

  // Destroy h1 and verify stale handle rejection
  TVK_EXPECT_TRUE(pool.Destroy(h1));
  TVK_EXPECT_FALSE(pool.IsValid(h1));
  TVK_EXPECT_TRUE(pool.Get(h1) == nullptr);
  TVK_EXPECT_FALSE(pool.Destroy(h1));
  TVK_EXPECT_EQ(pool.Count(), 1u);

  // Reuse slot 0 with bumped generation
  Handle<DummyTag> h3 = pool.Create(123);
  TVK_EXPECT_TRUE(pool.IsValid(h3));
  TVK_EXPECT_EQ(h3.Index(), h1.Index());
  TVK_EXPECT_TRUE(h3.Generation() != h1.Generation());
  TVK_EXPECT_FALSE(pool.IsValid(h1));
  TVK_EXPECT_EQ(pool.Get(h3)->value, 123);

  pool.Clear();
  TVK_EXPECT_EQ(pool.Count(), 0u);
  TVK_EXPECT_FALSE(pool.IsValid(h2));
  TVK_EXPECT_FALSE(pool.IsValid(h3));
}

TVK_TEST(Result_OkAndErrSemantics) {
  auto okRes = Result<int>::Ok(77);
  TVK_EXPECT_TRUE(okRes.IsOk());
  TVK_EXPECT_FALSE(okRes.IsErr());
  TVK_EXPECT_TRUE(static_cast<bool>(okRes));
  TVK_EXPECT_EQ(okRes.Value(), 77);
  TVK_EXPECT_EQ(okRes.ValueOr(10), 77);

  auto errRes = Result<int>::Err(ErrorCode::NotFound);
  TVK_EXPECT_FALSE(errRes.IsOk());
  TVK_EXPECT_TRUE(errRes.IsErr());
  TVK_EXPECT_FALSE(static_cast<bool>(errRes));
  TVK_EXPECT_EQ(errRes.Error(), ErrorCode::NotFound);
  TVK_EXPECT_EQ(errRes.ValueOr(10), 10);

  auto voidOk = Result<void>::Ok();
  TVK_EXPECT_TRUE(voidOk.IsOk());
  auto voidErr = Result<void>::Err(ErrorCode::InvalidArgument);
  TVK_EXPECT_TRUE(voidErr.IsErr());
  TVK_EXPECT_EQ(voidErr.Error(), ErrorCode::InvalidArgument);
}

TVK_TEST(JobSystem_ZeroThreadClampAndParallelFor) {
  // B-22: numThreads <= 0 must clamp to 1 without underflow
  JobSystem jsSingle(0);
  TVK_EXPECT_EQ(jsSingle.GetNumThreads(), 1);

  std::atomic<int> sumSingle{0};
  jsSingle.ParallelFor(100u, 7u, [&](uint32_t i) {
    sumSingle.fetch_add(static_cast<int>(i), std::memory_order_relaxed);
  });
  TVK_EXPECT_EQ(sumSingle.load(), 4950);

  // Multi-worker stress test across 500 dispatches (verifies B-19 mutex fix)
  JobSystem jsMulti(4);
  TVK_EXPECT_EQ(jsMulti.GetNumThreads(), 4);

  std::vector<int> data(256, 0);
  for (int iter = 0; iter < 500; ++iter) {
    jsMulti.ParallelFor(256u, 16u, [&](uint32_t i) { data[i] += 1; });
  }
  for (int v : data) {
    TVK_EXPECT_EQ(v, 500);
  }
}

TVK_TEST(PAL_TimestampsAndResizeFlag) {
  uint64_t t0 = pal::GetTimestampNs();
  uint64_t t1 = pal::GetTimestampNs();
  TVK_EXPECT_TRUE(t1 >= t0);
  TVK_EXPECT_FALSE(pal::ConsumeResizeEvent());
}

int main() { return TVK_RUN_ALL_TESTS(); }
