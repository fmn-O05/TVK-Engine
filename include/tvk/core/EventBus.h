#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tvk {

// ============================================================================
// Standard Core Event Definitions
// ============================================================================
struct WindowResizeEvent {
    int width = 0;
    int height = 0;
};

struct EngineExitEvent {
    int exitCode = 0;
};

// ============================================================================
// EventBus
// ============================================================================
class EventBus {
public:
    using SubscriptionId = uint32_t;

    EventBus() = default;
    ~EventBus() { Reset(); }

    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    /// Subscribes a callable handler to event type E
    template <typename E>
    SubscriptionId Subscribe(std::function<void(const E&)> handler) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        EventChannel<E>& channel = GetOrCreateChannel<E>();
        return channel.Add(std::move(handler));
    }

    /// Subscribes a member function listener
    template <typename E, typename Listener>
    SubscriptionId Subscribe(Listener* listener, void (Listener::*method)(const E&)) {
        return Subscribe<E>([listener, method](const E& event) {
            (listener->*method)(event);
        });
    }

    /// Unsubscribes a listener by SubscriptionId
    bool Unsubscribe(SubscriptionId id) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        for (auto& entry : m_Channels) {
            if (entry.second && entry.second->Remove(id)) {
                return true;
            }
        }
        return false;
    }

    /// Immediately publishes event E to all registered handlers synchronously
    template <typename E>
    void Publish(const E& event) {
        std::vector<std::function<void(const E&)>> handlersCopy;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            const uint32_t typeId = detail::GetEventTypeId<E>();
            auto it = m_Channels.find(typeId);
            if (it == m_Channels.end() || !it->second) {
                return;
            }
            auto* channel = static_cast<EventChannel<E>*>(it->second.get());
            channel->GetHandlers(handlersCopy);
        }

        for (auto& handler : handlersCopy) {
            handler(event);
        }
    }

    /// Enqueues event E for deferred dispatch during DispatchQueued()
    template <typename E>
    void Enqueue(const E& event) {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_EventQueue.push_back(std::make_unique<QueuedEventHolder<E>>(event));
    }

    /// Dispatches all queued events in FIFO order and empties the queue
    void DispatchQueued() {
        std::vector<std::unique_ptr<IQueuedEvent>> queueToDrain;
        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            queueToDrain.swap(m_EventQueue);
        }

        for (auto& q : queueToDrain) {
            if (q) {
                q->Dispatch(*this);
            }
        }
    }

    /// Clears all queued events without dispatching
    void ClearQueued() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_EventQueue.clear();
    }

    [[nodiscard]] size_t QueuedCount() const noexcept {
        std::lock_guard<std::mutex> lock(m_Mutex);
        return m_EventQueue.size();
    }

    template <typename E>
    [[nodiscard]] size_t SubscriberCount() const noexcept {
        std::lock_guard<std::mutex> lock(m_Mutex);
        const uint32_t typeId = detail::GetEventTypeId<E>();
        auto it = m_Channels.find(typeId);
        if (it == m_Channels.end() || !it->second) return 0u;
        return it->second->Count();
    }

    void Reset() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_EventQueue.clear();
        m_Channels.clear();
    }

private:
    struct detail {
        static inline uint32_t GetNextEventTypeId() noexcept {
            static std::atomic<uint32_t> s_Counter{0u};
            return s_Counter.fetch_add(1u, std::memory_order_relaxed);
        }

        template <typename E>
        static inline uint32_t GetEventTypeId() noexcept {
            static const uint32_t s_Id = GetNextEventTypeId();
            return s_Id;
        }
    };

    struct IEventChannel {
        virtual ~IEventChannel() = default;
        virtual bool Remove(SubscriptionId id) = 0;
        virtual size_t Count() const = 0;
    };

    template <typename E>
    class EventChannel final : public IEventChannel {
    public:
        struct Subscription {
            SubscriptionId id;
            std::function<void(const E&)> handler;
        };

        SubscriptionId Add(std::function<void(const E&)> handler) {
            SubscriptionId id = ++m_NextId;
            m_Subscriptions.push_back({id, std::move(handler)});
            return id;
        }

        bool Remove(SubscriptionId id) override {
            for (auto it = m_Subscriptions.begin(); it != m_Subscriptions.end(); ++it) {
                if (it->id == id) {
                    m_Subscriptions.erase(it);
                    return true;
                }
            }
            return false;
        }

        size_t Count() const override {
            return m_Subscriptions.size();
        }

        void GetHandlers(std::vector<std::function<void(const E&)>>& outHandlers) const {
            outHandlers.reserve(m_Subscriptions.size());
            for (const auto& sub : m_Subscriptions) {
                outHandlers.push_back(sub.handler);
            }
        }

    private:
        std::vector<Subscription> m_Subscriptions;
        SubscriptionId m_NextId = 0u;
    };

    template <typename E>
    EventChannel<E>& GetOrCreateChannel() {
        const uint32_t typeId = detail::GetEventTypeId<E>();
        auto it = m_Channels.find(typeId);
        if (it == m_Channels.end()) {
            auto channel = std::make_unique<EventChannel<E>>();
            auto* raw = channel.get();
            m_Channels[typeId] = std::move(channel);
            return *raw;
        }
        return *static_cast<EventChannel<E>*>(it->second.get());
    }

    struct IQueuedEvent {
        virtual ~IQueuedEvent() = default;
        virtual void Dispatch(EventBus& bus) = 0;
    };

    template <typename E>
    struct QueuedEventHolder final : public IQueuedEvent {
        explicit QueuedEventHolder(E event) : m_Event(std::move(event)) {}
        void Dispatch(EventBus& bus) override {
            bus.Publish(m_Event);
        }
        E m_Event;
    };

    mutable std::mutex m_Mutex;
    std::unordered_map<uint32_t, std::unique_ptr<IEventChannel>> m_Channels;
    std::vector<std::unique_ptr<IQueuedEvent>> m_EventQueue;
};

} // namespace tvk
