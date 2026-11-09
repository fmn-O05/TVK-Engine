#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

namespace tvk {

inline void CpuRelax() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
  _mm_pause();
#elif defined(__aarch64__) || defined(_M_ARM64)
  __asm__ __volatile__("yield" ::: "memory");
#else
  std::this_thread::yield();
#endif
}

#define TVK_CPU_PAUSE() ::tvk::CpuRelax()

struct AtomicQueue {
  std::atomic<int> current{0};
  int total{0};

  void Reset(int t) {
    current.store(0, std::memory_order_relaxed);
    total = t;
  }

  inline int Next() { return current.fetch_add(1, std::memory_order_relaxed); }
};

class JobSystem {
public:
  explicit JobSystem(int numThreads);
  ~JobSystem();

  JobSystem(const JobSystem &) = delete;
  JobSystem &operator=(const JobSystem &) = delete;
  JobSystem(JobSystem &&) = delete;
  JobSystem &operator=(JobSystem &&) = delete;

  int GetNumThreads() const noexcept { return m_NumThreads; }

  template <typename F>
  void Execute(F &&func) {
    {
      std::lock_guard<std::mutex> lock(m_WakeMutex);
      m_TaskContext = &func;
      m_TaskCallback = [](int worker_id, void *ctx) {
        (*static_cast<std::remove_reference_t<F> *>(ctx))(worker_id);
      };
      m_TasksActive.store(m_NumThreads, std::memory_order_release);
      m_JobGeneration.fetch_add(1, std::memory_order_release);
    }
    m_WakeCV.notify_all();

    m_TaskCallback(0, m_TaskContext);

    m_TasksActive.fetch_sub(1, std::memory_order_acq_rel);

    int spinCount = 0;
    while (m_TasksActive.load(std::memory_order_acquire) > 0) {
      if (spinCount < 4000) {
        CpuRelax();
        ++spinCount;
      } else {
        std::this_thread::yield();
      }
    }

    {
      std::lock_guard<std::mutex> lock(m_WakeMutex);
      m_TaskContext = nullptr;
      m_TaskCallback = nullptr;
    }
  }

  template <typename Func>
  void ParallelFor(uint32_t count, uint32_t grainSize, const Func &func) {
    if (count == 0)
      return;
    const uint32_t grain = std::max(1u, grainSize);
    const uint32_t numChunks = (count + grain - 1u) / grain;
    AtomicQueue queue;
    queue.Reset(static_cast<int>(numChunks));

    Execute([&](int worker_id) {
      while (true) {
        const int chunkIdx = queue.Next();
        if (chunkIdx >= static_cast<int>(numChunks))
          break;
        const uint32_t start = static_cast<uint32_t>(chunkIdx) * grain;
        const uint32_t end = std::min(start + grain, count);
        if constexpr (std::is_invocable_v<Func, uint32_t, uint32_t, int>) {
          func(start, end, worker_id);
        } else if constexpr (std::is_invocable_v<Func, uint32_t, uint32_t>) {
          (void)worker_id;
          func(start, end);
        } else if constexpr (std::is_invocable_v<Func, uint32_t, int>) {
          for (uint32_t i = start; i < end; ++i) {
            func(i, worker_id);
          }
        } else {
          (void)worker_id;
          for (uint32_t i = start; i < end; ++i) {
            func(i);
          }
        }
      }
    });
  }

private:
  void WorkerLoop(int worker_id);

  int m_NumThreads;
  std::vector<std::thread> m_Workers;
  std::atomic<bool> m_Shutdown{false};

  std::mutex m_WakeMutex;
  std::condition_variable m_WakeCV;
  std::atomic<uint32_t> m_JobGeneration{0};

  std::atomic<int> m_TasksActive{0};

  void *m_TaskContext{nullptr};
  void (*m_TaskCallback)(int, void *){nullptr};
};

} // namespace tvk
