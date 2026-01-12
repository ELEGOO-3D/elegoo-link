#pragma once

#include <string>
#include <vector>
#include <cstring>
#include "message.h"

namespace elink
{
    /**
     * Centralized method/event name definitions
     * Using dot.notation for consistency with JSON-RPC 2.0
     */
    namespace MethodNames
    {
        // ===== API Methods (client → server) =====

        // Initialization
        constexpr const char *GET_VERSION = "init.getVersion";

        // User Authentication
        constexpr const char *SET_HTTP_CREDENTIAL = "user.setCredential";
        constexpr const char *GET_HTTP_CREDENTIAL = "user.getCredential";
        constexpr const char *CLEAR_HTTP_CREDENTIAL = "user.clearCredential";
        constexpr const char *GET_USER_INFO = "user.getInfo";
        constexpr const char *REFRESH_HTTP_CREDENTIAL = "user.refreshCredential";
        constexpr const char *LOGOUT = "user.logout";

        // Printer Discovery
        constexpr const char *START_PRINTER_DISCOVERY = "printer.discovery.start";
        constexpr const char *STOP_PRINTER_DISCOVERY = "printer.discovery.stop";

        // Connection Management
        constexpr const char *CONNECT_PRINTER = "printer.connect";
        constexpr const char *DISCONNECT_PRINTER = "printer.disconnect";
        constexpr const char *GET_PRINTERS = "printer.getList";
        constexpr const char *SET_REGION = "printer.setRegion";

        // Printer Binding
        constexpr const char *BIND_PRINTER = "printer.bind";
        constexpr const char *UNBIND_PRINTER = "printer.unbind";
        constexpr const char *CANCEL_BIND_PRINTER = "printer.bind.cancel";

        // File Management
        constexpr const char *GET_FILE_LIST = "printer.file.getList";
        constexpr const char *GET_FILE_DETAIL = "printer.file.getDetail";
        constexpr const char *UPLOAD_FILE = "printer.file.upload";
        constexpr const char *CANCEL_FILE_UPLOAD = "printer.file.upload.cancel";

        // Print Task
        constexpr const char *GET_PRINT_TASK_LIST = "printer.task.getList";
        constexpr const char *START_PRINT = "printer.print.start";
        constexpr const char *PAUSE_PRINT = "printer.print.pause";
        constexpr const char *RESUME_PRINT = "printer.print.resume";
        constexpr const char *STOP_PRINT = "printer.print.stop";
        constexpr const char *DELETE_PRINT_TASKS = "printer.task.delete";

        // Status Query
        constexpr const char *GET_PRINTER_ATTRIBUTES = "printer.getAttributes";
        constexpr const char *GET_PRINTER_STATUS = "printer.getStatus";
        constexpr const char *GET_PRINTER_STATUS_RAW = "printer.getStatusRaw";
        constexpr const char *REFRESH_PRINTER_ATTRIBUTES = "printer.refreshAttributes";
        constexpr const char *REFRESH_PRINTER_STATUS = "printer.refreshStatus";
        constexpr const char *GET_CANVAS_STATUS = "printer.canvas.getStatus";

        // Printer Control
        constexpr const char *SET_AUTO_REFILL = "printer.setAutoRefill";
        constexpr const char *UPDATE_PRINTER_NAME = "printer.updateName";

        // RTC/RTM
        constexpr const char *GET_RTC_TOKEN = "rtc.getToken";
        constexpr const char *SEND_RTM_MESSAGE = "rtm.sendMessage";

        // ===== Events (server → client) =====
        // Printer Events
        constexpr const char *EVENT_PRINTER_CONNECTION = "event.printer.connection";
        constexpr const char *EVENT_PRINTER_STATUS = "event.printer.status";
        constexpr const char *EVENT_PRINTER_ATTRIBUTES = "event.printer.attributes";
        constexpr const char *EVENT_PRINTER_LIST_CHANGED = "event.printer.list.changed";
        constexpr const char *EVENT_PRINTER_RAW = "event.printer.raw";
        // User Events
        constexpr const char *EVENT_USER_LOGGED_ELSEWHERE = "event.user.logged.elsewhere";
        // Network Events
        constexpr const char *EVENT_USER_ONLINE_STATUS = "event.user.online.status";
        // Communication Events
        constexpr const char *EVENT_RTM_MESSAGE = "event.rtm.message";
        constexpr const char *EVENT_RTC_TOKEN_CHANGED = "event.rtc.token.changed";
        // File Events
        constexpr const char *EVENT_FILE_UPLOAD_PROGRESS = "event.file.upload.progress";

        // Event mapping table - bidirectional lookup
        struct EventMapping
        {
            MethodType methodType;
            const char* eventName;
        };

        inline const std::vector<EventMapping>& getEventMappings()
        {
            static const std::vector<EventMapping> mappings = {
                {MethodType::ON_PRINTER_STATUS, EVENT_PRINTER_STATUS},
                {MethodType::ON_PRINTER_ATTRIBUTES, EVENT_PRINTER_ATTRIBUTES},
                {MethodType::ON_CONNECTION_STATUS, EVENT_PRINTER_CONNECTION},
                {MethodType::ON_FILE_TRANSFER_PROGRESS, EVENT_FILE_UPLOAD_PROGRESS},
                {MethodType::ON_PRINTER_DISCOVERY, EVENT_PRINTER_LIST_CHANGED},
                {MethodType::ON_RTM_MESSAGE, EVENT_RTM_MESSAGE},
                {MethodType::ON_RTC_TOKEN_CHANGED, EVENT_RTC_TOKEN_CHANGED},
                {MethodType::ON_PRINTER_EVENT_RAW, EVENT_PRINTER_RAW},
                {MethodType::ON_LOGGED_IN_ELSEWHERE, EVENT_USER_LOGGED_ELSEWHERE},
                {MethodType::ON_PRINTER_LIST_CHANGED, EVENT_PRINTER_LIST_CHANGED},
                {MethodType::ON_ONLINE_STATUS_CHANGED, EVENT_USER_ONLINE_STATUS}
            };
            return mappings;
        }

        /**
         * Convert MethodType enum to event name string
         * @param type MethodType enum value (ON_XXX types)
         * @return Corresponding EVENT_ string constant, or nullptr if not an event type
         */
        inline const char* methodTypeToEventName(MethodType type)
        {
            for (const auto& mapping : getEventMappings())
            {
                if (mapping.methodType == type)
                    return mapping.eventName;
            }
            return nullptr;
        }

        /**
         * Convert event name string to MethodType enum
         * @param eventName Event name string (EVENT_ constants)
         * @return Corresponding MethodType enum value (ON_XXX), or MethodType::UNKNOWN if not found
         */
        inline MethodType eventNameToMethodType(const char* eventName)
        {
            if (!eventName) return MethodType::UNKNOWN;
            
            for (const auto& mapping : getEventMappings())
            {
                if (std::strcmp(mapping.eventName, eventName) == 0)
                    return mapping.methodType;
            }
            return MethodType::UNKNOWN;
        }
        
    } // namespace MethodNames

} // namespace elink
