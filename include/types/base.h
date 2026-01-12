#pragma once
namespace elink
{
    /**
     * Response status code enumeration - Simplified custom error code system
     */
    enum class ELINK_ERROR_CODE
    {
        // Success status
        SUCCESS = 0, // Operation successful

        // General errors (1-99)
        UNKNOWN_ERROR = 1,             // Unknown error
        NOT_INITIALIZED = 2,           // Not initialized
        INVALID_PARAMETER = 3,         // Invalid parameter (value error, type error, etc.)
        OPERATION_TIMEOUT = 4,         // Operation timeout (including connection, file transfer, etc.)
        OPERATION_CANCELLED = 5,       // Operation canceled
        OPERATION_IN_PROGRESS = 6,     // Operation in progress
        OPERATION_NOT_IMPLEMENTED = 7, // Operation not implemented
        NETWORK_ERROR = 8,             // Network error
        INSUFFICIENT_MEMORY = 9,       // Insufficient memory
        NOT_CONNECTED_TO_SUBSERVICE = 10,

        // Authentication-related errors (200-299)
        INVALID_USERNAME_OR_PASSWORD = 201, // Username or password invalid
        INVALID_TOKEN = 202,                // Token invalid
        INVALID_ACCESS_CODE = 203,          // Access code invalid
        INVALID_PIN_CODE = 204,             // PIN code invalid

        // File transfer-related errors (300-399)
        FILE_TRANSFER_FAILED = 300, // File transfer failed (including upload, download)
        FILE_NOT_FOUND = 301,       // File not found
        FILE_ALREADY_EXISTS = 302,  // File already exists
        FILE_ACCESS_DENIED = 303,   // File access denied

        // 1000-1999 Printer business errors
        PRINTER_NOT_FOUND = 1000,                 // Printer not found
        PRINTER_CONNECTION_ERROR = 1001,          // Printer connection error
        PRINTER_CONNECTION_LIMIT_EXCEEDED = 1002, // Printer connection limit exceeded
        PRINTER_ALREADY_CONNECTED = 1003,         // Printer already connected or connecting
        PRINTER_BUSY = 1004,                      // Printer busy
        PRINTER_COMMAND_FAILED = 1005,            // Printer command execution failed
        PRINTER_UNKNOWN_ERROR = 1006,            // Printer internal error
        PRINTER_INVALID_PARAMETER = 1007,         // send data format error
        PRINTER_INVALID_RESPONSE = 1008,          // Printer response invalid data
        PRINTER_ACCESS_DENIED = 1009,             // Printer access denied
        PRINTER_MISSING_BED_LEVELING_DATA = 1010, // Printer missing bed leveling data
        PRINTER_PRINT_FILE_NOT_FOUND = 1011,       // Printer print file not found
        PRINTER_OFFLINE = 1012,                    // Printer offline
        PRINTER_FILAMENT_RUNOUT = 1013,           // Printer filament runout

        // 2000-2999 Server business errors
        SERVER_UNKNOWN_ERROR = 2000,     // Server error
        SERVER_INVALID_RESPONSE = 2001,  // Server response invalid data
        SERVER_TOO_MANY_REQUESTS = 2002, // Too many requests
        SERVER_RTM_NOT_CONNECTED = 2049, // RTM client not connected or not logged in

        SERVER_UNAUTHORIZED = 2050, // Unauthorized access
        SERVER_FORBIDDEN = 2051,    // Forbidden access

    };

    
    struct BaseParams
    {
    };

    struct BaseEventData
    {
    };

    /**
     * Base class for response parameters
     */
    struct BaseResult
    {
    };
} // namespace elink
