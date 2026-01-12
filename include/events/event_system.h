#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>
#include <typeindex>
#include <string>
#include <mutex>

namespace elink 
{

    // Forward declaration
    class IEventHandler;
    struct BizEvent;
    /**
     * Base Event Class
     * All strongly-typed events should inherit from this class
     */
    class BaseEvent
    {
    public:
        virtual ~BaseEvent() = default;
    };

    /**
     * Event Handler Interface
     */
    class IEventHandler
    {
    public:
        virtual ~IEventHandler() = default;
        virtual void handleEvent(const std::shared_ptr<BaseEvent> &event) = 0;
    };

    /**
     * Strongly-Typed Event Handler Template
     */
    template <typename EventType>
    class TypedEventHandler : public IEventHandler
    {
    public:
        using HandlerFunc = std::function<void(const std::shared_ptr<EventType> &)>;

        explicit TypedEventHandler(HandlerFunc handler) : handler_(handler) {}

        void handleEvent(const std::shared_ptr<BaseEvent> &event) override
        {
            auto typedEvent = std::dynamic_pointer_cast<EventType>(event);
            if (typedEvent && handler_)
            {
                handler_(typedEvent);
            }
        }

    private:
        HandlerFunc handler_;
    };

    /**
     * Event Bus
     * Responsible for event dispatching and subscription management
     */
    class EventBus
    {
    public:
        using EventId = size_t;

        /**
         * Subscribe to a specific type of event
         */
        template <typename EventType>
        EventId subscribe(std::function<void(const std::shared_ptr<EventType> &)> handler)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            EventId id = nextId_++;

            auto typedHandler = std::make_shared<TypedEventHandler<EventType>>(handler);
            handlers_[std::type_index(typeid(EventType))].emplace_back(id, typedHandler);

            return id;
        }

        /**
         * Unsubscribe
         */
        template <typename EventType>
        bool unsubscribe(EventId id)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto &eventHandlers = handlers_[std::type_index(typeid(EventType))];

            auto it = std::find_if(eventHandlers.begin(), eventHandlers.end(),
                                   [id](const auto &pair)
                                   { return pair.first == id; });

            if (it != eventHandlers.end())
            {
                eventHandlers.erase(it);
                return true;
            }
            return false;
        }

        /**
         * Publish an event
         */
        template <typename EventType>
        void publish(std::shared_ptr<EventType> event)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto typeIndex = std::type_index(typeid(EventType));

            if (handlers_.find(typeIndex) != handlers_.end())
            {
                for (const auto &[id, handler] : handlers_[typeIndex])
                {
                    if (handler)
                    {
                        handler->handleEvent(event);
                    }
                }
            }
        }

        /**
         * Convert from old BizEvent and publish an event
         */
        void publishFromEvent(const BizEvent &event);

        /**
         * Clear all subscriptions
         */
        void clear()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            handlers_.clear();
        }

    private:
        std::mutex mutex_;
        EventId nextId_ = 1;
        std::unordered_map<std::type_index, std::vector<std::pair<EventId, std::shared_ptr<IEventHandler>>>> handlers_;
    };

} // namespace elink ::events
