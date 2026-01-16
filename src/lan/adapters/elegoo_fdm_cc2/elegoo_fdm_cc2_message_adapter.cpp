#include "adapters/elegoo_cc2_adapters.h"
#include "utils/logger.h"
#include <algorithm>
#include <sstream>
#include <nlohmann/json.hpp>
#include "utils/utils.h"
#include "types/internal/internal.h"
#include "utils/json_utils.h"
#include "types/internal/json_serializer.h"
namespace elink
{
    // ========== ElegooFdmCC2MessageAdapter Implementation ==========

    ElegooFdmCC2MessageAdapter::ElegooFdmCC2MessageAdapter(const PrinterInfo &printerInfo)
        : BaseMessageAdapter(printerInfo)
    {
    }

    PrinterBizRequest<std::string> ElegooFdmCC2MessageAdapter::convertRequest(MethodType method, const nlohmann::json &request, std::chrono::milliseconds timeout)
    {
        try
        {
            PrinterBizRequest<std::string> bizRequest;
            bizRequest.method = method;
            nlohmann::json printerMessage;

            // Get standard request ID
            std::string standardMessageId = generateMessageId();
            bizRequest.requestId = standardMessageId;

            // Generate printer-side request ID
            std::string printerRequestId = generatePrinterRequestId();

            // V1 printer message format, convert to integer
            printerMessage["id"] = std::stoi(printerRequestId);
            MethodType command = method;
            int printerCommand = mapCommandType(command);

            if (printerCommand == -1)
            {
                bizRequest.code = ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED;
                bizRequest.message = "Command not implemented";
                return bizRequest;
            }

            printerMessage["method"] = printerCommand;

            // Record request mapping
            recordRequest(standardMessageId, printerRequestId, command, timeout);

            // Convert parameters according to command type
            {
                switch (command)
                {
                case MethodType::GET_PRINTER_ATTRIBUTES:
                case MethodType::GET_PRINTER_STATUS:
                    printerMessage["params"] = nlohmann::json::object();
                    break;
                case MethodType::START_PRINT:
                {
                    auto startPrintData = request.get<StartPrintParams>();
                    nlohmann::json param;
                    if (startPrintData.storageLocation == "local")
                    {
                        param["storage_media"] = "local";
                    }
                    else if (startPrintData.storageLocation == "udisk")
                    {
                        param["storage_media"] = "u-disk";
                    }
                    else if (startPrintData.storageLocation == "sdcard")
                    {
                        param["storage_media"] = "sd-card";
                    }
                    else
                    {
                        param["storage_media"] = "local";
                    }

                    param["filename"] = startPrintData.fileName;
                    param["config"]["delay_video"] = startPrintData.enableTimeLapse;
                    param["config"]["printer_check"] = startPrintData.autoBedLeveling;
                    param["config"]["print_layout"] = startPrintData.heatedBedType == 0 ? "A" : "B";
                    param["config"]["bedlevel_force"] = startPrintData.bedLevelForce;
                    auto slotMapJson = nlohmann::json::array();
                    for (size_t i = 0; i < startPrintData.slotMap.size(); i++)
                    {
                        auto item = startPrintData.slotMap[i];
                        nlohmann::json itemJson;
                        itemJson["t"] = item.t;
                        itemJson["canvas_id"] = item.canvasId;
                        itemJson["tray_id"] = item.trayId;
                        slotMapJson.push_back(itemJson);
                    }
                    param["config"]["slot_map"] = slotMapJson;

                    printerMessage["params"] = param;
                    break;
                }
                case MethodType::HOME_AXES:
                {
                    auto moveData = request.get<MoveAxisParams>();
                    nlohmann::json param;
                    // Convert to lowercase
                    std::transform(moveData.axes.begin(), moveData.axes.end(), moveData.axes.begin(), ::tolower);
                    param["homed_axes"] = moveData.axes;
                    printerMessage["params"] = param;
                    break;
                }
                case MethodType::MOVE_AXES:
                {
                    auto moveData = request.get<MoveAxisParams>();
                    nlohmann::json param;
                    // Convert to lowercase
                    std::transform(moveData.axes.begin(), moveData.axes.end(), moveData.axes.begin(), ::tolower);
                    param["axes"] = moveData.axes;

                    param["distance"] = moveData.distance;
                    printerMessage["params"] = param;
                    break;
                }
                case MethodType::SET_TEMPERATURE:
                {
                    auto tempData = request.get<SetTemperatureParams>();
                    nlohmann::json param;
                    if (tempData.temperatures.find("heatedBed") != tempData.temperatures.end())
                    {
                        param["heater_bed"] = tempData.temperatures["heatedBed"];
                    }
                    if (tempData.temperatures.find("extruder") != tempData.temperatures.end())
                    {
                        param["extruder"] = tempData.temperatures["extruder"];
                    }
                    printerMessage["params"] = param;
                    break;
                }
                case MethodType::SET_FAN_SPEED:
                {
                    auto fanData = request.get<SetFanSpeedParams>();
                    nlohmann::json param;
                    for (const auto &fan : fanData.fans)
                    {
                        // Printer-side fan names may differ, need mapping
                        if (fan.first == "model")
                        {
                            param["fan"] = fan.second;
                        }
                        else if (fan.first == "chamber")
                        {
                            param["box_fan"] = fan.second;
                        }
                        else if (fan.first == "aux")
                        {
                            param["aux_fan"] = fan.second;
                        }
                        else
                        {
                            // Other fan types, not handled for now
                            ELEGOO_LOG_WARN("Unknown fan type: {}", fan.first);
                        }
                    }

                    printerMessage["params"] = param;
                    break;
                }
                case MethodType::SET_PRINT_SPEED:
                {
                    // Speed mode not implemented yet
                    auto speedData = request.get<SetPrintSpeedParams>();
                    nlohmann::json param;
                    param["mode"] = speedData.speedMode;
                    printerMessage["params"] = param;

                    break;
                }
                case MethodType::SET_AUTO_REFILL:
                {
                    auto autoRefillData = request.get<SetAutoRefillParams>();
                    nlohmann::json param;
                    param["auto_refill"] = autoRefillData.enable;
                    printerMessage["params"] = param;
                    break;
                }
                case MethodType::SET_PRINTER_DOWNLOAD_FILE:
                {
                    auto downloadData = request.get<SetPrinterDownloadFileParams>();
                    nlohmann::json param;
                    param["filename"] = downloadData.fileName;
                    param["url"] = downloadData.fileUrl;
                    param["md5"] = downloadData.md5;
                    param["taskID"] = downloadData.taskId;
                    printerMessage["params"] = param;
                    break;
                }
                case MethodType::CANCEL_PRINTER_DOWNLOAD_FILE:
                {
                    auto cancelData = request.get<SetPrinterDownloadFileParams>();
                    nlohmann::json param;
                    param["taskID"] = cancelData.taskId;
                    printerMessage["params"] = param;
                    break;
                }
                case MethodType::UPDATE_PRINTER_NAME:
                {
                    // Set machine name request
                    auto setNameData = request.get<UpdatePrinterNameParams>();
                    nlohmann::json param;
                    param["hostname"] = setNameData.printerName;
                    printerMessage["params"] = param;
                    break;
                }
                default:
                    printerMessage["params"] = nlohmann::json::object();
                    break;
                }
            }

            auto body = createStandardBody();
            body = printerMessage;
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
            return bizRequest;
        }
    }

