#pragma once
#include "message.h"
#include "../cloud.h"
#include "../printer.h"
#include "../common.h"
#include <variant>
#ifdef NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT
#undef NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT
#endif

#define NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(Type, ...)                                                                                                \
    template <typename BasicJsonType, nlohmann::detail::enable_if_t<nlohmann::detail::is_basic_json<BasicJsonType>::value, int> = 0>                              \
    static void to_json(BasicJsonType &nlohmann_json_j, const Type &nlohmann_json_t) { NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, __VA_ARGS__)) } \
    template <typename BasicJsonType, nlohmann::detail::enable_if_t<nlohmann::detail::is_basic_json<BasicJsonType>::value, int> = 0>                              \
    static void from_json(const BasicJsonType &nlohmann_json_j, Type &nlohmann_json_t)                                                                            \
    {                                                                                                                                                             \
        const Type nlohmann_json_default_obj{};                                                                                                                   \
        NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM_WITH_DEFAULT, __VA_ARGS__))                                                                   \
    }

namespace elink
{

    inline void to_json(nlohmann::json &j, const std::monostate &obj)
    {
        // Serialization logic
    }
    inline void from_json(const nlohmann::json &j, std::monostate &obj)
    {
        // Deserialization logic
    }
    inline void to_json(nlohmann::json &j, const BaseParams &obj)
    {
        // Serialization logic
        j = nlohmann::json::object();
    }
    inline void from_json(const nlohmann::json &j, BaseParams &obj)
    {
        // Deserialization logic
    }
    inline void to_json(nlohmann::json &j, const BaseEventData &obj)
    {
        // Serialization logic
        j = nlohmann::json::object();
    }
    inline void from_json(const nlohmann::json &j, BaseEventData &obj)
    {
        // Deserialization logic
    }

    inline void to_json(nlohmann::json &j, const BaseResult &obj)
    {
        // Serialization logic
        j = nlohmann::json::object();
    }
    inline void from_json(const nlohmann::json &j, BaseResult &obj)
    {
        // Deserialization logic
    }
    
    template <typename T>
    inline void to_json(nlohmann::json &j, const BizResult<T> &result)
    {
        j = nlohmann::json{
            {"code", static_cast<int>(result.code)},
            {"message", result.message}};

        if (result.data.has_value())
        {
            if constexpr (std::is_same_v<T, nlohmann::json>)
            {
                j["data"] = *result.data;
            }
            else
            {
                j["data"] = result.data.value();
            }
        }
    };

    // GetPrinterListParams is empty, so we define custom serialization
    inline void to_json(nlohmann::json &j, const GetPrinterListParams &p)
    {
        j = nlohmann::json::object();
    }

    inline void from_json(const nlohmann::json &j, GetPrinterListParams &p)
    {
        // Empty struct, nothing to deserialize
    }

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GetPrinterListData,
                                                    printers)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PrinterDiscoveryParams,
                                                    timeoutMs, broadcastInterval, enableAutoRetry, preferredListenPorts)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PrinterDiscoveryData,
                                                    printers)

