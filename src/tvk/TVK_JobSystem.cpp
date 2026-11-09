#include "tvk/TVK_JobSystem.h"

namespace tvk {

JobSystem::JobSystem(int numThreads) : m_NumThreads(numThreads) {
  m_Workers.reserve(numThreads - 1);
  for (int i = 1; i < m_NumThreads; ++i) {
    m_Workers.emplace_back(&JobSystem::WorkerLoop, this, i);
  }
}

JobSystem::~JobSystem() {
  {
    std::lock_guard<std::mutex> lock(m_WakeMutex);
    m_Shutdown.store(true, std::memory_order_release);
  }
  m_WakeCV.notify_all();

  for (auto &worker : m_Workers) {
    if (worker.joinable()) {
      worker.join();
    }
  }
}

void JobSystem::WorkerLoop(int worker_id) {
  uint32_t local_generation = 0;

  while (true) {
    // 1. Wait for a new job phase or shutdown
    int spinCount = 0;
    while (m_JobGeneration.load(std::memory_order_acquire) ==
               local_generation &&
           !m_Shutdown.load(std::memory_order_acquire)) {
      if (spinCount < 4000) {
        _mm_pause();
        ++spinCount;
      } else if (spinCount < 10000) {
        std::this_thread::yield();
        ++spinCount;
      } else {
        std::unique_lock<std::mutex> lock(m_WakeMutex);
        m_WakeCV.wait(lock, [this, &local_generation]() {
          return m_JobGeneration.load(std::memory_order_acquire) !=
                     local_generation ||
                 m_Shutdown.load(std::memory_order_acquire);
        });
        break;
      }
    }

    if (m_Shutdown.load(std::memory_order_acquire)) {
      break;
    }

    // Sync to the new generation
    local_generation = m_JobGeneration.load(std::memory_order_acquire);

    // 2. Execute the tight loop task
    if (m_TaskCallback && m_TaskContext) {
      m_TaskCallback(worker_id, m_TaskContext);
    }

    // 3. Mark thread as finished dynamically (main thread spins on this)
    m_TasksActive.fetch_sub(1, std::memory_order_acq_rel);
  }
}

} // namespace tvk