    PrinterBizResponse<nlohmann::json> ElegooFdmCC2MessageAdapter::convertToResponse(const std::string &printerResponse)
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
            // Try to extract ID from printer response and look up corresponding standard request ID
            std::string printerResponseId;
            if (printerJson.contains("id"))
            {
                printerResponseId = std::to_string(JsonUtils::safeGetInt(printerJson, "id", 0));
            }

            // V1 printer response parsing
            if (printerJson.contains("method"))
            {
                MethodType cmdFromResponse = mapPrinterCommand(JsonUtils::safeGetInt(printerJson, "method", 0));
                method = cmdFromResponse;
            }
            // Look up request record
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

            if (printerJson.contains("result") && printerJson["result"].is_object())
            {
                auto result = printerJson["result"];
                if (result.contains("error_code"))
                {
                    int error_code = JsonUtils::safeGetInt(result, "error_code", -1);
                    if (error_code == 0)
                    {
                        switch (method)
                        {
                        case MethodType::GET_PRINTER_ATTRIBUTES:
                        {
                            response.data = handlePrinterAttributes(printerJson);
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
                        case MethodType::GET_CANVAS_STATUS:
                        {
                            if (result.contains("canvas_info") && result["canvas_info"].is_object())
                            {
                                auto canvasInfo = result["canvas_info"];
                                response.data = handleCanvasStatus(canvasInfo);
                            }
                            else
                            {
                                response.code = ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE; // Default error
                                response.message = "No canvas_info in response";
                                ELEGOO_LOG_WARN("No canvas_info in response for printer {}", StringUtils::maskString(printerInfo_.printerId));
                            }
                            break;
                        }
                        case MethodType::MOVE_AXES:
                        case MethodType::HOME_AXES:
                        case MethodType::SET_FAN_SPEED:
                        case MethodType::SET_TEMPERATURE:
                        case MethodType::SET_PRINT_SPEED:
                        default:
                            break;
                        }
                    }
                    else
                    {
                        response.message = StringUtils::formatErrorMessage("Unknown error.", error_code);
                        response.code = convertRequestErrorToElegooError(error_code);
                        ELEGOO_LOG_ERROR("Printer response error: {}", response.message);
                    }
                }
                else
                {
                    response.message = "No error code in response";
                    response.code = ELINK_ERROR_CODE::UNKNOWN_ERROR; // Default error
                }
            }
            else
            {
                response.message = "No data in response";
                response.code = ELINK_ERROR_CODE::UNKNOWN_ERROR; // Default error
            }

            return response;
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error converting V1 printer response: {}", e.what());
            response.message = "Error converting V1 printer response";
            response.code = ELINK_ERROR_CODE::UNKNOWN_ERROR; // Default error
            return response;
        }
    }

    PrinterBizEvent ElegooFdmCC2MessageAdapter::convertToEvent(const std::string &printerMessage)
    {
        try
        {
            auto printerJson = parseJson(printerMessage);
            if (printerJson.empty())
            {
                return PrinterBizEvent();
            }

            PrinterBizEvent event;

            std::string printerResponseId = "";
            printerResponseId = std::to_string(JsonUtils::safeGetInt(printerJson, "id", 0));

            MethodType cmdFromResponse = MethodType::UNKNOWN;
            // V1 device response parsing
            cmdFromResponse = mapPrinterCommand(JsonUtils::safeGetInt(printerJson, "method", 0));

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
                event.method = MethodType::ON_PRINTER_ATTRIBUTES;
                event.data = handlePrinterAttributes(printerJson);
                break;
            }
            case MethodType::ON_PRINTER_ATTRIBUTES:
            {
                event.method = MethodType::ON_PRINTER_ATTRIBUTES;
                event.data = handlePrinterAttributes(printerJson);
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

    std::vector<std::string> ElegooFdmCC2MessageAdapter::parseMessageType(const std::string &printerMessage)
    {
        std::vector<std::string> messageTypes;
        try
        {
            auto json = parseJson(printerMessage);
            if (json.contains("method"))
            {
                int method = json.value("method", -1);
                if (method == 6000 || method == 6008)
                {
                    messageTypes.push_back("event");
                }
                else
                {
                    if (method == 1002 || method == 1001)
                    {
                        messageTypes.push_back("event");
                    }
                    // return "response";
                    messageTypes.push_back("response");
                }
            }
            else
            {
            }
        }
        catch (...)
        {
            // return "unknown";
        }
        return messageTypes;
    }
    std::optional<PrinterAttributesData> ElegooFdmCC2MessageAdapter::handlePrinterAttributes(const nlohmann::json &printerJson)
    {
        PrinterAttributesData printerAttributes(printerInfo_);
        // Printer status update event
        if (printerJson.contains("result"))
        {
            nlohmann::json result = printerJson["result"];
            if (result.contains("machine_model"))
            {
                printerAttributes.model = JsonUtils::safeGet(result, "machine_model", printerInfo_.model);
            }
            
            if (result.contains("software_version"))
            {
                auto software_version = result["software_version"];
                if (software_version.is_object())
                {
                    if (software_version.contains("ota_version"))
                    {
                        printerAttributes.firmwareVersion = JsonUtils::safeGet(software_version, "ota_version", std::string());
                    }
                }
            }

            if (result.contains("sn"))
            {
                printerAttributes.serialNumber = JsonUtils::safeGet(result, "sn", std::string());
                printerAttributes.mainboardId = printerAttributes.serialNumber;
            }

            if (result.contains("hostname"))
            {
                printerAttributes.name = JsonUtils::safeGet(result, "hostname", std::string());
            }

            printerInfo_.name = printerAttributes.name;
            printerInfo_.serialNumber = printerAttributes.serialNumber;
            printerInfo_.firmwareVersion = printerAttributes.firmwareVersion;
            printerInfo_.mainboardId = printerAttributes.mainboardId;
            printerInfo_.model = printerAttributes.model;
        }
        printerAttributes.capabilities.cameraCapabilities.supportsCamera = true;    // Assume printer supports camera
        printerAttributes.capabilities.cameraCapabilities.supportsTimeLapse = true; // Assume printer
        printerAttributes.capabilities.fanComponents = {
            {"model", true, 0, 100, true},
            // {"heatsink", true, 0, 100, true},
            // {"controller", true, 0, 100, true},
            {"chamber", true, 0, 100, true},
            {"aux", true, 0, 100, true}};
        printerAttributes.capabilities.temperatureComponents = {
            {"extruder", true, 0, 300, true},
            {"heatedBed", true, 0, 120, true},
            {"chamber", true, 0, 100, true}};

        printerAttributes.capabilities.lightComponents = {
            {"main", "singleColor", 0, 1},
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

        printerAttributes.capabilities.systemCapabilities.canGetDiskInfo = true;
        printerAttributes.capabilities.systemCapabilities.canSetPrinterName = true;
        printerAttributes.capabilities.systemCapabilities.supportsMultiFilament = true;
        printerAttributes.capabilities.printCapabilities.supportsAutoBedLeveling = true;
        printerAttributes.capabilities.printCapabilities.supportsTimeLapse = true;
        printerAttributes.capabilities.printCapabilities.supportsHeatedBedSwitching = true;
        printerAttributes.capabilities.printCapabilities.supportsFilamentMapping = true;
        printerAttributes.capabilities.printCapabilities.supportsAutoRefill = true;
        return printerAttributes;
    }

    std::optional<PrinterStatusData> ElegooFdmCC2MessageAdapter::handlePrinterStatus(MethodType method, const nlohmann::json &printerJson)
    {
        // Check status event continuity (only for method == 6000 event)
        if (printerJson.contains("method") && printerJson["method"].is_number_integer() && printerJson["method"] == 6000)
        {
            int currentStatusId = JsonUtils::safeGetInt(printerJson, "id", -1);
            if (currentStatusId != -1)
            {
                bool isContinuous = checkStatusEventContinuity(currentStatusId);
                if (!isContinuous)
                {
                    nonContinuousCount_++;
                    ELEGOO_LOG_WARN("Non-continuous status event detected, count: {}", nonContinuousCount_);

                    // If 5 consecutive non-continuous status events are received, request full status refresh
                    if (nonContinuousCount_ >= 5)
                    {
                        ELEGOO_LOG_WARN("Received 5 non-continuous status events, requesting full status refresh");
                        nonContinuousCount_ = 0; // Reset counter
                        // ID is not continuous, initiate full status refresh request
                        sendMessageToPrinter(MethodType::GET_PRINTER_STATUS);
                        // Return empty JSON to indicate not processing current event, as data is incomplete and may mislead
                        return std::optional<PrinterStatusData>();
                    }
                }
                else
                {
                    nonContinuousCount_ = 0; // Reset counter
                }
            }
        }

        // Parse printer status from merged JSON
        PrinterStatusData finalStatus(printerInfo_.printerId);
        bool isFullStatusUpdate = false;

        // Determine if it is a full status update
        if (method == MethodType::GET_PRINTER_STATUS)
        {
            isFullStatusUpdate = true;
            ELEGOO_LOG_TRACE("Processing full printer status update");
        }
        else if (printerJson.contains("method") && printerJson["method"] == 6000)
        {
            isFullStatusUpdate = false;
            ELEGOO_LOG_TRACE("Processing delta printer status update");
        }

        // Printer status update event
        if (printerJson.contains("result") && printerJson["result"].is_object())
        {
            auto result = printerJson["result"];

            if (result.contains("error_code") && result["error_code"].is_number_integer())
            {
                int errorCode = result["error_code"];
                if (isFullStatusUpdate)
                {
                    if (errorCode == 0)
                    {
                        // When receiving cmdFromResponse==MethodType::GET_PRINTER_STATUS, treat as full status event, reset status counter
                        printerStatusSequenceState_ = 1; // Full status data acquired
                    }
                    else
                    {

                        // Handle error status
                        ELEGOO_LOG_ERROR("Printer status update error: {}", result.value("message", "Unknown error"));
                        return std::optional<PrinterStatusData>();
                    }
                }
            }

            // Handle status cache and delta update
            nlohmann::json finalResult;
            if (isFullStatusUpdate)
            {
                // Full status update, cache original JSON data
                cacheFullPrinterStatusJson(result);
                finalResult = result;
                ELEGOO_LOG_DEBUG("Cached full printer status JSON for printer {}", StringUtils::maskString(printerInfo_.printerId));
            }
            else
            {
                if (!hasFullStatusCache_)
                {
                    // If no cached full status, return empty BizEvent
                    ELEGOO_LOG_WARN("No cached full status available, cannot merge with delta update for printer {}", StringUtils::maskString(printerInfo_.printerId));
                    return std::optional<PrinterStatusData>();
                }
                finalResult = mergeStatusUpdateJson(result);
                ELEGOO_LOG_TRACE("Merged delta status JSON with cached full status for printer {}", StringUtils::maskString(printerInfo_.printerId));
            }
            // Parse machine status
            if (finalResult.contains("machine_status") && finalResult["machine_status"].is_object())
            {
                auto printerStatus = finalResult["machine_status"];
                auto status = JsonUtils::safeGet(printerStatus, "status", -1);
                auto subStatus = JsonUtils::safeGet(printerStatus, "sub_status", -1);
                switch (status)
                {
                case 0:
                    finalStatus.printerStatus.state = PrinterState::INITIALIZING;
                    break;
                case 1:
                    finalStatus.printerStatus.state = PrinterState::IDLE;
                    /* code */
                    break;
                case 2:
                    finalStatus.printerStatus.state = PrinterState::PRINTING;
                    switch (subStatus)
                    {
                    case 0:
                        finalStatus.printerStatus.subState = PrinterSubState::NONE;
                        break;
                    case 1045:
                        finalStatus.printerStatus.subState = PrinterSubState::P_EXTRUDER_PREHEATING;
                        break;
                    case 1096:
                        finalStatus.printerStatus.subState = PrinterSubState::P_EXTRUDER_PREHEATING;
                        break;
                    case 1405:
                        finalStatus.printerStatus.subState = PrinterSubState::P_HEATED_BED_PREHEATING;
                        break;
                    case 1906:
                        finalStatus.printerStatus.subState = PrinterSubState::P_HEATED_BED_PREHEATING;
                        break;
                    case 1041:
                        finalStatus.printerStatus.subState = PrinterSubState::NONE;
                        break;

                    // Homing
                    case 2801:
                        finalStatus.printerStatus.subState = PrinterSubState::P_HOMING;
                        break;
                    case 2802:
                        finalStatus.printerStatus.subState = PrinterSubState::P_HOMING;
                        break;

                    // Auto leveling
                    case 2901:
                        finalStatus.printerStatus.subState = PrinterSubState::P_AUTO_LEVELING;
                        break;
                    case 2902:
                        finalStatus.printerStatus.subState = PrinterSubState::P_AUTO_LEVELING;
                        break;

                    case 2501:
                        finalStatus.printerStatus.subState = PrinterSubState::P_PAUSING;
                        break;
                    case 2505:
                    case 2502:
                        finalStatus.printerStatus.subState = PrinterSubState::P_PAUSED;
                        break;
                    case 2401:
                        finalStatus.printerStatus.subState = PrinterSubState::P_RESUMING;
                        break;
                    case 2402:
                        finalStatus.printerStatus.subState = PrinterSubState::P_RESUMING_COMPLETED;
                        break;
                    case 2075:
                        finalStatus.printerStatus.subState = PrinterSubState::P_PRINTING;
                        break;
                    case 2077:
                        finalStatus.printerStatus.subState = PrinterSubState::P_PRINTING_COMPLETED;
                        break;
                    case 2503:
                        finalStatus.printerStatus.subState = PrinterSubState::P_STOPPING;
                        break;
                    case 2504:
                        finalStatus.printerStatus.subState = PrinterSubState::P_STOPPED;
                        break;
                    default:
                        finalStatus.printerStatus.subState = PrinterSubState::UNKNOWN;
                    }
                    break;
                case 3:
                case 4:
                    finalStatus.printerStatus.state = PrinterState::FILAMENT_OPERATING;
                    switch (subStatus)
                    {
                    case 0:
                        finalStatus.printerStatus.subState = PrinterSubState::NONE;
                        break;
                    case 1133:
                        finalStatus.printerStatus.subState = PrinterSubState::FO_FILAMENT_LOADING;
                        break;
                    case 1134:
                        finalStatus.printerStatus.subState = PrinterSubState::FO_FILAMENT_LOADING;
                        break;
                    case 1135:
                        finalStatus.printerStatus.subState = PrinterSubState::FO_FILAMENT_LOADING;
                        break;
                    case 1136:
                        finalStatus.printerStatus.subState = PrinterSubState::FO_FILAMENT_LOADING_COMPLETED;
                        break;
                    case 1143:
                        finalStatus.printerStatus.subState = PrinterSubState::NONE;
                        break;
                    case 1144:
                        finalStatus.printerStatus.subState = PrinterSubState::FO_FILAMENT_UNLOADING;
                        break;
                    case 1145:
                        finalStatus.printerStatus.subState = PrinterSubState::FO_FILAMENT_UNLOADING_COMPLETED;
                        break;
                    default:
                        finalStatus.printerStatus.subState = PrinterSubState::UNKNOWN;
                    }
                    break;
                case 5:
                    finalStatus.printerStatus.state = PrinterState::AUTO_LEVELING;
                    switch (subStatus)
                    {
                    case 0:
                        finalStatus.printerStatus.subState = PrinterSubState::NONE;
                        break;
                    case 2901:
                        finalStatus.printerStatus.subState = PrinterSubState::AL_AUTO_LEVELING;
                        break;
                    case 2902:
                        finalStatus.printerStatus.subState = PrinterSubState::AL_AUTO_LEVELING_COMPLETED;
                        break;
                    default:
                        finalStatus.printerStatus.subState = PrinterSubState::UNKNOWN;
                    }
                    break;
                case 6:
                    finalStatus.printerStatus.state = PrinterState::PID_CALIBRATING;
                    switch (subStatus)
                    {
                    case 0:
                        finalStatus.printerStatus.subState = PrinterSubState::NONE;
                        break;
                    case 1503:
                        finalStatus.printerStatus.subState = PrinterSubState::PC_PID_CALIBRATING;
                        break;
                    case 1504:
                        finalStatus.printerStatus.subState = PrinterSubState::PC_PID_CALIBRATING;
                        break;
                    case 1505:
                        finalStatus.printerStatus.subState = PrinterSubState::PC_PID_CALIBRATING_COMPLETED;
                        break;
                    case 1506:
                        finalStatus.printerStatus.subState = PrinterSubState::PC_PID_CALIBRATING_FAILED;
                        break;
                    default:
                        finalStatus.printerStatus.subState = PrinterSubState::UNKNOWN;
                    }
                    break;
                case 7:
                    finalStatus.printerStatus.state = PrinterState::RESONANCE_TESTING;
                    switch (subStatus)
                    {
                    case 0:
                        finalStatus.printerStatus.subState = PrinterSubState::NONE;
                        break;
                    case 5934:
                        finalStatus.printerStatus.subState = PrinterSubState::RT_RESONANCE_TEST;
                        break;
                    case 5935:
                        finalStatus.printerStatus.subState = PrinterSubState::RT_RESONANCE_TEST_COMPLETED;
                        break;
                    case 5936:
                        finalStatus.printerStatus.subState = PrinterSubState::RT_RESONANCE_TEST_FAILED;
                        break;
                    default:
                        finalStatus.printerStatus.subState = PrinterSubState::UNKNOWN;
                    }
                    break;
                case 8:
                    finalStatus.printerStatus.state = PrinterState::SELF_CHECKING;
                    switch (subStatus)
                    {
                    case 0:
                        finalStatus.printerStatus.subState = PrinterSubState::NONE;
                        break;
                    case 5934:
                        finalStatus.printerStatus.subState = PrinterSubState::SC_RESONANCE_TEST;
                        break;
                    case 5935:
                        finalStatus.printerStatus.subState = PrinterSubState::SC_RESONANCE_TEST_COMPLETED;
                        break;
                    case 5936:
                        finalStatus.printerStatus.subState = PrinterSubState::SC_RESONANCE_TEST_FAILED;
                        break;
                    case 1503:
                        finalStatus.printerStatus.subState = PrinterSubState::SC_PID_CALIBRATING;
                        break;
                    case 1504:
                        finalStatus.printerStatus.subState = PrinterSubState::SC_PID_CALIBRATING;
                        break;
                    case 1505:
                        finalStatus.printerStatus.subState = PrinterSubState::SC_PID_CALIBRATING_COMPLETED;
                        break;
                    case 1506:
                        finalStatus.printerStatus.subState = PrinterSubState::SC_PID_CALIBRATING_FAILED;
                        break;
                    case 2901:
                        finalStatus.printerStatus.subState = PrinterSubState::SC_AUTO_LEVELING;
                        break;
                    case 2902:
                        finalStatus.printerStatus.subState = PrinterSubState::SC_AUTO_LEVELING_COMPLETED;
                        break;
                    default:
                        finalStatus.printerStatus.subState = PrinterSubState::UNKNOWN;
                    }
                    break;
                case 9:
                    finalStatus.printerStatus.state = PrinterState::UPDATING;
                    switch (subStatus)
                    {
                    case 0:
                        finalStatus.printerStatus.subState = PrinterSubState::NONE;
                        break;
                    case 2061:
                    case 2071:
                    case 2072:
                    case 2073:
                        finalStatus.printerStatus.subState = PrinterSubState::U_UPDATING;
                        break;
                    case 2074:
                        finalStatus.printerStatus.subState = PrinterSubState::U_UPDATING_COMPLETED;
                        break;
                    case 2075:
                        finalStatus.printerStatus.subState = PrinterSubState::U_UPDATING_FAILED;
                        break;
                    default:
                        finalStatus.printerStatus.subState = PrinterSubState::UNKNOWN;
                    }
                    break;
                case 10:
                    finalStatus.printerStatus.state = PrinterState::HOMING;
                    switch (subStatus)
                    {
                    case 0:
                        finalStatus.printerStatus.subState = PrinterSubState::NONE;
                        break;
                    case 2801:
                        finalStatus.printerStatus.subState = PrinterSubState::H_HOMING;
                        break;
                    case 2802:
                        finalStatus.printerStatus.subState = PrinterSubState::H_HOMING_COMPLETED;
                        break;
                    case 2803:
                        finalStatus.printerStatus.subState = PrinterSubState::H_HOMING_FAILED;
                        break;
                    default:
                        finalStatus.printerStatus.subState = PrinterSubState::UNKNOWN;
                    }
                    break;
                case 11:
                    finalStatus.printerStatus.state = PrinterState::FILE_TRANSFERRING;
                    switch (subStatus)
                    {
                    case 0:
                        finalStatus.printerStatus.subState = PrinterSubState::NONE;
                        break;
                    case 3000:
                        finalStatus.printerStatus.subState = PrinterSubState::UF_UPLOADING_FILE;
                        break;
                    case 3001:
                        finalStatus.printerStatus.subState = PrinterSubState::UF_UPLOADING_FILE_COMPLETED;
                        break;
                    default:
                        finalStatus.printerStatus.subState = PrinterSubState::UNKNOWN;
                    }
                    break;
                case 12:
                    finalStatus.printerStatus.state = PrinterState::VIDEO_COMPOSING;
                    finalStatus.printerStatus.subState = PrinterSubState::NONE;
                    break;
                case 13:
                    finalStatus.printerStatus.state = PrinterState::EXTRUDER_OPERATING;
                    switch (subStatus)
                    {
                    case 0:
                        finalStatus.printerStatus.subState = PrinterSubState::NONE;
                        break;
                    case 1061:
                        finalStatus.printerStatus.subState = PrinterSubState::EO_EXTRUDER_LOADING;
                        break;
                    case 1063:
                        finalStatus.printerStatus.subState = PrinterSubState::EO_EXTRUDER_LOADING_COMPLETED;
                        break;
                    case 1062:
                        finalStatus.printerStatus.subState = PrinterSubState::EO_EXTRUDER_UNLOADING;
                        break;
                    case 1064:
                        finalStatus.printerStatus.subState = PrinterSubState::EO_EXTRUDER_UNLOADING_COMPLETED;
                        break;
                    default:
                        finalStatus.printerStatus.subState = PrinterSubState::UNKNOWN;
                        break;
                    }
                    break;
                case 14:
                    finalStatus.printerStatus.state = PrinterState::EMERGENCY_STOP;
                    finalStatus.printerStatus.subState = PrinterSubState::NONE;
                    break;
                case 15:
                    finalStatus.printerStatus.state = PrinterState::POWER_LOSS_RECOVERY;
                    finalStatus.printerStatus.subState = PrinterSubState::NONE;
                    break;
                default:
                    finalStatus.printerStatus.state = PrinterState::UNKNOWN;
                    ELEGOO_LOG_WARN("Unknown machine status: {}", status);
                    break;
                }

                finalStatus.printerStatus.exceptionCodes = JsonUtils::safeGet(printerStatus, "exception_status", std::vector<int>());
                finalStatus.printerStatus.progress = JsonUtils::safeGet(printerStatus, "progress", 0);
                finalStatus.printerStatus.supportProgress = true;
            }

            if (finalStatus.printerStatus.state == PrinterState::PRINTING)
            {
                // Parse print status info
                if (finalResult.contains("print_status") && finalResult["print_status"].is_object())
                {
                    auto printStatus = finalResult["print_status"];
                    finalStatus.printStatus.fileName = JsonUtils::safeGet(printStatus, "filename", std::string());
                    finalStatus.printStatus.totalTime = JsonUtils::safeGet(printStatus, "total_duration", 0);
                    finalStatus.printStatus.currentTime = JsonUtils::safeGet(printStatus, "print_duration", 0);
                    finalStatus.printStatus.totalLayer = JsonUtils::safeGet(printStatus, "total_layer", 0);
                    finalStatus.printStatus.estimatedTime = JsonUtils::safeGet(printStatus, "remaining_time_sec", 0);
                    finalStatus.printStatus.currentLayer = JsonUtils::safeGet(printStatus, "current_layer", 0);
                    // finalStatus.printStatus.progress = (int)(JsonUtils::safeGet(printStatus, "progress", 0.0f) * 100);
                    finalStatus.printStatus.progress = finalStatus.printerStatus.progress;
                }
            }
            else
            {
                finalStatus.printStatus = PrintStatus();
            }

            // Parse temperature info
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

            if (finalResult.contains("ztemperature_sensor") && finalResult["ztemperature_sensor"].is_object())
            {
                auto zSensor = finalResult["ztemperature_sensor"];
                TemperatureStatus sensorTemp;
                sensorTemp.current = JsonUtils::safeGet(zSensor, "temperature", 0.0f);
                sensorTemp.highest = JsonUtils::safeGet(zSensor, "measured_max_temperature", 0.0f);
                sensorTemp.lowest = JsonUtils::safeGet(zSensor, "measured_min_temperature", 0.0f);
                finalStatus.temperatureStatus["chamber"] = sensorTemp;
            }

            // Parse fan info
            if (finalResult.contains("fans") && finalResult["fans"].is_object())
            {
                auto fans = finalResult["fans"];
                if (fans.contains("fan") && fans["fan"].is_object())
                {
                    auto fan = fans["fan"];
                    FanStatus fanStatus;
                    fanStatus.speed = JsonUtils::safeGet(fan, "speed", 0);
                    fanStatus.rpm = JsonUtils::safeGet(fan, "rpm", 0);
                    finalStatus.fanStatus["model"] = fanStatus;
                }

                if (fans.contains("heater_fan") && fans["heater_fan"].is_object())
                {
                    auto heaterFan = fans["heater_fan"];
                    FanStatus fanStatus;
                    fanStatus.speed = JsonUtils::safeGet(heaterFan, "speed", 0);
                    fanStatus.rpm = JsonUtils::safeGet(heaterFan, "rpm", 0);
                    finalStatus.fanStatus["heatsink"] = fanStatus;
                }

                if (fans.contains("controller_fan") && fans["controller_fan"].is_object())
                {
                    auto controllerFan = fans["controller_fan"];
                    FanStatus fanStatus;
                    fanStatus.speed = JsonUtils::safeGet(controllerFan, "speed", 0);
                    fanStatus.rpm = JsonUtils::safeGet(controllerFan, "rpm", 0);
                    finalStatus.fanStatus["controller"] = fanStatus;
                }

                if (fans.contains("box_fan") && fans["box_fan"].is_object())
                {
                    auto boxFan = fans["box_fan"];
                    FanStatus fanStatus;
                    fanStatus.speed = JsonUtils::safeGet(boxFan, "speed", 0);
                    fanStatus.rpm = JsonUtils::safeGet(boxFan, "rpm", 0);
                    finalStatus.fanStatus["chassis"] = fanStatus;
                }

                if (fans.contains("aux_fan") && fans["aux_fan"].is_object())
                {
                    auto auxFan = fans["aux_fan"];
                    FanStatus fanStatus;
                    fanStatus.speed = JsonUtils::safeGet(auxFan, "speed", 0);
                    fanStatus.rpm = JsonUtils::safeGet(auxFan, "rpm", 0);
                    finalStatus.fanStatus["aux"] = fanStatus;
                }
            }

            if (finalResult.contains("led") && finalResult["led"].is_object())
            {
                auto led = finalResult["led"];
                finalStatus.lightStatus["main"].brightness = JsonUtils::safeGet(led, "status", 0);
                finalStatus.lightStatus["main"].connected = true; // Assume main light is always connected
            }

            // Parse movement and axis info
            // if (finalResult.contains("gcode_move") && finalResult["gcode_move"].is_object())
            // {
            //     auto gcodeMove = finalResult["gcode_move"];
            //     // Speed-related info can be added to other structures as needed
            // }

            if (finalResult.contains("toolhead") && finalResult["toolhead"].is_object())
            {
                auto toolhead = finalResult["toolhead"];
                std::string homedAxes = JsonUtils::safeGet(toolhead, "homed_axes", std::string());
                // Parse homed axes
                // finalStatus.printAxesStatus.xHomed = homedAxes.find('x') != std::string::npos;
                // finalStatus.printAxesStatus.yHomed = homedAxes.find('y') != std::string::npos;
                // finalStatus.printAxesStatus.zHomed = homedAxes.find('z') != std::string::npos;
            }

            if (finalResult.contains("gcode_move_inf") && finalResult["gcode_move_inf"].is_object())
            {
                auto gcode_move_inf = finalResult["gcode_move_inf"];
                // finalStatus.printAxesStatus.x = JsonUtils::safeGet(gcode_move_inf, "x", 0.0f);
                // finalStatus.printAxesStatus.y = JsonUtils::safeGet(gcode_move_inf, "y", 0.0f);
                // finalStatus.printAxesStatus.z = JsonUtils::safeGet(gcode_move_inf, "z", 0.0f);
                finalStatus.printAxesStatus.position.clear();
                finalStatus.printAxesStatus.position.push_back(JsonUtils::safeGet(gcode_move_inf, "x", 0.0f));
                finalStatus.printAxesStatus.position.push_back(JsonUtils::safeGet(gcode_move_inf, "y", 0.0f));
                finalStatus.printAxesStatus.position.push_back(JsonUtils::safeGet(gcode_move_inf, "z", 0.0f));
                finalStatus.printAxesStatus.position.push_back(JsonUtils::safeGet(gcode_move_inf, "e", 0.0f));

                finalStatus.printStatus.printSpeedMode = JsonUtils::safeGet(gcode_move_inf, "speed_mode", 0);
            }

            if (finalResult.contains("external_device") && finalResult["external_device"].is_object())
            {
                auto externalPrinter = finalResult["external_device"];
                finalStatus.externalDeviceStatus.usbConnected = JsonUtils::safeGet(externalPrinter, "u_disk", false);
                finalStatus.externalDeviceStatus.cameraConnected = JsonUtils::safeGet(externalPrinter, "camera", false);
                if (externalPrinter.contains("type"))
                {
                    if (externalPrinter["type"].is_string())
                    {
                        std::string type = externalPrinter["type"];
                        finalStatus.externalDeviceStatus.canvasConnected = type.empty() ? false : true;
                        if (type == "0")
                        {
                            finalStatus.externalDeviceStatus.canvasConnected = false;
                        }
                    }
                    else if (externalPrinter["type"].is_number_integer())
                    {
                        int type = externalPrinter["type"];
                        finalStatus.externalDeviceStatus.canvasConnected = (type == 0) ? false : true;
                    }
                }
            }

            if (finalResult.contains("canvas_info") && finalResult["canvas_info"].is_object())
            {
                auto canvasStatusOpt = handleCanvasStatus(finalResult["canvas_info"]);
                finalStatus.canvasStatus = canvasStatusOpt.value();
            }
        }

        return std::optional<PrinterStatusData>(finalStatus);
    }

    std::optional<CanvasStatus> ElegooFdmCC2MessageAdapter::handleCanvasStatus(const nlohmann::json &result)
    {
        CanvasStatus canvasStatus;

        if (result.contains("active_canvas_id"))
        {
            canvasStatus.activeCanvasId = JsonUtils::safeGetInt(result, "active_canvas_id", 0);
        }
        if (result.contains("active_tray_id"))
        {
            canvasStatus.activeTrayId = JsonUtils::safeGetInt(result, "active_tray_id", 0);
        }
        if (result.contains("auto_refill"))
        {
            canvasStatus.autoRefill = JsonUtils::safeGetBool(result, "auto_refill", false);
        }
        if (result.contains("canvas_list") && result["canvas_list"].is_array())
        {
            auto canvasList = result["canvas_list"];
            for (const auto &canvasInfo : canvasList)
            {
                CanvasInfo info;
                info.canvasId = canvasInfo.value("canvas_id", 0);
                info.connected = canvasInfo.value("connected", 0);
                if (canvasInfo.contains("tray_list") && canvasInfo["tray_list"].is_array())
                {
                    auto trayInfos = canvasInfo["tray_list"];
                    for (const auto &trayInfo : trayInfos)
                    {
                        TrayInfo tinfo;
                        tinfo.trayId = JsonUtils::safeGetInt(trayInfo, "tray_id", 0);
                        tinfo.brand = JsonUtils::safeGetString(trayInfo, "brand", "");
                        tinfo.filamentType = JsonUtils::safeGetString(trayInfo, "filament_type", "");
                        tinfo.filamentName = JsonUtils::safeGetString(trayInfo, "filament_name", "");
                        tinfo.filamentCode = JsonUtils::safeGetString(trayInfo, "filament_code", "");
                        tinfo.filamentColor = JsonUtils::safeGetString(trayInfo, "filament_color", "");
                        tinfo.minNozzleTemp = JsonUtils::safeGetInt(trayInfo, "min_nozzle_temp", 0);
                        tinfo.maxNozzleTemp = JsonUtils::safeGetInt(trayInfo, "max_nozzle_temp", 0);
                        tinfo.status = JsonUtils::safeGetInt(trayInfo, "status", 0);
                        info.trays.push_back(tinfo);
                    }
                }
                canvasStatus.canvases.push_back(info);
            }
        }
        return std::optional<CanvasStatus>(canvasStatus);
    }

    void ElegooFdmCC2MessageAdapter::resetStatusSequence()
    {
        std::lock_guard<std::mutex> lock(statusSequenceMutex_);
        lastStatusEventId_ = -1;
        printerStatusSequenceState_ = 0;
        nonContinuousCount_ = 0;

        // Also clear status cache
        clearStatusCache();

        ELEGOO_LOG_DEBUG("Status event sequence and cache reset");
    }

    bool ElegooFdmCC2MessageAdapter::checkStatusEventContinuity(int currentId)
    {
        std::lock_guard<std::mutex> lock(statusSequenceMutex_);

        if (lastStatusEventId_ == -1)
        {
            // First time receiving status event
            lastStatusEventId_ = currentId;
            return true;
        }

        bool isContinuous = (currentId == lastStatusEventId_ + 1) || (currentId == 0);
        lastStatusEventId_ = currentId;

        return isContinuous;
    }

    void ElegooFdmCC2MessageAdapter::cacheFullPrinterStatusJson(const nlohmann::json &fullStatusResult)
    {
        std::lock_guard<std::mutex> lock(statusCacheMutex_);
        cachedFullStatusJson_ = fullStatusResult;
        hasFullStatusCache_ = true;
        ELEGOO_LOG_DEBUG("Cached full printer status JSON for printer {}", StringUtils::maskString(printerInfo_.printerId));
    }

    nlohmann::json ElegooFdmCC2MessageAdapter::mergeStatusUpdateJson(const nlohmann::json &deltaStatusResult)
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

    void ElegooFdmCC2MessageAdapter::clearStatusCache()
    {
        std::lock_guard<std::mutex> lock(statusCacheMutex_);
        hasFullStatusCache_ = false;
        cachedFullStatusJson_ = nlohmann::json::object();
        ELEGOO_LOG_DEBUG("Cleared status cache for printer {}", StringUtils::maskString(printerInfo_.printerId));
    }

    int ElegooFdmCC2MessageAdapter::mapCommandType(MethodType command)
    {
        for (const auto &[methodType, commandCode] : COMMAND_MAPPING_TABLE)
        {
            if (methodType == command)
            {
                return commandCode;
            }
        }
        return -1; // Unknown command returns -1
    }

    MethodType ElegooFdmCC2MessageAdapter::mapPrinterCommand(int printerCommand)
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

    nlohmann::json ElegooFdmCC2MessageAdapter::createStandardBody() const
    {
        nlohmann::json body;
        body["id"] = printerInfo_.printerId; // Printer ID
        return body;
    }

    // CCS printer command mapping table - static member, avoid repeated creation
    const std::vector<std::pair<MethodType, int>> ElegooFdmCC2MessageAdapter::COMMAND_MAPPING_TABLE = {
        {MethodType::GET_PRINTER_ATTRIBUTES, 1001}, // Version negotiation request
        {MethodType::GET_PRINTER_STATUS, 1002},     // Get status
        {MethodType::ON_PRINTER_STATUS, 6000},      // Printer status
        {MethodType::ON_PRINTER_ATTRIBUTES, 6008},  // Printer attributes
        {MethodType::START_PRINT, 1020},            // Start print
        {MethodType::PAUSE_PRINT, 1021},            // Pause print
        {MethodType::STOP_PRINT, 1022},             // Stop print
                                                    // {MethodType::RESUME_PRINT, 1023},      // Resume print
                                                    // {MethodType::HOME_AXES, 1026},         // Home axes
                                                    // {MethodType::MOVE_AXES, 1027},         // Move axes
                                                    // {MethodType::SET_TEMPERATURE, 1028},   // Set temperature
                                                    // {MethodType::SET_LIGHT, 1029},         // Set light brightness
                                                    // {MethodType::SET_FAN_SPEED, 1030},     // Set fan speed
                                                    // {MethodType::SET_PRINT_SPEED, 1031},   // Set print speed mode
                                                    // {MethodType::PRINT_TASK_LIST, 1036},   // Get print task list
                                                    // {MethodType::PRINT_TASK_DETAIL, 1037}, // Get print task detail
                                                    // {MethodType::DELETE_PRINT_TASK, 1038}, // Delete print task
                                                    // {MethodType::GET_FILE_LIST, 1044},     // Get file list
                                                    // {MethodType::GET_FILE_DETAIL, 1046},   // Get file detail
                                                    // {MethodType::DELETE_FILE, 1047},       // Delete file
                                                    // {MethodType::GET_DISK_INFO, 1048},     // Get disk capacity
                                                    // {MethodType::VIDEO_STREAM, 1042},      // Get camera video stream
        {MethodType::UPDATE_PRINTER_NAME, 1043},    // Set machine name
        // {MethodType::LOAD_FILAMENT, 1024},     // Load filament
        // {MethodType::UNLOAD_FILAMENT, 1025},   // Load/unload filament
        // {MethodType::EXPORT_TIMELAPSE_VIDEO, 1045}, // Export timelapse video, interface not designed yet
        {MethodType::GET_CANVAS_STATUS, 2005}, // Get canvas status
        {MethodType::SET_AUTO_REFILL, 2004},   // Set auto refill
        {MethodType::SET_PRINTER_DOWNLOAD_FILE, 1057},
        {MethodType::CANCEL_PRINTER_DOWNLOAD_FILE, 1058},

    };

    ELINK_ERROR_CODE ElegooFdmCC2MessageAdapter::convertRequestErrorToElegooError(int code) const
    {
        // 0       Success
        // 1000    Token validation failed
        // 1001    Unknown interface
        // 1002    Failed to open folder
        // 1003    Invalid parameter
        // 1004    File write failed
        // 1005    Failed to update token
        // 1006    Failed to send update to MOS
        // 1007    File deletion failed
        // 1008    Response data is empty
        // 1009    Printer is busy
        // 1010    Printer is not in printing state
        // 1011    File copy failed
        // 1012    Task not found
        // 1013    Database operation failed
        // 9000    File offset does not match the current file
        // 9001    Failed to open file for writing
        // 9002    File write failed
        // 9003    File positioning failed
        // 9004    MD5 checksum failed
        // 9005    No cancellation required for the current client
        // 9006    Cancellation failed
        // 9007    Upload path does not exist
        // 9008    MD5 checksum failed (system error)
        // 9009    MD5 checksum failed (file read error)
        // 9999    Other unknown errors
        switch (code)
        {
        case 0:
            return ELINK_ERROR_CODE::SUCCESS;
        case 109:
            return ELINK_ERROR_CODE::PRINTER_FILAMENT_RUNOUT;
        case 1000:
            return ELINK_ERROR_CODE::PRINTER_ACCESS_DENIED;
        case 1001:
            return ELINK_ERROR_CODE::PRINTER_INVALID_PARAMETER;
        case 1003:
            return ELINK_ERROR_CODE::PRINTER_INVALID_PARAMETER;
        case 1009:
            return ELINK_ERROR_CODE::PRINTER_BUSY;
        case 1021:
            return ELINK_ERROR_CODE::PRINTER_PRINT_FILE_NOT_FOUND;
        case 1026:
            return ELINK_ERROR_CODE::PRINTER_MISSING_BED_LEVELING_DATA;
        default:
            ELEGOO_LOG_WARN("Unknown error code: {}", code);
            return ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR; // Default to unknown error
        }
    }
} // namespace elink
