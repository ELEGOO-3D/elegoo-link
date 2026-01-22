#include "adapters/generic_moonraker_adapters.h"
#include "utils/logger.h"
#include <algorithm>
#include <sstream>
#include <random>
#include <chrono>
#include <nlohmann/json.hpp>
#include "utils/utils.h"
#include "types/internal/internal.h"
#include "types/internal/json_serializer.h"
#include "utils/json_utils.h"
namespace elink
{

    GenericMoonrakerMessageAdapter::GenericMoonrakerMessageAdapter(const PrinterInfo &printerInfo)
        : BaseMessageAdapter(printerInfo)
    {
    }

    PrinterBizRequest<std::string> GenericMoonrakerMessageAdapter::convertRequest(MethodType method, const nlohmann::json &request, std::chrono::milliseconds timeout)
    {
        try
        {
            PrinterBizRequest<std::string> bizRequest;
            bizRequest.method = method;
            nlohmann::json printerMessage;
            bizRequest.requestId = generateMessageId();
            std::string standardMessageId = bizRequest.requestId;

            // Generate printer-side request ID
            std::string printerRequestId = generatePrinterRequestId();

            // V1 printer message format
            printerMessage["id"] = std::stoi(printerRequestId);
            printerMessage["jsonrpc"] = "2.0";
            int commandInt = static_cast<int>(method);
            MethodType command = static_cast<MethodType>(commandInt);

            std::string methodPrinter = mapCommandType(command);
            printerMessage["method"] = methodPrinter;
            if (methodPrinter.empty())
            {
                ELEGOO_LOG_ERROR("Unsupported command type: {}", static_cast<int>(command));
                bizRequest.code = ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED;
                bizRequest.message = "Command not implemented";
                return bizRequest;
            }

            // Record request mapping
            recordRequest(standardMessageId, printerRequestId, command, timeout);

            // Convert parameters based on command type

            switch (command)
            {
            case MethodType::START_PRINT:
            {
                auto startPrintData = request.get<StartPrintParams>();
                nlohmann::json param;
                std::string pathPrefix;
                param["filename"] = startPrintData.fileName;
                printerMessage["params"] = param;
                break;
            }
            case MethodType::GET_PRINTER_ATTRIBUTES:
            {
                nlohmann::json param;
                // param["namespace"] = "fluidd";
                // param["key"] = "uiSettings";
                // printerMessage["params"] = param;
                break;
            }
            case MethodType::GET_PRINTER_STATUS:
            {
                // https://moonraker.readthedocs.io/en/latest/external_api/printer/#query-printer-object-status
                nlohmann::json param;
                param["objects"] = {
                    {"gcode_move", nullptr},
                    {"toolhead", nullptr},
                    {"display_status", nullptr},
                    {"idle_timeout", nullptr},
                    {"print_stats", nullptr},
                    {"heater_bed", nullptr},
                    {"pause_resume", nullptr},
                    {"extruder", nullptr}};
                printerMessage["params"] = param;
                break;
            }
            case MethodType::UPDATE_PRINTER_NAME:
            {
                auto nameData = request.get<UpdatePrinterNameParams>();
                // Update local printer info
                printerInfo_.name = nameData.printerName;

                bizRequest.code = ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED;
                bizRequest.message = "Command not implemented";
                return bizRequest;
            }
            default:
                break;
            }

            auto body = printerMessage;
            bizRequest.data = body.dump();
            return bizRequest;
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error converting request for V1 printer: {}", e.what());
            PrinterBizRequest<std::string> bizRequest;
            bizRequest.code = ELINK_ERROR_CODE::INVALID_PARAMETER;
            bizRequest.message = e.what();
            bizRequest.data = "";
            return bizRequest; // Return empty request
        }
    }

