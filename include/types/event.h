#ifndef ELEGOO_EVENT_H
#define ELEGOO_EVENT_H
#include "../events/event_system.h"
#include "printer.h"
#include "cloud.h"

namespace elink
{
    /**
     * Printer connection status change event
     */
    class PrinterConnectionEvent : public BaseEvent
    {
    public:
        ConnectionStatusData connectionStatus; // Connection status data
    };

    /**
     * Printer status update event
     */
    class PrinterStatusEvent : public BaseEvent
    {
    public:
        PrinterStatusData status; // Printer status data
    };

    /**
     * Printer attributes update event
     */
    class PrinterAttributesEvent : public BaseEvent
    {
    public:
        PrinterAttributes attributes; // Printer attributes data
    };

    // class PrinterDiscoveredEvent : public BaseEvent
    // {
    // public:
    //     PrinterDiscoveredEvent(PrinterInfo printer)
    //         : printer(printer)
    //     {
    //     }
    //     PrinterInfo printer; // Discovered printer info
    // };

    // class PrinterDiscoveryCompletedEvent : public BaseEvent
    // {
    // public:
    //     PrinterDiscoveryCompletedEvent()
    //     {
    //     }
    //     std::vector<PrinterInfo> printers; // All discovered printers
    // };

    class RtmMessageEvent : public BaseEvent
    {
    public:
        RtmMessageData message; // Connection status data
    };

    /*
     * RTC Token Changed Event
     */
    class RtcTokenEvent : public BaseEvent
    {
    public:
        RtcTokenData token; // RTC Token data
    };

    /**
     * Logged in elsewhere event
     */
    class LoggedInElsewhereEvent : public BaseEvent
    {
    };

    class PrinterEventRawEvent : public BaseEvent
    {
    public:
        PrinterEventRawData rawData;
    };

    class PrinterListChangedEvent : public BaseEvent
    {
    };

    // Online status changed event
    class OnlineStatusChangedEvent : public BaseEvent
    {
    public:
        bool isOnline; // Online status
    };
}
#endif // ELEGOO_EVENT_H
