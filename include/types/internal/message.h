#pragma once
#include <string>
#include <map>
#include <vector>
#include <chrono>
#include <memory>
#include <functional>
#include "../base.h"
#include "../biz.h"
#include <nlohmann/json.hpp>
namespace elink
{

    /**
     * Command types
     */
    enum class MethodType
    {
        UNKNOWN = 0, // Unknown command

        /**
         * Basic settings-related methods
         */
        GET_PRINTER_ATTRIBUTES = 1010, // Get printer attributes asynchronously, will be reported via message event after retrieval
        GET_PRINTER_STATUS,            // Get printer status asynchronously, will be reported via message event after retrieval

        UPDATE_PRINTER_NAME, // Update printer name

        //  Print task control (1100-1199)
        START_PRINT = 1100, // Start printing
        PAUSE_PRINT,        // Pause printing
        RESUME_PRINT,       // Resume printing
        STOP_PRINT,         // Stop printing

        //  Hardware settings and control (1200-1299)
        HOME_AXES = 1200, // Home axes
        MOVE_AXES,        // Move axes
        SET_TEMPERATURE,  // Set temperature
        SET_PRINT_SPEED,  // Set print speed level
        SET_FAN_SPEED,    // Set fan speed

        // File management (1300-1399)
        SET_PRINTER_DOWNLOAD_FILE = 1300,    // Set printer to download file
        CANCEL_PRINTER_DOWNLOAD_FILE, // Cancel printer file download

        //  Task management (1400-1499)
        GET_PRINT_TASK_LIST = 1400, // Get print task list
        DELETE_PRINT_TASKS,         // Delete print task
        GET_FILE_LIST,
        GET_FILE_DETAIL,

        // Multi-color printing related (1500-1599)
        GET_CANVAS_STATUS = 1500, // Get canvas status
        SET_AUTO_REFILL = 1501,   // Set auto refill

        /** Below are message event-related methods, actively reported by SDK */
        ON_PRINTER_STATUS = 2000, // Print status update
        ON_PRINTER_ATTRIBUTES,
        ON_CONNECTION_STATUS,      // Connection status change
        ON_FILE_TRANSFER_PROGRESS, // File transfer progress
        ON_PRINTER_DISCOVERY,      // Printer discovery event·
        ON_RTM_MESSAGE,            // RTM message received
        ON_RTC_TOKEN_CHANGED,      // RTC token changed
        ON_PRINTER_EVENT_RAW,      // Raw printer event
        ON_LOGGED_IN_ELSEWHERE,    // Logged in elsewhere
        ON_PRINTER_LIST_CHANGED,   // Printer list changed
        ON_ONLINE_STATUS_CHANGED,  // Online status changed
    };

    struct BizRequest
    {
        MethodType method;
        nlohmann::json params;

        BizRequest(MethodType method = MethodType::UNKNOWN, const nlohmann::json &params = nlohmann::json{})
            : method(method), params(params) {}
    };

    struct BizEvent
    {
        MethodType method;
        nlohmann::json data;

        BizEvent(MethodType method = MethodType::UNKNOWN, const nlohmann::json &data = nlohmann::json{})
            : method(method), data(data) {}
    };

    using ResponseCallback = std::function<void(const BizResult<nlohmann::json> &)>;
    using EventCallback = std::function<int(const BizEvent &)>;
}