    PrinterBizResponse<nlohmann::json> GenericMoonrakerMessageAdapter::convertToResponse(const std::string &printerResponse)
    {
        PrinterBizResponse<nlohmann::json> response;
        try
        {
            MethodType method = MethodType::UNKNOWN;
            auto printerJson = parseJson(printerResponse);
            if (printerJson.empty())
            {
                return PrinterBizResponse<nlohmann::json>::error(ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE, "Invalid printer response format");
            }

            // Try to extract ID from printer response and find corresponding standard request ID
            std::string printerResponseId;
            if (printerJson.contains("id"))
            {
                printerResponseId = std::to_string(JsonUtils::safeGetInt(printerJson, "id", -1));
            }

            // Find request record
            RequestRecord record = findRequestRecord(printerResponseId);
            if (!record.standardMessageId.empty())
            {
                response.requestId = record.standardMessageId;
                method = record.method;
                // Clean up completed request record
                removeRequestRecord(printerResponseId);
                ELEGOO_LOG_DEBUG("Found request mapping for printer response: {} -> {}",
                                 printerResponseId, record.standardMessageId);
            }
            else
            {
                ELEGOO_LOG_DEBUG("No request mapping found for printer response: {}, using fallback id: {}",
                                 printerResponseId, response.requestId);
                return PrinterBizResponse<nlohmann::json>::error(ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE, "No request mapping found for printer response");
            }
            if (printerJson.contains("error"))
            {
                auto error = printerJson["error"];
                if (error.contains("message"))
                {
                    response.message = error["message"].get<std::string>();
                }
                if (error.contains("code"))
                {
                    int errorCode = error["code"].get<int>();
                    if (errorCode == 400)
                    {
                        response.code = ELINK_ERROR_CODE::PRINTER_BUSY;
                    }
                    else
                    {
                        ELEGOO_LOG_ERROR("Printer error, code: {}, message: {}", errorCode, response.message);
                        response.code = ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR; // Default error
                        response.message = StringUtils::formatErrorMessage("Unknown error.", errorCode);
                    }
                }
            }
            else
            {
                if (printerJson.contains("result"))
                {
                    auto result = printerJson["result"];
                    if (result.is_string() && result == "ok")
                    {
                        response.message = "Success";
                        response.code = ELINK_ERROR_CODE::SUCCESS;
                    }
                    else if (result.is_object())
                    {
                        response.code = ELINK_ERROR_CODE::SUCCESS;
                        response.message = "Success";
                        switch (method)
                        {
                        case MethodType::GET_PRINTER_ATTRIBUTES:
                        {
                            auto attributes = handlePrinterAttributes(printerJson);
                            if (attributes.has_value())
                            {
                                response.data = attributes.value();
                            }
                            else
                            {
                                response.code = ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE; // Default error
                                response.message = "Failed to parse printer attributes";
                                ELEGOO_LOG_WARN("Failed to handle printer attributes for printer {}", StringUtils::maskString(printerInfo_.printerId));
                            }
                            break;
                        }
                        case MethodType::GET_PRINTER_STATUS:
                        {
                            auto status = handlePrinterStatus(method, printerJson);
                            if (status.has_value())
                            {
                                response.data = status.value();
                            }
                            else
                            {
                                response.code = ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE; // Default error
                                response.message = "Failed to parse printer status";
                                ELEGOO_LOG_WARN("Failed to handle printer status for printer {}", StringUtils::maskString(printerInfo_.printerId));
                            }
                            break;
                        }
                        default:

                            break;
                        }
                    }
                }
                else
                {
                    response.message = "No data in response";
                    response.code = ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE; // Default error
                }
            }

            return response;
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error converting V1 printer response: {}", e.what());
            response.code = ELINK_ERROR_CODE::UNKNOWN_ERROR; // Default error
            response.message = "Printer error";
            return response;
        }
    }

