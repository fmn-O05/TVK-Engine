#pragma once
#include <atomic>
#include <condition_variable>
#include <emmintrin.h>
#include <mutex>
#include <thread>
#include <vector>

namespace tvk {

class HybridBarrier {
public:
  explicit HybridBarrier(int count)
      : m_Count(count), m_Generation(0), m_Threshold(count) {}

  void Wait() {
    int gen = m_Generation.load(std::memory_order_acquire);
    if (m_Count.fetch_sub(1, std::memory_order_acq_rel) == 1) {

      m_Count.store(m_Threshold, std::memory_order_release);
      m_Generation.fetch_add(1, std::memory_order_release);
    } else {

      int spinCount = 0;
      while (m_Generation.load(std::memory_order_acquire) == gen) {
        if (spinCount < 1000) {
          _mm_pause();
          ++spinCount;
        } else {

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

  template <typename F> void Execute(F &&func) {
    m_TaskContext = &func;
    m_TaskCallback = [](int worker_id, void *ctx) {
      (*static_cast<std::remove_reference_t<F> *>(ctx))(worker_id);
    };

    {
      std::lock_guard<std::mutex> lock(m_WakeMutex);
      m_TasksActive.store(m_NumThreads, std::memory_order_release);
      m_JobGeneration.fetch_add(1, std::memory_order_release);
    }
    m_WakeCV.notify_all();

    m_TaskCallback(0, m_TaskContext);

    m_TasksActive.fetch_sub(1, std::memory_order_acq_rel);

    int spinCount = 0;
    while (m_TasksActive.load(std::memory_order_acquire) > 0) {
      if (spinCount < 4000) {
        _mm_pause();
        ++spinCount;
      } else {
        std::this_thread::yield();
      }
    }

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

  void *m_TaskContext{nullptr};
  void (*m_TaskCallback)(int, void *){nullptr};
};

}
