#pragma once
#include "printer.h"
namespace elink
{
    struct RtcTokenData
    {
        std::string userId;         // user ID
        std::string rtcToken;       // RTC token
        int64_t rtcTokenExpireTime; // RTC token expire time (timestamp in seconds)
    };

    using GetRtcTokenResult = BizResult<RtcTokenData>;

    struct OnlineStatusData
    {
        bool isOnline; // Online status
    };
    
    struct SetRegionParams : public BaseParams
    {
        std::string region; // Region identifier, e.g., "us", "eu", "asia"
        std::string baseUrl; // Base URL for the specified region, optional, if empty, use default URL
        std::string caCertPath; // CA certificate path for SSL/TLS verification

        bool operator==(const SetRegionParams &other) const
        {
            return region == other.region &&
                   baseUrl == other.baseUrl &&
                   caCertPath == other.caCertPath;
        }
    };

    struct UserInfo
    {
        std::string userId;
        std::string phone;
        std::string email;
        std::string nickName;
        std::string avatar;
    };
    using GetUserInfoParams = BaseParams;
    using GetUserInfoResult = BizResult<UserInfo>;
    struct HttpCredential
    {
        std::string userId;
        std::string accessToken;
        std::string refreshToken;
        int64_t accessTokenExpireTime;
        int64_t refreshTokenExpireTime;
    };

    /**
     * Agora Credential Information
     */
    struct AgoraCredential
    {
        std::string userId;         // User ID
        std::string rtcUserId;      // RTC User ID (string type user ID)
        std::string rtmUserId;      // RTM User ID (string type user ID)
        std::string rtcToken;       // RTC Token
        std::string rtmToken;       // RTM Token
        int64_t rtcTokenExpireTime; // RTC token expire time (timestamp in seconds)
        int64_t rtmTokenExpireTime; // RTM token expire time (timestamp in seconds)
    };

    struct MqttCredential
    {
        std::string host;
        std::string mqttClientId;
        std::string mqttPassword;
        std::string mqttUserName;
        std::string publishAuthorization;
        std::string subscribeAuthorization;
    };

    struct BindPrinterParams
    {
        std::string name;         // Printer name, e.g., "Elegoo Neptune 3", user customizable
        std::string model;        // Printer model
        std::string serialNumber; // Printer serial number
        std::string authMode;     // Authentication mode, "pinCode"
        std::string pinCode;      // PIN code
    };

    struct BindPrinterData
    {
        bool bindResult;
        PrinterInfo printerInfo; // Printer information
    };

    using BindPrinterResult = BizResult<BindPrinterData>;

    struct CancelBindPrinterParams
    {
        std::string serialNumber; // Printer serial number
    };

    struct UnbindPrinterParams
    {
        std::string serialNumber;
    };

    using UnbindPrinterResult = VoidResult;

    struct SendRtmMessageParams : public PrinterBaseParams
    {
        std::string message; //  Message content
    };

    struct RtmMessageData
    {
        std::string printerId; // Printer ID
        std::string message;   // Message content
    };

    struct PrinterEventRawData
    {
        std::string printerId; // Printer ID
        std::string rawData;   // Raw data
    };

    struct PrintTaskDetail
    {
        std::string taskId;    // Task ID
        std::string thumbnail; // Thumbnail URL
        std::string taskName;  // Task name
        int64_t beginTime = 0; // Start time (timestamp/seconds)
        int64_t endTime = 0;   // End time (timestamp/seconds)
        /** Task status
            0: Other status
            1: Completed
            2: Exception status
            3: Stopped
         */
        int taskStatus = 0; // Task status
        // /**
        //  * Time-lapse video status
        //  * 0: No time-lapse video file
        //  * 1: Time-lapse video file exists
        //  * 2: Time-lapse video file deleted
        //  * 3: Time-lapse video generating
        //  * 4: Time-lapse video generation failed
        //  */
        // int timeLapseVideoStatus = 0;   // Time-lapse video status
        // std::string timeLapseVideoUrl;  // Time-lapse video URL
        // int timeLapseVideoSize = 0;     // Time-lapse video size (FDM)
        // int timeLapseVideoDuration = 0; // Time-lapse video duration (seconds) (FDM)
    };

    /**
     * Get print task list request parameters
     */
    struct PrintTaskListParams : public PrinterBaseParams
    {
        int pageNumber = 1; // Page number, starting from 1
        int pageSize = 50;  // Page size, maximum value is 100
    };

    /**
     * Print task list data
     */
    struct PrintTaskListData
    {
        // Print task ID list
        std::vector<PrintTaskDetail> taskList; // Print task detail list
        int totalTasks = 0;                    // Total number of print tasks
    };

    using PrintTaskListResult = BizResult<PrintTaskListData>;

    /**
     * Batch delete historical print tasks request parameters
     */
    struct DeletePrintTasksParams : public PrinterBaseParams
    {
        std::vector<std::string> taskIds; // Print task ID list
    };

    using DeletePrintTasksResult = VoidResult;
    struct FilamentColorMapping
    {
        int t;             // index of the tray in the multi-color printing GCode T command
        std::string color; // Filament color in hex format, e.g., "#BCBCBC"
        std::string type;  // Filament type, e.g., "PLA", "ABS"
    };
    /**
     * File detail
     */
    struct FileDetail
    {
        std::string fileName;                           // File name
        int64_t printTime = 0;                          // Print time (seconds)
        int layer = 0;                                  // Total layers
        double layerHeight = 0;                         // Layer height (millimeters)
        std::string thumbnail;                          // Thumbnail URL
        int64_t size = 0;                               // File size (bytes)
        int64_t createTime = 0;                         // File creation time (timestamp/seconds)
        double totalFilamentUsed = 0.0f;                // Estimated total filament consumption weight (grams)
        double totalFilamentUsedLength = 0.0f;          // Estimated total filament consumption length (millimeters)
        int totalPrintTimes = 0;                        // Total print times
        int64_t lastPrintTime = 0;                      // Last print time (timestamp/seconds)
        std::vector<FilamentColorMapping> colorMapping; // Filament color mapping for multi-color printing
    };
    /**
     * Get file list request parameters
     */
    struct GetFileListParams : public PrinterBaseParams
    {
        int pageNumber = 1; // Page number, starting from 1
        int pageSize = 50;  // Page size, maximum value is 100
    };
    /**
     * File list data
     */
    struct GetFileListData
    {
        /**
         * File list
         */
        std::vector<FileDetail> fileList; // File list
        int totalFiles = 0;               // Total number of files
    };

    using GetFileListResult = BizResult<GetFileListData>;

    struct GetFileDetailParams : public PrinterBaseParams
    {
        std::string fileName; // File name
    };
    using GetFileDetailResult = BizResult<FileDetail>;

    struct SetPrinterDownloadFileParams : public PrinterBaseParams
    {
        std::string fileUrl;  // File URL
        std::string fileName; // File name
        std::string taskId;
        std::string md5;
    };
    using SetPrinterDownloadFileResult = VoidResult;

    struct CancelPrinterDownloadFileParams : public PrinterBaseParams
    {
        std::string taskId;
    };
    using CancelPrinterDownloadFileResult = VoidResult;

    struct UpdatePrinterNameParams : public PrinterBaseParams
    {
        std::string printerName;
    };
    using UpdatePrinterNameResult = VoidResult;
} // namespace elink