    PrinterBizEvent GenericMoonrakerMessageAdapter::convertToEvent(const std::string &printerMessage)
    {
        try
        {
            auto printerJson = parseJson(printerMessage);
            if (printerJson.empty())
            {
                return PrinterBizEvent();
            }

            PrinterBizEvent event;

            MethodType cmdFromResponse = MethodType::UNKNOWN;
            // V1 device response parsing
            if (printerJson.contains("method"))
            {
                cmdFromResponse = mapPrinterCommand(JsonUtils::safeGetString(printerJson, "method", ""));
            }

            std::string printerResponseId;
            if (printerJson.contains("id"))
            {
                printerResponseId = std::to_string(JsonUtils::safeGetInt(printerJson, "id", 0));
                // Find request record
                RequestRecord record = findRequestRecord(printerResponseId);
                if (!record.standardMessageId.empty())
                {
                    cmdFromResponse = record.method;
                }
            }

            switch (cmdFromResponse)
            {
            case MethodType::GET_PRINTER_STATUS:
            case MethodType::ON_PRINTER_STATUS:
            {
                auto status = handlePrinterStatus(cmdFromResponse, printerJson);
                if (status.has_value())
                {
                    event.method = MethodType::ON_PRINTER_STATUS;
                    event.data = status.value();
                }
                else
                {
                    ELEGOO_LOG_WARN("Failed to handle printer status for printer {}", StringUtils::maskString(printerInfo_.printerId));
                }
                break;
            }
            case MethodType::GET_PRINTER_ATTRIBUTES:
            {
                auto attributes = handlePrinterAttributes(printerJson);
                if (attributes.has_value())
                {
                    event.method = MethodType::ON_PRINTER_ATTRIBUTES;
                    event.data = attributes.value();
                }
                else
                {
                    ELEGOO_LOG_WARN("Failed to handle printer attributes for printer {}", StringUtils::maskString(printerInfo_.printerId));
                }
                break;
            }
            default:
                break;
            }
            return event;
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error converting V1 printer event: {}", e.what());
            return PrinterBizEvent();
        }
    }

