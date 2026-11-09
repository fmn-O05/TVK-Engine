#pragma once
#include <atomic>
#include <condition_variable>
#include <emmintrin.h>
#include <mutex>
#include <thread>
#include <vector>


namespace tvk {

// Hybrid sync primitive: extremely fast spin-wait for intra-frame latency,
// gracefully falls back to OS yield if preempted to save power and prevent
// deadlocks.
class HybridBarrier {
public:
  explicit HybridBarrier(int count)
      : m_Count(count), m_Generation(0), m_Threshold(count) {}

  void Wait() {
    int gen = m_Generation.load(std::memory_order_acquire);
    if (m_Count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
      // Last thread to reach the barrier, reset count and flip generation
      m_Count.store(m_Threshold, std::memory_order_release);
      m_Generation.fetch_add(1, std::memory_order_release);
    } else {
      // Spin-wait for the generation to change
      int spinCount = 0;
      while (m_Generation.load(std::memory_order_acquire) == gen) {
        if (spinCount < 1000) {
          _mm_pause();
          ++spinCount;
        } else {
          // Fallback to avoid burning CPU maxed out or starvation
          std::this_thread::yield();
        }
      }
    }
  }

private:
  std::atomic<int> m_Count;
  std::atomic<int> m_Generation;
  int m_Threshold;
};

// Generic atomic queue replacing legacy phase-specific atomics
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

  int GetNumThreads() const { return m_NumThreads; }

  // Zero-cost abstraction for dispatching a job to all worker threads
  // No std::function allocations; deduced inline and type-erased properly.
  template <typename F> void Execute(F &&func) {
    m_TaskContext = &func;
    m_TaskCallback = [](int worker_id, void *ctx) {
      (*static_cast<std::remove_reference_t<F> *>(ctx))(worker_id);
    };

    // Wake sleeping background threads
    {
      std::lock_guard<std::mutex> lock(m_WakeMutex);
      m_TasksActive.store(m_NumThreads, std::memory_order_release);
      m_JobGeneration.fetch_add(1, std::memory_order_release);
    }
    m_WakeCV.notify_all();

    // Main thread participates as worker 0
    m_TaskCallback(0, m_TaskContext);

    // Main thread finished — decrement the active counter
    m_TasksActive.fetch_sub(1, std::memory_order_acq_rel);

    // Inter-frame fast local spin-yield loop
    int spinCount = 0;
    while (m_TasksActive.load(std::memory_order_acquire) > 0) {
      if (spinCount < 4000) {
        _mm_pause();
        ++spinCount;
      } else {
        std::this_thread::yield();
      }
    }

    // Close job state (No longer needed to toggle a boolean, workers wait on
    // generation change)
    m_TaskContext = nullptr;
    m_TaskCallback = nullptr;
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

  // Type erased pointer and wrapper to bypass std::function SBO / Heap
  // allocation misses
  void *m_TaskContext{nullptr};
  void (*m_TaskCallback)(int, void *){nullptr};
};

} // namespace tvk