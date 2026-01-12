#include "adapters/elegoo_cc_adapters.h"
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

    namespace cc
    {
        typedef enum
        {
            SDCP_MACHINE_STATUS_IDLE = 0,              // Idle
            SDCP_MACHINE_STATUS_PRINTING = 1,          // Printing task in progress
            SDCP_MACHINE_STATUS_FILE_TRANSFERRING = 2, // File transferring
            SDCP_MACHINE_STATUS_EXPOSURE_TESTING = 3,  // Exposure testing (for resin printers)
            SDCP_MACHINE_STATUS_PRINTERS_TESTING = 4,  // Printer self-check
            SDCP_MACHINE_STATUS_AUTO_LEVEL = 5,        // Auto leveling
            SDCP_MACHINE_STATUS_RESONANCE_TESTING = 6, // Resonance testing (FDM)
            SDCP_MACHINE_STATUS_OTHERS_BUSY = 7,       // Machine busy, reason reserved (FDM)
            SDCP_MACHINE_STATUS_FILE_CHECKING = 8,     // File checking
            SDCP_MACHINE_STATUS_HOMING = 9,            // Manual homing (FDM)
            SDCP_MACHINE_STATUS_FEED_OUT = 10,         // Filament unloading
            SDCP_MACHINE_STATUS_PID_DETECT = 11        // PID detection (FDM)
        } sdcp_machine_status_t;

        typedef enum
        {
            SDCP_PRINT_STATUS_IDLE = 0,                         // Idle
            SDCP_PRINT_STATUS_HOMING = 1,                       // Homing
            SDCP_PRINT_STATUS_DROPPING = 2,                     // Dropping (resin printer only)
            SDCP_PRINT_STATUS_EXPOSURING = 3,                   // Exposing (resin printer only)
            SDCP_PRINT_STATUS_LIFTING = 4,                      // Lifting (resin printer only)
            SDCP_PRINT_STATUS_PAUSING = 5,                      // Pausing in progress
            SDCP_PRINT_STATUS_PAUSED = 6,                       // Paused
            SDCP_PRINT_STATUS_STOPPING = 7,                     // Stopping in progress
            SDCP_PRINT_STATUS_STOPED = 8,                       // Stopped
            SDCP_PRINT_STATUS_COMPLETE = 9,                     // Print completed
            SDCP_PRINT_STATUS_FILE_CHECKING = 10,               // File checking
            SDCP_PRINT_STATUS_PRINTERS_CHECKING = 11,           // Printer checking
            SDCP_PRINT_STATUS_RESUMING = 12,                    // Resuming
            SDCP_PRINT_STATUS_PRINTING = 13,                    // Printing (FDM)
            PRINT_STATS_STATE_ERROR = 14,                       // Print stopped due to error
            PRINT_STATS_STATE_AUTOLEVELING = 15,                // Auto leveling
            PRINT_STATS_STATE_PREHEATING = 16,                  // Preheating (lcd)
            PRINT_STATS_STATE_RESONANCE_TESTING = 17,           // Resonance testing
            PRINT_STATS_STATE_PRINT_START = 18,                 // Print started
            PRINT_STATS_STATE_AUTOLEVELING_COMPLETED = 19,      // Auto leveling completed
            PRINT_STATS_STATE_PREHEATING_COMPLETED = 20,        // Preheating completed
            PRINT_STATS_STATE_HOMING_COMPLETED = 21,            // Homing completed
            PRINT_STATS_STATE_RESONANCE_TESTING_COMPLETED = 22, // Resonance testing completed
            SDCP_PRINT_STATUS_AUTO_FEEDING = 23,                // Auto feeding (lcd prz)
            SDCP_PRINT_STATUS_FEEDOUT = 24,                     // Filament unloading (lcd prz)
            SDCP_PRINT_STATUS_FEEDOUT_ABNORMAL = 25,            // Filament unloading abnormal (lcd prz)
            SDCP_PRINT_STATUS_FEEDOUT_PAUSED = 26               // Filament unloading paused (lcd prz)
        } sdcp_print_status_t;
    }

    // ========== ElegooFdmCCMessageAdapter Implementation ==========

    ElegooFdmCCMessageAdapter::ElegooFdmCCMessageAdapter(const PrinterInfo &printerInfo)
        : BaseMessageAdapter(printerInfo)
    {
    }

    PrinterBizRequest<std::string> ElegooFdmCCMessageAdapter::convertRequest(MethodType method, const nlohmann::json &request, std::chrono::milliseconds timeout)
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
            printerMessage["RequestID"] = printerRequestId;
            printerMessage["MainboardID"] = request.value("printerId", printerInfo_.printerId);
            printerMessage["TimeStamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now().time_since_epoch())
                                              .count();

            int commandInt = static_cast<int>(method);
            MethodType command = static_cast<MethodType>(commandInt);

            int printerCommand = mapCommandType(command);
            if (printerCommand == -1)
            {
                bizRequest.code = ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED;
                bizRequest.message = "Command not implemented";
                return bizRequest;
            }

            printerMessage["Cmd"] = printerCommand;
            printerMessage["From"] = 1;

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
                if (startPrintData.storageLocation == "local")
                {
                    pathPrefix = "/local";
                }
                else if (startPrintData.storageLocation == "udisk")
                {
                    pathPrefix = "/usb";
                }
                else
                {
                    pathPrefix = "/local";
                }

                param["Filename"] = startPrintData.fileName;
                param["StartLayer"] = 0;
                param["Calibration_switch"] = startPrintData.autoBedLeveling ? 1 : 0;
                param["PrintPlatformType"] = startPrintData.heatedBedType == 0 ? 0 : 1;
                param["Tlp_Switch"] = startPrintData.enableTimeLapse ? 1 : 0;
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
                param["slot_map"] = slotMapJson;
                printerMessage["Data"] = param;
                break;
            }
            case MethodType::HOME_AXES:
            {
                auto moveData = request.get<HomeAxisParams>();
                nlohmann::json param;
                // Convert to uppercase
                std::transform(moveData.axes.begin(), moveData.axes.end(), moveData.axes.begin(), ::toupper);
                param["Axis"] = moveData.axes;
                printerMessage["Data"] = param;
                break;
            }
            case MethodType::MOVE_AXES:
            {
                auto moveData = request.get<MoveAxisParams>();
                nlohmann::json param;
                // Convert to uppercase
                std::transform(moveData.axes.begin(), moveData.axes.end(), moveData.axes.begin(), ::toupper);
                param["Axis"] = moveData.axes;
                param["Step"] = moveData.distance;
                printerMessage["Data"] = param;
                break;
            }
            case MethodType::SET_TEMPERATURE:
            {
                auto tempData = request.get<SetTemperatureParams>();
                nlohmann::json param;
                if (tempData.temperatures.find("heatedBed") != tempData.temperatures.end())
                {
                    param["TempTargetHotbed"] = tempData.temperatures["heatedBed"];
                }
                if (tempData.temperatures.find("extruder") != tempData.temperatures.end())
                {
                    param["TempTargetNozzle"] = tempData.temperatures["extruder"];
                }
                if (tempData.temperatures.find("chamber") != tempData.temperatures.end())
                {
                    param["TempTargetBox"] = tempData.temperatures["chamber"];
                }
                printerMessage["Data"] = param;
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
                        param["TargetFanSpeed"]["ModelFan"] = fan.second;
                    }
                    else if (fan.first == "chassis")
                    {
                        param["TargetFanSpeed"]["BoxFan"] = fan.second;
                    }
                    else if (fan.first == "aux")
                    {
                        param["TargetFanSpeed"]["AuxiliaryFan"] = fan.second;
                    }
                    else
                    {
                        // Other fan types, not handled for now
                        ELEGOO_LOG_WARN("Unknown fan type: {}", fan.first);
                    }
                }

                printerMessage["Data"] = param;
                break;
            }
            case MethodType::SET_PRINT_SPEED:
            {
                // Speed mode not implemented yet
                SetPrintSpeedParams speedData = request.get<SetPrintSpeedParams>();
                nlohmann::json param;
                param["PrintSpeedPct"] = speedData.speedMode;
                printerMessage["Data"] = param;

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
                auto nameData = request.get<UpdatePrinterNameParams>();
                // Update local printer info
                printerInfo_.name = nameData.printerName;

                bizRequest.code = ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED;
                bizRequest.message = "Command not implemented";
                return bizRequest;
            }
            case MethodType::GET_PRINTER_ATTRIBUTES:
            case MethodType::GET_PRINTER_STATUS:
            default:
                printerMessage["Data"] = nlohmann::json::object();
                break;
            }

            auto body = createStandardBody();
            body["Data"] = printerMessage;
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

    PrinterBizResponse<nlohmann::json> ElegooFdmCCMessageAdapter::convertToResponse(const std::string &printerResponse)
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

            // Check if printer response contains status or attributes
            // If so, handle them separately
            if (printerJson.contains("Status") || printerJson.contains("Attributes"))
            {
                if (printerJson.contains("Status"))
                {
                    method = MethodType::GET_PRINTER_STATUS;
                }
                else if (printerJson.contains("Attributes"))
                {
                    method = MethodType::GET_PRINTER_ATTRIBUTES;
                }

                auto oldestMethodTypeRecord = getOldestMethodTypeRecord(method);
                if (!oldestMethodTypeRecord.has_value())
                {
                    response.requestId = generateMessageId();
                    ELEGOO_LOG_DEBUG("No request mapping found for printer response, using fallback id: {}",
                                     response.requestId);
                    return PrinterBizResponse<nlohmann::json>::error(ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE, "No request mapping found for printer response");
                }
                if (!oldestMethodTypeRecord->standardMessageId.empty())
                {
                    response.requestId = oldestMethodTypeRecord->standardMessageId;
                    method = oldestMethodTypeRecord->method;
                    // Clean up completed request record
                    removeRequestRecord(oldestMethodTypeRecord->printerRequestId);
                    ELEGOO_LOG_DEBUG("Found request mapping for printer response: {} -> {}",
                                     oldestMethodTypeRecord->printerRequestId, oldestMethodTypeRecord->standardMessageId);

                    if (method == MethodType::GET_PRINTER_ATTRIBUTES)
                    {
                        response.data = handlePrinterAttributes(printerJson);
                    }
                    else if (method == MethodType::GET_PRINTER_STATUS)
                    {
                        response.data = handlePrinterStatus(printerJson);
                    }
                    response.code = ELINK_ERROR_CODE::SUCCESS;
                    response.message = "Success";
                    return response;
                }

                return PrinterBizResponse<nlohmann::json>::error(ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE, "No request mapping found for printer response");
            }

            if (printerJson.contains("Data") && printerJson["Data"].is_object())
            {
                printerJson = printerJson["Data"];
            }
            else
            {
                return PrinterBizResponse<nlohmann::json>::error(ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE, "No Data field in printer response");
            }

            // Try to extract ID from printer response and find corresponding standard request ID
            std::string printerResponseId = "";
            if (printerJson.contains("RequestID"))
            {
                printerResponseId = JsonUtils::safeGetString(printerJson, "RequestID", "");
            }

            if (printerResponseId.empty())
            {
                ELEGOO_LOG_ERROR("No RequestID in printer response");
                return PrinterBizResponse<nlohmann::json>::error(ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE, "No RequestID in printer response");
            }

            // Find request record
            RequestRecord record = findRequestRecord(printerResponseId);
            if (!record.standardMessageId.empty())
            {
                response.requestId = record.standardMessageId;
                method = record.method;
                if (method != MethodType::GET_PRINTER_ATTRIBUTES &&
                    method != MethodType::GET_PRINTER_STATUS)
                {
                    // Clean up completed request record
                    removeRequestRecord(printerResponseId);
                    ELEGOO_LOG_DEBUG("Found request mapping for printer response: {} -> {}",
                                     printerResponseId, record.standardMessageId);
                }
            }
            else
            {
                ELEGOO_LOG_DEBUG("No request mapping found for printer response: {}, using fallback id: {}",
                                 printerResponseId, response.requestId);
                return PrinterBizResponse<nlohmann::json>::error(ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE, "No request mapping found for printer response");
            }

            if (printerJson.contains("Data") && printerJson["Data"].is_object())
            {
                auto result = printerJson["Data"];
                // if(!result.contains("Ack") ){
                //     result["Ack"] = 0;
                // }
                if (result.contains("Ack") && result["Ack"].is_number_integer())
                {
                    int ack = result["Ack"];
                    if (ack == 0)
                    {
                        response.code = ELINK_ERROR_CODE::SUCCESS; // Default success status
                        response.message = "Success";

                        switch (method)
                        {
                        case MethodType::GET_PRINTER_ATTRIBUTES:
                        case MethodType::GET_PRINTER_STATUS:
                        {
                            // skip printer attributes and status response handling
                            // will be handled in event
                            return PrinterBizResponse<nlohmann::json>::success();
                        }
                        case MethodType::GET_CANVAS_STATUS:
                        {
                            response.data = handleCanvasStatus(result);
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
                        // typedef enum {
                        //     SDCP_PRINT_CTRL_ACK_OK = 0,  // OK
                        //     SDCP_PRINT_CTRL_ACK_BUSY = 1,  // 设备忙
                        //     SDCP_PRINT_CTRL_ACK_NOT_FOUND = 2,  // 未找到目标文件
                        //     SDCP_PRINT_CTRL_ACK_MD5_FAILED = 3,  // MD5 校验失败
                        //     SDCP_PRINT_CTRL_ACK_FILEIO_FAILED = 4,  // 文件读取失败
                        //     SDCP_PRINT_CTRL_ACK_INVLAID_RESOLUTION = 5,  // 文件分辨率不匹配
                        //     SDCP_PRINT_CTRL_ACK_UNKNOW_FORMAT = 5,  // 无法识别的文件格式
                        //     SDCP_PRINT_CTRL_ACK_UNKNOW_MODEL = 6  // 文件机型不匹配
                        // } sdcp_print_ctrl_ack_t;
                        nlohmann::json errorData;
                        response.code = ELINK_ERROR_CODE::PRINTER_UNKNOWN_ERROR; // Default error
                        response.message = StringUtils::formatErrorMessage("Unknown error.", ack);
                        response.data = errorData;
                    }
                } else {
                    response.message = "No Ack in response";
                    response.code = ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE; // Default error
                }
            }
            else
            {
                response.data = nlohmann::json::object();
                response.message = "No data in response";
                response.code = ELINK_ERROR_CODE::PRINTER_INVALID_RESPONSE; // Default error
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

    PrinterBizEvent ElegooFdmCCMessageAdapter::convertToEvent(const std::string &printerMessage)
    {
        try
        {
            auto printerJson = parseJson(printerMessage);
            if (printerJson.empty())
            {
                ELEGOO_LOG_ERROR("Invalid printer event format: {}", printerMessage);
                return PrinterBizEvent();
            }

            PrinterBizEvent event;
            event.method = MethodType::UNKNOWN;
            nlohmann::json data;
            data["printerId"] = printerInfo_.printerId;

            // V1 printer event message parsing
            if (printerJson.contains("Status"))
            {
                event.method = MethodType::ON_PRINTER_STATUS;
                data = handlePrinterStatus(printerJson);
            }
            else if (printerJson.contains("Attributes"))
            {
                event.method = MethodType::ON_PRINTER_ATTRIBUTES;
                data = handlePrinterAttributes(printerJson);
            }

            event.data = data;
            return event;
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Error converting V1 printer event: {}", e.what());
            return PrinterBizEvent();
        }
    }

    std::vector<std::string> ElegooFdmCCMessageAdapter::parseMessageType(const std::string &printerMessage)
    {
        std::vector<std::string> messageTypes;
        try
        {
            auto json = parseJson(printerMessage);
            if (json.contains("Topic"))
            {
                std::string type = json["Topic"];
                // Contains response
                if (type.find("sdcp/response") != std::string::npos)
                {
                    // return "response";
                    messageTypes.push_back("response");
                }
                else if (type.find("sdcp/attributes") != std::string::npos)
                {
                    // if (hasMethodTypeRecord(MethodType::GET_PRINTER_ATTRIBUTES))
                    // {
                    //     messageTypes.push_back("response");
                    // }
                    // else
                    // {
                    //     messageTypes.push_back("event");
                    // }

                    messageTypes.push_back("response");
                    messageTypes.push_back("event");
                }
                else if (type.find("sdcp/status") != std::string::npos)
                {
                    // if (hasMethodTypeRecord(MethodType::GET_PRINTER_STATUS))
                    // {
                    //     messageTypes.push_back("response");
                    // }
                    // else
                    // {
                    //     messageTypes.push_back("event");
                    // }
                    messageTypes.push_back("response");
                    messageTypes.push_back("event");
                }
                else
                {
                    messageTypes.push_back("event");
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
    PrinterStatusData ElegooFdmCCMessageAdapter::handlePrinterStatus(const nlohmann::json &printerJson) const
    {
        PrinterStatusData printerStatusData(printerInfo_.printerId);
        if (!printerJson.contains("Status") || !printerJson["Status"].is_object())
        {
            ELEGOO_LOG_ERROR("Invalid printer status format: {}", printerJson.dump());
            return printerStatusData;
        }

        auto statusJson = printerJson["Status"];
        if (statusJson.contains("CurrentStatus"))
        {
            auto status = statusJson["CurrentStatus"];
            printerStatusData.printerStatus.subState = PrinterSubState::NONE; // Default sub status is 0
            printerStatusData.printerStatus.state = PrinterState::UNKNOWN;    // Default status is unknown
            // This machine status is an array list
            if (status.is_array())
            {
                std::vector<int> statusList;
                for (const auto &s : status)
                {
                    if (s.is_number_integer())
                    {
                        statusList.push_back(s.get<int>());
                    }
                }

                int currentStatus = statusList.empty() ? 0 : statusList[0];

                // File transfer status has lower priority
                if (currentStatus == cc::sdcp_machine_status_t::SDCP_MACHINE_STATUS_FILE_TRANSFERRING)
                {
                    if (statusList.size() > 1)
                    {
                        currentStatus = statusList[1]; // Use the second status
                    }
                }

                switch (currentStatus)
                {
                case cc::sdcp_machine_status_t::SDCP_MACHINE_STATUS_IDLE:
                    printerStatusData.printerStatus.state = PrinterState::IDLE;
                    break;
                case cc::sdcp_machine_status_t::SDCP_MACHINE_STATUS_PRINTING:
                    printerStatusData.printerStatus.state = PrinterState::PRINTING;
                    break;
                case cc::sdcp_machine_status_t::SDCP_MACHINE_STATUS_FILE_TRANSFERRING:
                    printerStatusData.printerStatus.state = PrinterState::FILE_TRANSFERRING;
                    break;
                // case cc::sdcp_machine_status_t::SDCP_MACHINE_STATUS_EXPOSURE_TESTING:
                //     printerStatusData.printerStatus.state = PrinterState::EXPOSURE_TESTING;
                //     break;
                case cc::sdcp_machine_status_t::SDCP_MACHINE_STATUS_PRINTERS_TESTING:
                    printerStatusData.printerStatus.state = PrinterState::SELF_CHECKING;
                    break;
                case cc::sdcp_machine_status_t::SDCP_MACHINE_STATUS_AUTO_LEVEL:
                    printerStatusData.printerStatus.state = PrinterState::AUTO_LEVELING;
                    break;
                case cc::sdcp_machine_status_t::SDCP_MACHINE_STATUS_RESONANCE_TESTING:
                    printerStatusData.printerStatus.state = PrinterState::RESONANCE_TESTING;
                    break;
                case cc::sdcp_machine_status_t::SDCP_MACHINE_STATUS_OTHERS_BUSY:
                    printerStatusData.printerStatus.state = PrinterState::BUSY;
                    break;
                case cc::sdcp_machine_status_t::SDCP_MACHINE_STATUS_FILE_CHECKING:
                    printerStatusData.printerStatus.state = PrinterState::FILE_TRANSFERRING;
                    break;
                case cc::sdcp_machine_status_t::SDCP_MACHINE_STATUS_HOMING:
                    printerStatusData.printerStatus.state = PrinterState::HOMING;
                    break;
                case cc::sdcp_machine_status_t::SDCP_MACHINE_STATUS_FEED_OUT:
                    printerStatusData.printerStatus.state = PrinterState::FILAMENT_OPERATING;
                    // printerStatusData.printerStatus.subState = PrinterSubState::FO_FILAMENT_UNLOADING;
                    break;
                case cc::sdcp_machine_status_t::SDCP_MACHINE_STATUS_PID_DETECT:
                    printerStatusData.printerStatus.state = PrinterState::PID_CALIBRATING;
                    printerStatusData.printerStatus.subState = PrinterSubState::NONE;
                    break;
                default:
                    printerStatusData.printerStatus.state = PrinterState::UNKNOWN;
                    ELEGOO_LOG_WARN("Unknown machine status: {}", currentStatus);
                    break;
                }
            }
        }

        if (statusJson.contains("PrintInfo") && statusJson["PrintInfo"].is_object())
        {
            auto printInfo = statusJson["PrintInfo"];
            int printStatus = JsonUtils::safeGetInt(printInfo, "Status", -1);
            switch (printStatus)
            {
            case cc::sdcp_print_status_t::SDCP_PRINT_STATUS_IDLE:
                printerStatusData.printerStatus.subState = PrinterSubState::P_PRINTING;
                break;
            case cc::sdcp_print_status_t::SDCP_PRINT_STATUS_HOMING:
                printerStatusData.printerStatus.subState = PrinterSubState::P_HOMING;
                break;
            case cc::sdcp_print_status_t::SDCP_PRINT_STATUS_PAUSING:
                printerStatusData.printerStatus.subState = PrinterSubState::P_PAUSING;
                break;
            case cc::sdcp_print_status_t::SDCP_PRINT_STATUS_PAUSED:
                printerStatusData.printerStatus.subState = PrinterSubState::P_PAUSED;
                break;
            case cc::sdcp_print_status_t::SDCP_PRINT_STATUS_STOPPING:
                printerStatusData.printerStatus.subState = PrinterSubState::P_STOPPING;
                break;
            case cc::sdcp_print_status_t::SDCP_PRINT_STATUS_STOPED:
                printerStatusData.printerStatus.subState = PrinterSubState::P_STOPPED;
                break;
            case cc::sdcp_print_status_t::SDCP_PRINT_STATUS_COMPLETE:
                printerStatusData.printerStatus.state = PrinterState::PRINTING;
                printerStatusData.printerStatus.subState = PrinterSubState::P_PRINTING_COMPLETED;
                break;
            case cc::sdcp_print_status_t::SDCP_PRINT_STATUS_FILE_CHECKING:
                printerStatusData.printerStatus.subState = PrinterSubState::P_PRINTING;
                break;
            case cc::sdcp_print_status_t::SDCP_PRINT_STATUS_PRINTERS_CHECKING:
                printerStatusData.printerStatus.subState = PrinterSubState::P_PRINTING;
                break;
            case cc::sdcp_print_status_t::SDCP_PRINT_STATUS_RESUMING:
                printerStatusData.printerStatus.subState = PrinterSubState::P_RESUMING;
                break;
            case cc::sdcp_print_status_t::SDCP_PRINT_STATUS_PRINTING:
                printerStatusData.printerStatus.subState = PrinterSubState::P_PRINTING;
                break;
            case cc::sdcp_print_status_t::PRINT_STATS_STATE_ERROR:
                printerStatusData.printerStatus.subState = PrinterSubState::UNKNOWN;
                break;
            case cc::sdcp_print_status_t::PRINT_STATS_STATE_AUTOLEVELING:
                printerStatusData.printerStatus.subState = PrinterSubState::P_AUTO_LEVELING;
                break;
            case cc::sdcp_print_status_t::PRINT_STATS_STATE_PREHEATING:
                printerStatusData.printerStatus.subState = PrinterSubState::P_PREHEATING;
                break;
            case cc::sdcp_print_status_t::PRINT_STATS_STATE_RESONANCE_TESTING:
                printerStatusData.printerStatus.subState = PrinterSubState::P_PRINTING;
                break;
            case cc::sdcp_print_status_t::PRINT_STATS_STATE_PRINT_START:
                printerStatusData.printerStatus.subState = PrinterSubState::P_PRINTING;
                break;
            case cc::sdcp_print_status_t::PRINT_STATS_STATE_AUTOLEVELING_COMPLETED:
                printerStatusData.printerStatus.subState = PrinterSubState::P_AUTO_LEVELING;
                break;
            case cc::sdcp_print_status_t::PRINT_STATS_STATE_PREHEATING_COMPLETED:
                printerStatusData.printerStatus.subState = PrinterSubState::P_PREHEATING;
                break;
            case cc::sdcp_print_status_t::PRINT_STATS_STATE_HOMING_COMPLETED:
                printerStatusData.printerStatus.subState = PrinterSubState::P_PRINTING;
                break;
            case cc::sdcp_print_status_t::PRINT_STATS_STATE_RESONANCE_TESTING_COMPLETED:
                printerStatusData.printerStatus.subState = PrinterSubState::P_PRINTING;
                break;
                //     case cc::sdcp_print_status_t::SDCP_PRINT_STATUS_AUTO_FEEDING:
                // printerStatusData.printerStatus.subState = PrinterSubState::P_AUTO_FEEDING;
                // break;
                //     case cc::sdcp_print_status_t::SDCP_PRINT_STATUS_FEEDOUT:
                // printerStatusData.printerStatus.subState = PrinterSubState::P_FEEDOUT;
                // break;
                //     case cc::sdcp_print_status_t::SDCP_PRINT_STATUS_FEEDOUT_ABNORMAL:
                // printerStatusData.printerStatus.subState = PrinterSubState::P_FEEDOUT_ABNORMAL;
                // break;
                //     case cc::sdcp_print_status_t::SDCP_PRINT_STATUS_FEEDOUT_PAUSED:
                // printerStatusData.printerStatus.subState = PrinterSubState::P_FEEDOUT_PAUSED;
                // break;

            default:
                printerStatusData.printerStatus.subState = PrinterSubState::UNKNOWN;
                break;
            }

            if (printerStatusData.printerStatus.state != PrinterState::PRINTING)
            {
                printerStatusData.printerStatus.subState = PrinterSubState::NONE;
                printerStatusData.printStatus = PrintStatus(); // Reset print status
            }
            else
            {
                printerStatusData.printStatus.progress = JsonUtils::safeGetInt(printInfo, "Progress", 0);                                          // Print progress
                printerStatusData.printStatus.currentLayer = JsonUtils::safeGetInt(printInfo, "CurrentLayer", 0);                                  // Current print layer
                printerStatusData.printStatus.totalLayer = JsonUtils::safeGetInt(printInfo, "TotalLayer", 0);                                      // Total layers in print file
                printerStatusData.printStatus.currentTime = static_cast<int64_t>(JsonUtils::safeGetDouble(printInfo, "CurrentTicks", 0));          // Current printed time
                printerStatusData.printStatus.totalTime = static_cast<int64_t>(JsonUtils::safeGetDouble(printInfo, "TotalTicks", 0));              // Total print time
                printerStatusData.printStatus.estimatedTime = printerStatusData.printStatus.totalTime - printerStatusData.printStatus.currentTime; // Estimated remaining time (s)
                if (printerStatusData.printStatus.estimatedTime < 0)
                {
                    printerStatusData.printStatus.estimatedTime = 0;
                }
                printerStatusData.printStatus.fileName = JsonUtils::safeGetString(printInfo, "Filename", ""); // Print file name
                printerStatusData.printStatus.taskId = JsonUtils::safeGetString(printInfo, "TaskId", "");     // Print task ID
                int speedPct = JsonUtils::safeGetInt(printInfo, "PrintSpeedPct", 0);                          // Print speed mode
                int speedMode = 0;                                                                            // Default silent mode
                if (speedMode < 100)
                {
                    speedMode = 0; // Silent mode
                }
                else if (speedPct < 130 && speedPct >= 100)
                {
                    speedMode = 1; // Balanced mode
                }
                else if (speedPct < 160 && speedPct >= 130)
                {
                    speedMode = 2; // Sport mode
                }
                else if (speedPct >= 160)
                {
                    speedMode = 3; // Furious mode
                }
                printerStatusData.printStatus.printSpeedMode = speedMode; // Print speed mode
            }
        }

        if (statusJson.contains("CurrenCoord"))
        {
            auto currentCoord = JsonUtils::safeGetString(statusJson, "CurrenCoord", "");
            // currentCoord is 0,0,0
            std::vector<std::string> coords;
            std::stringstream ss(currentCoord);
            std::string coord;
            while (std::getline(ss, coord, ','))
            {
                coords.push_back(coord);
            }

            if (coords.size() == 3)
            {
                // printerStatusData.printAxesStatus.x = std::stod(coords[0]);
                // printerStatusData.printAxesStatus.y = std::stod(coords[1]);
                // printerStatusData.printAxesStatus.z = std::stod(coords[2]);
                printerStatusData.printAxesStatus.position.clear();
                printerStatusData.printAxesStatus.position.push_back(std::stod(coords[0]));
                printerStatusData.printAxesStatus.position.push_back(std::stod(coords[1]));
                printerStatusData.printAxesStatus.position.push_back(std::stod(coords[2]));
                printerStatusData.printAxesStatus.position.push_back(0.0); // Add 4th axis as 0
            }
        }

        if (statusJson.contains("CurrentFanSpeed") && statusJson["CurrentFanSpeed"].is_object())
        {
            auto currentFanSpeed = statusJson["CurrentFanSpeed"];
            printerStatusData.fanStatus["model"].speed = JsonUtils::safeGetInt(currentFanSpeed, "ModelFan", 0);
            printerStatusData.fanStatus["aux"].speed = JsonUtils::safeGetInt(currentFanSpeed, "AuxiliaryFan", 0);
            printerStatusData.fanStatus["chassis"].speed = JsonUtils::safeGetInt(currentFanSpeed, "BoxFan", 0);
        }
        if (statusJson.contains("LightStatus") && statusJson["LightStatus"].is_object())
        {
            auto lightStatus = statusJson["LightStatus"];
            printerStatusData.lightStatus["main"].brightness = JsonUtils::safeGetInt(lightStatus, "MainLight", 0); // Main light brightness
            printerStatusData.lightStatus["main"].connected = true;
        }

        printerStatusData.temperatureStatus["heatedBed"].current = JsonUtils::safeGetDouble(statusJson, "TempOfHotbed", 0.0f);
        printerStatusData.temperatureStatus["heatedBed"].target = JsonUtils::safeGetDouble(statusJson, "TempTargetHotbed", 0.0f);

        printerStatusData.temperatureStatus["extruder"].current = JsonUtils::safeGetDouble(statusJson, "TempOfNozzle", 0.0f);
        printerStatusData.temperatureStatus["extruder"].target = JsonUtils::safeGetDouble(statusJson, "TempTargetNozzle", 0.0f);

        printerStatusData.temperatureStatus["chamber"].current = JsonUtils::safeGetDouble(statusJson, "TempOfBox", 0.0f);
        printerStatusData.temperatureStatus["chamber"].target = JsonUtils::safeGetDouble(statusJson, "TempTargetBox", 0.0f);

        printerStatusData.storageStatus["local"].connected = true; // Local storage is always available
        printerStatusData.printerStatus.progress = printerStatusData.printStatus.progress;
        printerStatusData.printerStatus.supportProgress = false;
        return printerStatusData;
    }
    PrinterAttributesData ElegooFdmCCMessageAdapter::handlePrinterAttributes(const nlohmann::json &printerJson) const
    {
        PrinterAttributesData attributesEvent(printerInfo_);
        if (printerJson.contains("Attributes") == false || printerJson["Attributes"].is_object() == false)
        {
            ELEGOO_LOG_ERROR("Invalid printer attributes format: {}", printerJson.dump());
            return attributesEvent;
        }
        auto attributesJson = printerJson["Attributes"];

        auto machineName = JsonUtils::safeGetString(attributesJson, "MachineName", "Unknown Machine");
        auto mainboardId = JsonUtils::safeGetString(attributesJson, "MainboardID", "");
        // auto ipAddress = JsonUtils::safeGetString(attributesJson, "MainboardIP", "");
        attributesEvent.mainboardId = mainboardId; // This id will be used to control the printer, do not change it here, otherwise the printer cannot be controlled
        // attributesEvent.host = ipAddress;
        // attributesEvent.name = JsonUtils::safeGetString(attributesJson, "Name", "");
        attributesEvent.model = machineName;
        attributesEvent.brand = "Elegoo";
        attributesEvent.firmwareVersion = JsonUtils::safeGetString(attributesJson, "FirmwareVersion", "");
        // attributesEvent.attributes.serialNumber = JsonUtils::safeGetString(attributesJson, "SerialNumber", "");
        attributesEvent.capabilities.cameraCapabilities.supportsCamera = true;
        attributesEvent.capabilities.cameraCapabilities.supportsTimeLapse = true;
        attributesEvent.capabilities.fanComponents.push_back({"model", true, 0, 100});
        attributesEvent.capabilities.fanComponents.push_back({"aux", true, 0, 100});
        attributesEvent.capabilities.fanComponents.push_back({"chamber", true, 0, 100});

        attributesEvent.capabilities.lightComponents.push_back({"main", "singleColor", 0, 1});
        attributesEvent.capabilities.temperatureComponents.push_back({"heatedBed", true, true, 0, 100});
        attributesEvent.capabilities.temperatureComponents.push_back({"extruder", true, true, 0, 300});
        attributesEvent.capabilities.temperatureComponents.push_back({"chamber", false, true, 0, 100});

        attributesEvent.capabilities.storageComponents.push_back({"local", true});
        attributesEvent.capabilities.storageComponents.push_back({"sdCard", false});
        attributesEvent.capabilities.storageComponents.push_back({"udisk", false});

        attributesEvent.capabilities.systemCapabilities.canSetPrinterName = true;
        

        attributesEvent.capabilities.printCapabilities.supportsAutoBedLeveling = true;
        attributesEvent.capabilities.printCapabilities.supportsTimeLapse = true;
        attributesEvent.capabilities.printCapabilities.supportsHeatedBedSwitching = true;

        // Parse firmware version to determine if it's greater than V1.1.X
        bool supportsMultiFilament = false;
        std::string fwVersion = attributesEvent.firmwareVersion;
        // Remove 'v' or 'V' prefix
        if (!fwVersion.empty() && (fwVersion[0] == 'v' || fwVersion[0] == 'V')) {
            fwVersion = fwVersion.substr(1);
        }
        // Parse version number (format: X.Y.Z)
        int majorVer = 0, minorVer = 0;
        if (sscanf(fwVersion.c_str(), "%d.%d", &majorVer, &minorVer) >= 2) {
            // Determine if greater than 1.1.x
            if (majorVer > 1 || (majorVer == 1 && minorVer > 1)) {
                supportsMultiFilament = true;
            }
        }

        attributesEvent.capabilities.printCapabilities.supportsFilamentMapping = supportsMultiFilament;
        attributesEvent.capabilities.systemCapabilities.supportsMultiFilament = supportsMultiFilament;

        printerInfo_.mainboardId = mainboardId; // Update mainboard ID
        printerInfo_.firmwareVersion = attributesEvent.firmwareVersion;
        return attributesEvent;
    }
    std::optional<CanvasStatus> ElegooFdmCCMessageAdapter::handleCanvasStatus(const nlohmann::json &result) const
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

    int ElegooFdmCCMessageAdapter::mapCommandType(MethodType command)
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

    MethodType ElegooFdmCCMessageAdapter::mapPrinterCommand(int printerCommand)
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
    const std::vector<std::pair<MethodType, int>> ElegooFdmCCMessageAdapter::COMMAND_MAPPING_TABLE = {
        {MethodType::GET_PRINTER_ATTRIBUTES, 1}, // Version negotiation request
        {MethodType::GET_PRINTER_STATUS, 0},     // Get status
        {MethodType::START_PRINT, 128},          // Start print
        {MethodType::PAUSE_PRINT, 129},          // Pause print
        {MethodType::STOP_PRINT, 130},           // Stop print
        {MethodType::RESUME_PRINT, 131},         // Resume print
        {MethodType::UPDATE_PRINTER_NAME, -1},   // Update printer name
        // {MethodType::HOME_AXES, 402},         // Home axes
        // {MethodType::MOVE_AXES, 401},         // Move axes
        // {MethodType::SET_TEMPERATURE, 403},   // Set temperature
        // {MethodType::SET_LIGHT, 403},         // Set light brightness
        // {MethodType::SET_FAN_SPEED, 403},     // Set fan speed
        // {MethodType::SET_PRINT_SPEED, 403},   // Set print speed mode
        // {MethodType::PRINT_TASK_LIST, 320},   // Get print task list
        // {MethodType::PRINT_TASK_DETAIL, 321}, // Get print task detail
        // {MethodType::DELETE_PRINT_TASK, 322}, // Delete print task
        // {MethodType::GET_FILE_LIST, 258},     // Get file list
        // {MethodType::GET_FILE_DETAIL, 258},   // Get file detail
        // {MethodType::DELETE_FILE, 259},       // Delete file
        // {MethodType::GET_DISK_INFO, 1048},           // Get disk capacity
        // {MethodType::VIDEO_STREAM, 386}, // Get camera video stream
        // {MethodType::SET_PRINTER_NAME, 1043},        // Set machine name
        // {MethodType::LOAD_FILAMENT, 1024},           // Load filament
        // {MethodType::UNLOAD_FILAMENT, 1025},         // Load/unload filament
        // {MethodType::EXPORT_TIMELAPSE_VIDEO, 323}, // Export timelapse video
        {MethodType::GET_CANVAS_STATUS, 324}};

    nlohmann::json ElegooFdmCCMessageAdapter::createStandardBody() const
    {
        // {
        // "Id": "xxx",  // Machine brand identifier, 32-bit UUID
        // "Data": {
        //     "Cmd": 0,  // Request command
        //     "Data": {},
        //     "RequestID": "000000000001d354",  // Request ID
        //     "MainboardID": "ffffffff",  // Mainboard ID
        //     "TimeStamp": 1687069655,  // Timestamp
        //     "From": 0  // Command source identifier
        // },
        // "Topic": "sdcp/request/${MainboardID}"  // Message type
        nlohmann::json body;
        body["Id"] = printerInfo_.mainboardId; // Printer ID
        body["Topic"] = "";
        body["Data"] = nlohmann::json::object();
        return body;
    }
}