    std::optional<PrinterAttributesData> GenericMoonrakerMessageAdapter::handlePrinterAttributes(const nlohmann::json &printerJson)
    {
        PrinterAttributesData printerAttributes(printerInfo_);
        if (printerJson.contains("result"))
        {
            auto result = printerJson["result"];
            if (result.is_object())
            {
                // if (result.contains("value") && result["value"].contains("general"))
                // {
                //     auto general = result["value"]["general"];
                //     printerAttributes.name = JsonUtils::safeGet(general, "instanceName", std::string());
                // }
                // else
                // {
                //     printerAttributes.name = "";
                // }

                if (result.contains("system_info") && result["system_info"].is_object())
                {
                    auto systemInfo = result["system_info"];
                    if (systemInfo.contains("network") && systemInfo["network"].is_object())
                    {
                        auto network = systemInfo["network"];
                        if (network.contains("wlan0") && network["wlan0"].is_object())
                        {
                            printerAttributes.mainboardId = JsonUtils::safeGet(network["wlan0"], "mac_address", std::string());
                        }
                    }
                }

                printerAttributes.capabilities.fanComponents = {
                    {"model", true, 0, 100, true},
                };
                printerAttributes.capabilities.temperatureComponents = {
                    {"extruder", true, 0, 300, true},
                    {"heatedBed", true, 0, 120, true},
                };

                printerAttributes.capabilities.lightComponents = {

                };
                printerAttributes.capabilities.storageComponents = {
                    {"local", false},
                    {
                        "udisk",
                        true,
                    },
                    {
                        "sdcard",
                        true,
                    }};
                return printerAttributes;
            }
        }
        return std::nullopt;
    }
    std::optional<PrinterStatusData> GenericMoonrakerMessageAdapter::handlePrinterStatus(MethodType method, const nlohmann::json &printerJson)
    {
        // Parse printer status from merged JSON
        PrinterStatusData finalStatus(printerInfo_.printerId);
        bool isFullStatusUpdate = false;

        // Determine if it is a full status update
        if (method == MethodType::GET_PRINTER_STATUS)
        {
            isFullStatusUpdate = true;
            ELEGOO_LOG_TRACE("Processing full printer status update");
        }
        else
        {
            isFullStatusUpdate = false;
            ELEGOO_LOG_TRACE("Processing delta printer status update");
        }

        nlohmann::json statusJson;
        if (method == MethodType::ON_PRINTER_STATUS)
        {
            statusJson = printerJson["params"];
            if (statusJson.is_array())
            {
                // get first element of array
                if (!statusJson.empty() && statusJson[0].is_object())
                {
                    statusJson = statusJson[0];
                }
                else
                {
                    ELEGOO_LOG_WARN("Received empty or invalid status array for printer {}", StringUtils::maskString(printerInfo_.printerId));
                    return std::optional<PrinterStatusData>();
                }
            }
        }
        else
        {
            if (printerJson.contains("result") && printerJson["result"].contains("status"))
            {
                statusJson = printerJson["result"]["status"];
            }
        }

        // Printer status update event
        if (statusJson.is_object())
        {
            // Handle status cache and delta update
            nlohmann::json finalResult;
            if (isFullStatusUpdate)
            {
                // Full status update, cache original JSON data
                cacheFullPrinterStatusJson(statusJson);
                finalResult = statusJson;
            }
            else
            {
                if (!hasFullStatusCache_)
                {
                    // If no cached full status, return empty BizEvent
                    ELEGOO_LOG_WARN("No cached full status available, cannot merge with delta update for printer {}", StringUtils::maskString(printerInfo_.printerId));
                    return std::optional<PrinterStatusData>();
                }
                finalResult = mergeStatusUpdateJson(statusJson);
                ELEGOO_LOG_TRACE("Merged delta status JSON with cached full status for printer {}", StringUtils::maskString(printerInfo_.printerId));
            }

            // Parse print status info
            if (finalResult.contains("print_stats") && finalResult["print_stats"].is_object())
            {
                auto printStatus = finalResult["print_stats"];
                finalStatus.printStatus.fileName = JsonUtils::safeGet(printStatus, "filename", std::string());
                // finalStatus.printStatus.totalTime = JsonUtils::safeGet(printStatus, "total_duration", 0);
                finalStatus.printStatus.currentTime = JsonUtils::safeGet(printStatus, "print_duration", 0);
                // finalStatus.printStatus.estimatedTime = finalStatus.printStatus.totalTime - finalStatus.printStatus.currentTime;
                if (finalStatus.printStatus.estimatedTime < 0)
                {
                    finalStatus.printStatus.estimatedTime = 0;
                }
                if (printStatus.contains("info") && printStatus["info"].is_object())
                {
                    auto info = printStatus["info"];
                    finalStatus.printStatus.totalLayer = JsonUtils::safeGetInt(info, "total_layer", 0);
                    finalStatus.printStatus.currentLayer = JsonUtils::safeGetInt(info, "current_layer", 0);
                }

                std::string state = JsonUtils::safeGet(printStatus, "state", std::string());
                if (state == "printing")
                {
                    finalStatus.printerStatus.state = PrinterState::PRINTING;
                    finalStatus.printerStatus.subState = PrinterSubState::P_PRINTING;
                }
                else if (state == "paused")
                {
                    finalStatus.printerStatus.state = PrinterState::PRINTING;
                    finalStatus.printerStatus.subState = PrinterSubState::P_PAUSED;
                }
                else if (state == "standby")
                {
                    finalStatus.printerStatus.state = PrinterState::IDLE;
                }
                else if (state == "complete")
                {
                    finalStatus.printerStatus.state = PrinterState::PRINTING;
                    finalStatus.printerStatus.subState = PrinterSubState::P_PRINTING_COMPLETED;
                }
                else if (state.empty())
                {
                    finalStatus.printerStatus.state = PrinterState::IDLE;
                    finalStatus.printerStatus.subState = PrinterSubState::NONE;
                }
                else
                {
                    finalStatus.printerStatus.state = PrinterState::UNKNOWN;
                }
            }
            // Parse machine status
            if (finalResult.contains("idle_timeout") && finalResult["idle_timeout"].is_object())
            {
                auto idle_timeout = finalResult["idle_timeout"];
                auto state = JsonUtils::safeGet(idle_timeout, "state", std::string());
                //  "Idle", "Printing", "Ready".
                if (state == "Idle")
                {
                    // finalStatus.printerStatus.state = PrinterState::IDLE;
                }
                else if (state == "Printing")
                {
                    // finalStatus.printerStatus.state = PrinterState::PRINTING;
                }
                else if (state == "Ready")
                {
                    // finalStatus.printerStatus.state = PrinterState::IDLE;
                }
                else
                {
                    // finalStatus.printerStatus.state = PrinterState::UNKNOWN;
                }

                finalStatus.printerStatus.exceptionCodes = {};
            }

            if (finalResult.contains("display_status") && finalResult["display_status"].is_object())
            {
                auto displayStatus = finalResult["display_status"];
                double progress = JsonUtils::safeGetDouble(displayStatus, "progress", 0.0);
                finalStatus.printerStatus.progress = (int)(progress * 100);
                finalStatus.printerStatus.supportProgress = true;
                finalStatus.printStatus.progress = finalStatus.printerStatus.progress;
                if (progress > 0.0)
                {
                    finalStatus.printStatus.totalTime = static_cast<int>(finalStatus.printStatus.currentTime / progress);
                }
                finalStatus.printStatus.estimatedTime = finalStatus.printStatus.totalTime - finalStatus.printStatus.currentTime;
                if (finalStatus.printStatus.estimatedTime < 0)
                {
                    finalStatus.printStatus.estimatedTime = 0;
                }
            }

            // if (finalResult.contains("pause_resume"))
            // {
            //     auto pauseResume = finalResult["pause_resume"];
            //     bool isPaused = JsonUtils::safeGet(pauseResume, "is_paused", false);
            //     if (finalStatus.printerStatus.state == PrinterState::PRINTING && isPaused)
            //     {
            //         finalStatus.printerStatus.subState = PrinterSubState::P_PAUSED;
            //     }
            // }

            if (finalResult.contains("toolhead") && finalResult["toolhead"].is_object())
            {
                auto toolhead = finalResult["toolhead"];
                if (toolhead.contains("position"))
                {
                    // position: [56.202, 193.257, -1.46, 1475.8027900000004]
                    auto pos = toolhead.value("position", std::vector<double>{});
                    if (pos.size() >= 4)
                    {
                        finalStatus.printAxesStatus.position = std::vector<double>(pos.begin(), pos.begin() + 4);
                    }
                    else
                    {
                        ELEGOO_LOG_WARN("Received invalid position data for printer {}, expected at least 4 values, got {}",
                                        printerInfo_.printerId, pos.size());
                    }
                }
            }

            if (finalResult.contains("extruder") && finalResult["extruder"].is_object())
            {
                auto extruder = finalResult["extruder"];
                TemperatureStatus extruderTemp;
                extruderTemp.current = JsonUtils::safeGet(extruder, "temperature", 0.0f);
                extruderTemp.target = JsonUtils::safeGet(extruder, "target", 0.0f);
                finalStatus.temperatureStatus["extruder"] = extruderTemp;
            }

            if (finalResult.contains("heater_bed") && finalResult["heater_bed"].is_object())
            {
                auto heaterBed = finalResult["heater_bed"];
                TemperatureStatus bedTemp;
                bedTemp.current = JsonUtils::safeGet(heaterBed, "temperature", 0.0f);
                bedTemp.target = JsonUtils::safeGet(heaterBed, "target", 0.0f);
                finalStatus.temperatureStatus["heatedBed"] = bedTemp;
            }
        }
        return finalStatus;
    }
    // sendMessageToPrinter method is inherited from base class, no need to implement again

