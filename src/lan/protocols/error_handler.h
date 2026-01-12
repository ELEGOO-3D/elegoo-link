#pragma once

#include <string>
#include <chrono>
#include "type.h"
#include "protocol_interface.h"
namespace elink 
{

    /**
     * @brief Error Handling Utility Class
     *
     * Provides unified error code mapping and error message generation functionality
     */
    class ErrorHandler
    {
    public:
        /**
         * @brief Map WebSocket error code based on error message
         * @param errorMessage Error message
         * @return Corresponding response code
         */
        static ELINK_ERROR_CODE mapWebSocketErrorCode(const std::string &errorMessage);

        /**
         * @brief Map MQTT error code based on error message
         * @param errorMessage Error message
         * @return Corresponding response code
         */
        static ELINK_ERROR_CODE mapMqttReasonCode(const int &reasonCode);
        static ELINK_ERROR_CODE mapMqttReturnCode(const int &returnCode, const std::string &authMode="");

        /**
         * @brief Create connection failure result
         * @param errorCode Error code
         * @param title Error title
         * @param details Error details
         * @param startTime Start time
         * @return Connection result
         */
        static VoidResult createConnectionFailure(
            ELINK_ERROR_CODE errorCode,
            const std::string &title,
            const std::string &details,
            std::chrono::steady_clock::time_point startTime);

        /**
         * @brief Create connection timeout result
         * @param protocolName Protocol name
         * @param startTime Start time
         * @return Connection result
         */
        static VoidResult createTimeoutFailure(
            const std::string &protocolName,
            std::chrono::steady_clock::time_point startTime);
    };

} // namespace elink