#if 1 // Printer-related structs
    // Basic struct serialization definitions
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PrinterBaseParams,
                                                    printerId)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PrinterInfo,
                                                    printerId, printerType, brand, manufacturer, name, model,
                                                    firmwareVersion, serialNumber, mainboardId, host, webUrl,
                                                    authMode, networkMode, extraInfo)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StorageComponent,
                                                    name, removable)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FanComponent,
                                                    name, controllable, minSpeed, maxSpeed, supportsRpmReading)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TemperatureComponent,
                                                    name, controllable, supportsTemperatureReading, minTemperature, maxTemperature)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LightComponent,
                                                    name, type, minBrightness, maxBrightness)

    // Nested structs
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CameraCapabilities,
                                                    supportsCamera, supportsTimeLapse)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SystemCapabilities,
                                                    canSetPrinterName, canGetDiskInfo, supportsMultiFilament)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PrintCapabilities,
                                                    supportsAutoBedLeveling, supportsTimeLapse, supportsHeatedBedSwitching, supportsFilamentMapping, supportsAutoRefill)

    inline void to_json(nlohmann::json &j, const PrinterCapabilities &obj)
    {
        j["storageComponents"] = obj.storageComponents;
        j["fanComponents"] = obj.fanComponents;
        j["temperatureComponents"] = obj.temperatureComponents;
        j["lightComponents"] = obj.lightComponents;
        j["cameraCapabilities"] = obj.cameraCapabilities;
        j["systemCapabilities"] = obj.systemCapabilities;
        j["printCapabilities"] = obj.printCapabilities;
    }
    inline void from_json(const nlohmann::json &j, PrinterCapabilities &obj)
    {
        obj.storageComponents = j.value("storageComponents", std::vector<StorageComponent>());
        obj.fanComponents = j.value("fanComponents", std::vector<FanComponent>());
        obj.temperatureComponents = j.value("temperatureComponents", std::vector<TemperatureComponent>());
        obj.lightComponents = j.value("lightComponents", std::vector<LightComponent>());
        obj.cameraCapabilities = j.value("cameraCapabilities", CameraCapabilities());
        obj.systemCapabilities = j.value("systemCapabilities", SystemCapabilities());
        obj.printCapabilities = j.value("printCapabilities", PrintCapabilities());
    }

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PrinterAttributes,
                                                    printerId, printerType, brand, manufacturer, name, model,
                                                    firmwareVersion, serialNumber, mainboardId, host, webUrl,
                                                    authMode, extraInfo, capabilities)

    // Status structs
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PrinterStatus,
                                                    state, subState, exceptionCodes, supportProgress, progress)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TemperatureStatus,
                                                    current, target, highest, lowest)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FanStatus,
                                                    speed, rpm)

    // NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PrintAxesStatus,
    //                                                 x, y, z, xHomed, yHomed, zHomed)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PrintAxesStatus,
                                                    position)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PrintStatus,
                                                    taskId, fileName, totalTime, currentTime, estimatedTime,
                                                    totalLayer, currentLayer, progress, printSpeedMode)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(LightStatus,
                                                    connected, brightness, color)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StorageStatus,
                                                    connected)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SlotMapItem,
                                                    t, trayId, canvasId)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(TrayInfo,
                                                    trayId, brand, filamentType, filamentName,
                                                    filamentCode, filamentColor, minNozzleTemp, maxNozzleTemp, status)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CanvasInfo, name, model,
                                                    canvasId, connected, trays)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CanvasStatus,
                                                    activeCanvasId, activeTrayId, autoRefill, canvases)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ExternalDeviceStatus,
                                                    usbConnected, sdCardConnected, cameraConnected, canvasConnected)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PrinterStatusData,
                                                    printerId, printerStatus, printStatus, temperatureStatus, fanStatus,
                                                    printAxesStatus, lightStatus, storageStatus, canvasStatus, externalDeviceStatus)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(StartPrintParams,
                                                    printerId, storageLocation, fileName, autoBedLeveling, heatedBedType, enableTimeLapse, bedLevelForce, slotMap)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(MoveAxisParams,
                                                    printerId, axes, distance)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(HomeAxisParams,
                                                    printerId, axes)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SetTemperatureParams,
                                                    printerId, temperatures)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SetFanSpeedParams,
                                                    printerId, fans)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SetPrintSpeedParams,
                                                    printerId, speedMode)

    // File transfer structs
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FileUploadParams,
                                                    printerId, storageLocation, localFilePath, fileName, overwriteExisting)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FileUploadProgressData, printerId, totalBytes, uploadedBytes, percentage)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FileDownloadParams,
                                                    printerId, storageLocation, remoteFilePath, localFilePath, overwriteExisting)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FileDownloadProgressData, totalBytes, downloadedBytes, percentage)

    // CancelFileUploadParams
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CancelFileUploadParams,
                                                    printerId)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GetDownloadUrlData,
                                                    downloadUrl)

    // Connection-related structs
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ConnectPrinterParams,
                                                    printerId, printerType, brand, name, model, serialNumber,
                                                    host, webUrl, authMode, username, password, token, accessCode, pinCode, checkConnection, autoReconnect, connectionTimeout, networkMode, extraParams)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ConnectPrinterData,
                                                    printerInfo, isConnected)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ConnectionStatusData,
                                                    printerId, status)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SetAutoRefillParams,
                                                    printerId, enable)

#endif

#if 1 // Cloud-related structs

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(OnlineStatusData, isOnline)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SetRegionParams, region, baseUrl, caCertPath)
    // GetUserInfoParams
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UserInfo,
                                                    userId, phone, email, nickName, avatar)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
        HttpCredential, userId, accessToken, refreshToken, accessTokenExpireTime, refreshTokenExpireTime)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RtcTokenData, userId, rtcToken, rtcTokenExpireTime)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PrintTaskDetail, taskId, thumbnail, taskName, beginTime, endTime, taskStatus)

    // Task-related structs
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PrintTaskListParams, printerId, pageNumber, pageSize)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PrintTaskListData, taskList, totalTasks)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(DeletePrintTasksParams, printerId, taskIds)

    // File-related structs
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FilamentColorMapping,
                                                    t, color, type)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(FileDetail,
                                                    fileName, printTime, layer, layerHeight, thumbnail, size, createTime,
                                                    totalFilamentUsed, totalFilamentUsedLength, totalPrintTimes, lastPrintTime, colorMapping)
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GetFileListParams, printerId, pageNumber, pageSize)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GetFileListData, fileList, totalFiles)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(GetFileDetailParams,
                                                    printerId, fileName)

    // RtmMessageData
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(RtmMessageData, printerId, message)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(PrinterEventRawData, printerId, rawData)
    // SendRtmMessageParams
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SendRtmMessageParams, printerId, message)

    // BindPrinterParams
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BindPrinterParams, name, authMode, model, serialNumber, pinCode)

    // BindPrinterData
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(BindPrinterData, bindResult, printerInfo)

    // UnbindPrinterParams
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UnbindPrinterParams, serialNumber)
    // CancelBindPrinterParams
    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CancelBindPrinterParams, serialNumber)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(SetPrinterDownloadFileParams,
                                                    printerId, fileUrl, fileName, taskId, md5)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(CancelPrinterDownloadFileParams, printerId, taskId)

    NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(UpdatePrinterNameParams, printerId, printerName)
#endif
} // namespace elink