    void GenericMoonrakerMessageAdapter::cacheFullPrinterStatusJson(const nlohmann::json &fullStatusResult)
    {
        std::lock_guard<std::mutex> lock(statusCacheMutex_);
        cachedFullStatusJson_ = fullStatusResult;
        hasFullStatusCache_ = true;
        ELEGOO_LOG_TRACE("Cached full printer status JSON for printer {}", StringUtils::maskString(printerInfo_.printerId));
    }

    nlohmann::json GenericMoonrakerMessageAdapter::mergeStatusUpdateJson(const nlohmann::json &deltaStatusResult)
    {
        std::lock_guard<std::mutex> lock(statusCacheMutex_);

        if (!hasFullStatusCache_)
        {
            ELEGOO_LOG_WARN("No cached full status JSON available for merge, returning delta status as-is");
            return deltaStatusResult;
        }

        // Create merged JSON, start from cached full status
        nlohmann::json mergedResult = cachedFullStatusJson_;

        // Recursively merge JSON objects
        std::function<void(nlohmann::json &, const nlohmann::json &)> mergeJsonRecursive =
            [&](nlohmann::json &target, const nlohmann::json &source)
        {
            for (auto &[key, value] : source.items())
            {
                if (value.is_object() && target.contains(key) && target[key].is_object())
                {
                    // Recursively merge objects
                    mergeJsonRecursive(target[key], value);
                }
                else
                {
                    // Directly overwrite basic types and arrays
                    target[key] = value;
                }
            }
        };

        // Perform merge
        mergeJsonRecursive(mergedResult, deltaStatusResult);

        // Update cache to merged status
        cachedFullStatusJson_ = mergedResult;
        return mergedResult;
    }

