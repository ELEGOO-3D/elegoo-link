#include "events/event_system.h"
#include "types/event.h"
#include "utils/logger.h"
#include "types/internal/internal.h"
#include "types/internal/json_serializer.h"

namespace elink
{

    void EventBus::publishFromEvent(const BizEvent &bizEvent)
    {
        std::shared_ptr<BaseEvent> event = nullptr;

        try
        {
            switch (bizEvent.method)
            {
            case MethodType::ON_CONNECTION_STATUS:
            {
                auto connectionEvent = std::make_shared<PrinterConnectionEvent>();
                connectionEvent->connectionStatus = bizEvent.data.get<ConnectionStatusData>();
                event = connectionEvent;
                break;
            }

            case MethodType::ON_PRINTER_STATUS:
            {
                auto statusEvent = std::make_shared<PrinterStatusEvent>();
                statusEvent->status = bizEvent.data;
                event = statusEvent;
                break;
            }

            case MethodType::ON_PRINTER_ATTRIBUTES:
            {
                auto attributesEvent = std::make_shared<PrinterAttributesEvent>();
                attributesEvent->attributes = bizEvent.data.get<PrinterAttributes>();
                event = attributesEvent;
                break;
            }

            case MethodType::ON_RTM_MESSAGE:
            {
                auto rtmEvent = std::make_shared<RtmMessageEvent>();
                rtmEvent->message = bizEvent.data.get<RtmMessageData>();
                event = rtmEvent;
                break;
            }

            case MethodType::ON_RTC_TOKEN_CHANGED:
            {
                auto rtcEvent = std::make_shared<RtcTokenEvent>();
                rtcEvent->token = bizEvent.data.get<RtcTokenData>();
                event = rtcEvent;
                break;
            }

            case MethodType::ON_LOGGED_IN_ELSEWHERE:
            {
                auto loggedInEvent = std::make_shared<LoggedInElsewhereEvent>();
                event = loggedInEvent;
                break;
            }

            case MethodType::ON_PRINTER_EVENT_RAW:
            {
                auto rawEvent = std::make_shared<PrinterEventRawEvent>();
                rawEvent->rawData = bizEvent.data.get<PrinterEventRawData>();
                event = rawEvent;
                break;
            }

            case MethodType::ON_PRINTER_LIST_CHANGED:
            {
                auto listChangedEvent = std::make_shared<PrinterListChangedEvent>();
                event = listChangedEvent;
                break;
            }
            case MethodType::ON_ONLINE_STATUS_CHANGED:
            {
                auto onlineStatusEvent = std::make_shared<OnlineStatusChangedEvent>();
                onlineStatusEvent->isOnline = bizEvent.data.get<OnlineStatusData>().isOnline;
                event = onlineStatusEvent;
                break;
            }

            default:
                ELEGOO_LOG_DEBUG("Unhandled event method type: {}", static_cast<int>(bizEvent.method));
                return;
            }

            if (event)
            {
                // Dispatch according to specific event type
                if (auto connectionEvent = std::dynamic_pointer_cast<PrinterConnectionEvent>(event))
                {
                    this->publish<PrinterConnectionEvent>(connectionEvent);
                }
                else if (auto statusEvent = std::dynamic_pointer_cast<PrinterStatusEvent>(event))
                {
                    this->publish<PrinterStatusEvent>(statusEvent);
                }
                else if (auto attributesEvent = std::dynamic_pointer_cast<PrinterAttributesEvent>(event))
                {
                    this->publish<PrinterAttributesEvent>(attributesEvent);
                }
                else if (auto rtmEvent = std::dynamic_pointer_cast<RtmMessageEvent>(event))
                {
                    this->publish<RtmMessageEvent>(rtmEvent);
                }
                else if (auto rtcEvent = std::dynamic_pointer_cast<RtcTokenEvent>(event))
                {
                    this->publish<RtcTokenEvent>(rtcEvent);
                }
                else if (auto loggedInEvent = std::dynamic_pointer_cast<LoggedInElsewhereEvent>(event))
                {
                    this->publish<LoggedInElsewhereEvent>(loggedInEvent);
                }
                else if (auto rawEvent = std::dynamic_pointer_cast<PrinterEventRawEvent>(event))
                {
                    this->publish<PrinterEventRawEvent>(rawEvent);
                }
                else if (auto listChangedEvent = std::dynamic_pointer_cast<PrinterListChangedEvent>(event))
                {
                    this->publish<PrinterListChangedEvent>(listChangedEvent);
                }
                else if (auto onlineStatusEvent = std::dynamic_pointer_cast<OnlineStatusChangedEvent>(event))
                {
                    this->publish<OnlineStatusChangedEvent>(onlineStatusEvent);
                }
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error converting event to typed event: {}", e.what());
        }
    }

} // namespace elink ::events