    void GenericMoonrakerMessageAdapter::clearStatusCache()
    {
        std::lock_guard<std::mutex> lock(statusCacheMutex_);
        hasFullStatusCache_ = false;
        cachedFullStatusJson_ = nlohmann::json::object();
        ELEGOO_LOG_DEBUG("Cleared status cache for printer {}", printerInfo_.printerId);
    }

    std::vector<std::string> GenericMoonrakerMessageAdapter::parseMessageType(const std::string &printerMessage)
    {
        std::vector<std::string> messageTypes;
        try
        {
            auto json = parseJson(printerMessage);
            if (json.contains("method"))
            {
                std::string method = JsonUtils::safeGetString(json, "method", "");
                // Contains response
                if (method.find("notify_") != std::string::npos)
                {
                    // return "response";
                    messageTypes.push_back("event");
                }
            }
            else
            {
                if (json.contains("id"))
                {
                    std::string id = std::to_string(JsonUtils::safeGetInt(json, "id", -1));
                    RequestRecord record = findRequestRecord(id);
                    if (record.method == MethodType::GET_PRINTER_ATTRIBUTES ||
                        record.method == MethodType::GET_PRINTER_STATUS)
                    {
                        messageTypes.push_back("event");
                    }
                    messageTypes.push_back("response");
                }
            }

            // return "event";
            if (messageTypes.empty())
            {
                messageTypes.push_back("event");
            }
        }
        catch (...)
        {
            // return "unknown";
            // messageTypes.push_back("unknown");
        }
        return messageTypes; // Return the first type or "unknown"
    }
    std::string GenericMoonrakerMessageAdapter::mapCommandType(MethodType command)
    {
        for (const auto &[methodType, commandCode] : COMMAND_MAPPING_TABLE)
        {
            if (methodType == command)
            {
                return commandCode;
            }
        }
        return ""; // Unknown command returns -1
    }

    MethodType GenericMoonrakerMessageAdapter::mapPrinterCommand(std::string printerCommand)
    {
        for (const auto &[methodType, commandCode] : COMMAND_MAPPING_TABLE)
        {
            if (commandCode == printerCommand)
            {
                return methodType;
            }
        }
        return MethodType::UNKNOWN;
    }

    // CCS printer command mapping table - static member to avoid repeated creation
    const std::vector<std::pair<MethodType, std::string>> GenericMoonrakerMessageAdapter::COMMAND_MAPPING_TABLE = {
        // {MethodType::GET_PRINTER_ATTRIBUTES, "server.database.get_item"}, // Version negotiation request
        {MethodType::GET_PRINTER_ATTRIBUTES, "machine.system_info"},    // Get printer attributes
        {MethodType::GET_PRINTER_STATUS, "printer.objects.subscribe"},  // Get status
        {MethodType::ON_PRINTER_STATUS, "notify_status_update"},        // Printer status
        {MethodType::START_PRINT, "printer.print.start"},               // Start print
        {MethodType::PAUSE_PRINT, "printer.print.pause"},               // Pause print
        {MethodType::RESUME_PRINT, "printer.print.resume"},             // Resume print
        {MethodType::STOP_PRINT, "printer.print.cancel"},               // Stop print
        {MethodType::UPDATE_PRINTER_NAME, "server.database.post_item"}, // Set machine name
    };
}
