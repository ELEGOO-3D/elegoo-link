#pragma once
#include "biz.h"
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <cstdint>
namespace elink
{

    struct PrinterBaseParams
    {
        std::string printerId = ""; // Printer ID
        PrinterBaseParams(const std::string &printerId = "")
            : printerId(printerId) {}
    };

    using PrinterEventData = PrinterBaseParams;

    /**
     * Printer type enumeration
     */
    enum class PrinterType
    {
        UNKNOWN = -1, // Unknown printer
        ELEGOO_FDM_KLIPPER = 0,
        ELEGOO_FDM_CC = 1,
        ELEGOO_FDM_CC2 = 2,

        // Generic
        GENERIC_FDM_KLIPPER = 100, // Generic FDM Klipper printer
    };

    // Connection status
    enum class ConnectionStatus
    {
        DISCONNECTED,
        CONNECTED,
    };

    enum class NetworkMode
    {
        LAN = 0,
        CLOUD = 1,
    };

    static std::string connectionStatusToString(ConnectionStatus status)
    {
        switch (status)
        {
        case ConnectionStatus::DISCONNECTED:
            return "Disconnected";
        case ConnectionStatus::CONNECTED:
            return "Connected";
        default:
            return "Unknown";
        }
    };

    static std::string printerTypeToString(PrinterType type)
    {
        switch (type)
        {
        case PrinterType::UNKNOWN:
            return "Unknown";
        case PrinterType::ELEGOO_FDM_KLIPPER:
            return "ELEGOO_FDM_KLIPPER";
        case PrinterType::ELEGOO_FDM_CC:
            return "ELEGOO_FDM_CC";
        case PrinterType::ELEGOO_FDM_CC2:
            return "ELEGOO_FDM_CC2";
        case PrinterType::GENERIC_FDM_KLIPPER:
            return "GENERIC_FDM_KLIPPER";
        default:
            return "Unknown";
        }
    };
    
    static PrinterType printerModelToPrinterType(const std::string &model)
    {
        if (model.find("Centauri Carbon 2") != std::string::npos || model.find("Centauri 2") != std::string::npos)
        {
            return PrinterType::ELEGOO_FDM_CC2;
        }
        else
        {
            return PrinterType::UNKNOWN;
        }
    }
    // Printer information structure
    struct PrinterInfo
    {
        std::string printerId = "";                     // Printer ID
        PrinterType printerType = PrinterType::UNKNOWN; // Printer type
        std::string brand = "";                         // Printer brand
        std::string manufacturer = "";                  // Manufacturer
        std::string name = "";                          // Printer name, e.g., "Elegoo Neptune 3", user customizable
        std::string model = "";                         // Printer model, e.g., "Neptune 3 Pro"
        std::string firmwareVersion = "";               // Firmware version
        std::string mainboardId = "";                   // Mainboard ID, unique identifier for the printer
        std::string serialNumber = "";                  // Printer serial number
        std::string host = "";                          // Host name or IP address
        // Web URL, if available, for accessing printer's web interface, e.g., http://192.168.1.100, empty if not available
        std::string webUrl = ""; // Web URL
        /**
         * Authorization mode, such as `token` or `basic`, empty when no authorization is required
         * `token` authorization mode requires providing a token when connecting
         * `basic` authorization mode requires providing username and password when connecting
         * `accessCode` authorization mode requires providing an access code when connecting
         * `pinCode` authorization mode requires providing a PIN code when connecting
         * Other authorization modes can be added as needed
         */
        std::string authMode = ""; // Authorization mode
        NetworkMode networkMode = NetworkMode::LAN;       // 0: LAN, 1: WAN
        std::map<std::string, std::string> extraInfo;
    };

    // Storage printer information
    struct StorageComponent
    {
        std::string name = "";  // Storage printer name (e.g., "local", "udisk", "sdcard")
        bool removable = false; // Whether it's a removable printer
    };

    // Fan information
    struct FanComponent
    {
        std::string name = "";           // Fan name (e.g., "model", "heatsink", "controller", "chamber", "aux")
        bool controllable = true;        // Whether it's controllable
        int minSpeed = 0;                // Minimum speed (0-100)
        int maxSpeed = 100;              // Maximum speed (0-100)
        bool supportsRpmReading = false; // Whether it supports RPM reading
    };

    // Temperature control component information
    struct TemperatureComponent
    {
        std::string name = "";                  // Component name (e.g., "heatedBed", "extruder", "chamber")
        bool controllable = true;               // Whether temperature is controllable
        bool supportsTemperatureReading = true; // Whether it supports temperature reading
        double minTemperature = 0.0f;           // Minimum temperature (Celsius)
        double maxTemperature = 100.0f;         // Maximum temperature (Celsius)
    };

    struct LightComponent
    {
        std::string name = ""; // Light name, e.g., "main"
        std::string type = ""; // Light type, e.g., "rgb", "singleColor"
        // Brightness range, for RGB lights, brightness range is usually 0-255, for singleColor lights, can use 0-1 range directly, 0 means off, 1 means full brightness
        int minBrightness = 0;   // Minimum brightness (0-255)
        int maxBrightness = 255; // Maximum brightness (0-255)
    };

    struct CameraCapabilities
    {
        bool supportsCamera = false;    // Whether it supports camera
        bool supportsTimeLapse = false; // Supports time-lapse photography
    };

    // System capabilities
    struct SystemCapabilities
    {
        bool canSetPrinterName = false;     // Supports setting machine name
        bool canGetDiskInfo = false;        // Supports getting disk information
        bool supportsMultiFilament = false; // Supports multi-filament printing
    };

    struct PrintCapabilities
    {
        bool supportsAutoBedLeveling = false;    // Supports auto bed leveling
        bool supportsTimeLapse = false;          // Supports time-lapse printing
        bool supportsHeatedBedSwitching = false; // Supports heated bed switching
        bool supportsFilamentMapping = false;    // Supports filament mapping
        bool supportsAutoRefill = false;         // Supports auto refill
    };

    // Detailed printer capabilities
    struct PrinterCapabilities
    {
        // Storage printer list
        std::vector<StorageComponent> storageComponents;

        // Fan list
        std::vector<FanComponent> fanComponents;

        // Temperature control component list
        std::vector<TemperatureComponent> temperatureComponents;

        // Light information list
        std::vector<LightComponent> lightComponents;

        // Camera related
        CameraCapabilities cameraCapabilities;

        // System capabilities
        SystemCapabilities systemCapabilities;

        PrintCapabilities printCapabilities;
    };

    // Printer attributes (optimized version)
    struct PrinterAttributes : public PrinterInfo
    {
        PrinterCapabilities capabilities; // Detailed capability description

        PrinterAttributes() = default;
        PrinterAttributes(const PrinterInfo &devInfo) : PrinterInfo(devInfo)
        {
        }
    };

    // Printer main status
    enum class PrinterState
    {
        OFFLINE = -1,            // Offline status
        IDLE = 0,                // Idle status
        PRINTING = 1,            // Printing
        FILAMENT_OPERATING = 2,  // Filament operating
        AUTO_LEVELING = 3,       // Auto-leveling
        PID_CALIBRATING = 4,     // PID calibrating
        RESONANCE_TESTING = 5,   // Resonance testing
        SELF_CHECKING = 6,       // Self-checking
        UPDATING = 7,            // Updating
        HOMING = 8,              // Homing
        FILE_TRANSFERRING = 9,   // File transferring
        FILE_COPYING = 10,       // File copying
        PREHEATING = 11,         // Preheating
        EXTRUDER_OPERATING = 12, // Extruder operating
        VIDEO_COMPOSING = 13,    // Video composing

        EMERGENCY_STOP = 14, // Emergency stop
        POWER_LOSS_RECOVERY = 15,    // Power loss recover


        INITIALIZING = 97,        // Initializing
        BUSY = 98,      // Printer busy status
        EXCEPTION = 99, // Printer exception status
        UNKNOWN = 100   // Unknown status
    };

    // Printer sub status
    enum class PrinterSubState
    {
        NONE = 0,    // No sub status
        UNKNOWN = 1, // Unknown status

        P_PRINTING = 101,           // Printing
        P_PRINTING_COMPLETED = 102, // Printing completed
        P_PAUSING = 103,            // Pausing
        P_PAUSED = 104,             // Paused
        P_RESUMING = 105,           // Resuming
        P_RESUMING_COMPLETED = 106, // Resuming completed
        P_STOPPING = 107,           // Stopping
        P_STOPPED = 108,            // Stopped

        P_PREHEATING = 120,            // Preheating
        P_EXTRUDER_PREHEATING = 121,   // Extruder preheating
        P_HEATED_BED_PREHEATING = 122, // Heated bed preheating
        P_HOMING = 123,                // Homing
        P_AUTO_LEVELING = 124,         // Auto-leveling
        P_LOADING_FILAMENT = 125,      // Loading filament
        P_UNLOADING_FILAMENT = 126,    // Unloading filament

        FO_FILAMENT_LOADING = 201,       // Filament loading in progress
        FO_FILAMENT_LOADING_COMPLETED,   // Filament loading completed
        FO_FILAMENT_UNLOADING,           // Filament unloading in progress
        FO_FILAMENT_UNLOADING_COMPLETED, // Filament unloading completed

        AL_AUTO_LEVELING = 301,     // Auto-leveling in progress
        AL_AUTO_LEVELING_COMPLETED, // Auto-leveling completed (popup) status

        PC_PID_CALIBRATING = 401,     // PID calibration in progress
        PC_PID_CALIBRATING_COMPLETED, // PID calibration completed (popup) status
        PC_PID_CALIBRATING_FAILED,    // PID calibration error

        RT_RESONANCE_TEST = 501,     // Resonance test in progress
        RT_RESONANCE_TEST_COMPLETED, // Resonance test completed
        RT_RESONANCE_TEST_FAILED,    // Resonance test error

        SC_PID_CALIBRATING = 601,     // PID calibration in progress
        SC_PID_CALIBRATING_COMPLETED, // PID calibration completed
        SC_PID_CALIBRATING_FAILED,    // PID calibration error
        SC_RESONANCE_TEST = 610,      // Resonance test in progress
        SC_RESONANCE_TEST_COMPLETED,  // Resonance test completed
        SC_RESONANCE_TEST_FAILED,     // Resonance test error
        SC_AUTO_LEVELING = 620,       // Auto-leveling in progress
        SC_AUTO_LEVELING_COMPLETED,   // Auto-leveling completed
        SC_COMPLETED = 699,           // Self-check completed

        U_UPDATING = 701,     // Updating in progress
        U_UPDATING_COMPLETED, // Update completed
        U_UPDATING_FAILED,    // Update error

        CF_COPYING_FILE = 1001,    // File copying in progress
        CF_COPYING_FILE_COMPLETED, // File copying completed

        UF_UPLOADING_FILE = 901,     // File uploading in progress
        UF_UPLOADING_FILE_COMPLETED, // File uploading completed

        H_HOMING = 801,     // Homing in progress
        H_HOMING_COMPLETED, // Homing completed
        H_HOMING_FAILED,    // Homing error

        PRE_EXTRUDER_PREHEATING = 1101,      // Extruder preheating in progress
        PRE_EXTRUDER_PREHEATING_COMPLETED,   // Extruder preheating completed
        PRE_HEATED_BED_PREHEATING,           // Heated bed preheating in progress
        PRE_HEATED_BED_PREHEATING_COMPLETED, // Heated bed preheating completed

        EO_EXTRUDER_LOADING = 1201,      // Extruder loading (forward) in progress
        EO_EXTRUDER_LOADING_COMPLETED,   // Extruder loading (forward) completed
        EO_EXTRUDER_UNLOADING,           // Extruder unloading (reverse) in progress
        EO_EXTRUDER_UNLOADING_COMPLETED, // Extruder unloading (reverse) completed

    };


    struct PrinterStatus
    {
        PrinterState state = PrinterState::UNKNOWN;       // Printer status list
        PrinterSubState subState = PrinterSubState::NONE; // Sub status code
        std::vector<int> exceptionCodes;                  // Exception status code list
        bool supportProgress = false;                     // Whether it supports progress
        int progress = 0;                                 // Progress percentage (0-100)
    };

    // Temperature information
    struct TemperatureStatus
    {
        // std::string name;    // Temperature sensor name, e.g., "hotend", "bed", "ambient"
        double current = 0.0f; // Current temperature
        double target = 0.0f;  // Target temperature
        // Historical highest temperature
        double highest = 0.0f;
        // Historical lowest temperature
        double lowest = 0.0f;
    };

    struct FanStatus
    {
        // std::string name; // Fan name, e.g., "model", "heatsink", "controller", "chamber", "aux"
        int speed = 0; // Fan speed percentage (0-100)
        int rpm = 0;   // Fan speed (RPM)
    };

    struct PrintAxesStatus
    {
        std::vector<double> position; // List of axes, e.g., "x", "y", "z", "e"

        // double x = 0.0f; // X-axis position
        // double y = 0.0f; // Y-axis position
        // double z = 0.0f; // Z-axis position
        // float e = 0.0f; // Extruder position

        // bool xHomed = false; // Whether X-axis is homed
        // bool yHomed = false; // Whether Y-axis is homed
        // bool zHomed = false; // Whether Z-axis is homed
    };

    // Print status information
    struct PrintStatus
    {
        std::string taskId = "";   // Print task ID
        std::string fileName = ""; // File name
        // Total print duration
        int64_t totalTime = 0;      // Total print duration (seconds)
        int64_t currentTime = 0;    // Current print duration (seconds)
        int64_t estimatedTime = 0;  // Estimated remaining time (seconds)
        int totalLayer = 0;     // Total layers
        int currentLayer = 0;   // Current layer
        int progress = 0;       // Print progress
        int printSpeedMode = 0; // Print speed mode, Silent: 0, Balanced: 1, Sport: 2, Ludicrous: 3
    };

    struct LightStatus
    {
        // std::string name; // Light name, e.g., "main"
        // std::string type; // Light type, e.g., "rgb","singleColor"
        bool connected = false; // Whether connected
        int brightness = 0;     // Brightness percentage (0-255), for singleColor lights
        int color = 0;          // Color, #RGB format, e.g., "0xFF0000" for red
    };

    struct StorageStatus
    {
        // std::string name; // Storage printer name, e.g., "local", "udisk", "sdcard"
        bool connected = false; // Whether connected
    };

    // Canvas tray information
    // Tray information for canvas, used for printers with multiple trays
    struct TrayInfo
    {
        int trayId = 0;                 // Tray ID, 0 means no tray, 1-4 for trays
        std::string brand = "";         // Brand name
        std::string filamentType = "";  // Filament type, e.g., "PLA", "ABS"
        std::string filamentName = "";  // Filament name, e.g., "PLA+"
        std::string filamentCode = "";  // Filament code, e.g., "0x00000"
        std::string filamentColor = ""; // Filament color in hex format, e.g., "#BCBCBC"
        double minNozzleTemp = 0.0f;    // Minimum nozzle temperature (Celsius)
        double maxNozzleTemp = 0.0f;    // Maximum nozzle temperature (Celsius)
        int status = 0;                 // 0: Empty, 1: Pre-loaded, 2: Loaded
    };

    // Canvas information, used for printers with multiple canvases
    // Each canvas can have multiple trays, and each tray can have its own filament information
    struct CanvasInfo
    {
        int canvasId = 0; // Canvas ID, 0-4
        std::string name; // Canvas name, may be empty
        std::string model;// Canvas model, may be empty
        bool connected = false;      // Whether connected
        std::vector<TrayInfo> trays; // Tray information list
    };

    // Canvas status information
    // Used for printers with multiple canvases and trays
    struct CanvasStatus
    {
        int activeCanvasId = 0;           // Active canvas ID, 0-4
        int activeTrayId = 0;             // Active tray ID, 0 means no tray, 1-4 for trays
        bool autoRefill = false;          // Auto refill switch status
        std::vector<CanvasInfo> canvases; // Canvas information list
    };

    struct ExternalDeviceStatus
    {
        bool usbConnected = false;    // Whether USB disk is connected
        bool sdCardConnected = false; // Whether SD card is connected
        bool cameraConnected = false; // Whether camera is connected
        bool canvasConnected = false; // Whether canvas is connected
    };
    // Printer status information
    struct PrinterStatusData : public PrinterEventData
    {
        PrinterStatus printerStatus;
        PrintStatus printStatus; // Print status
        // Use maps to store temperatures and fans for better extensibility
        // Temperature and fan names can be keys, such as "heatedBed", "extruder", "chamber", etc.
        std::map<std::string, TemperatureStatus> temperatureStatus; // Temperature information
        // Fans can also be stored in a map for flexibility
        // Fan names can be keys, such as "model", "heatsink", "controller", "chassis", "aux", etc.
        std::map<std::string, FanStatus> fanStatus;
        PrintAxesStatus printAxesStatus; // Print axes status information
                                         // Light status information
        // Use map to store light status, key is light name, e.g., "main"
        std::map<std::string, LightStatus> lightStatus;
        // Storage printer status information, key is storage printer name, e.g., "local", "udisk", "sdcard"
        std::map<std::string, StorageStatus> storageStatus;

        CanvasStatus canvasStatus; // Canvas status information, optional,some printers may not support it

        ExternalDeviceStatus externalDeviceStatus; // External device status information, such as USB disk, SD card, camera, etc.
        PrinterStatusData(const std::string &printerId = "")
            : PrinterEventData(printerId) {}
    };

    using PrinterStatusParams = PrinterBaseParams;
    // struct PrinterStatusParams : public PrinterBaseParams
    // {
    //     PrinterStatusParams(const std::string &printerId)
    //         : PrinterBaseParams(printerId) {}
    // };

    using PrinterStatusResult = BizResult<PrinterStatusData>;

    using PrinterAttributesParams = PrinterBaseParams;

    /**
     * Printer attributes reply message
     */
    using PrinterAttributesData = PrinterAttributes;

    using PrinterAttributesResult = BizResult<PrinterAttributesData>;

    struct SlotMapItem
    {
        int t;        // index of the tray in the multi-color printing GCode T command
        int canvasId; // Canvas ID, supports multiple multi-color boxes
        int trayId;   // Tray ID, Canvas ID and Tray ID are used to specify which tray to use for which canvas
    };
    struct StartPrintParams : public PrinterBaseParams
    {
        // File location, local, USB, SD card
        // e.g., `local`, `udisk`, `sdcard`
        std::string storageLocation;
        // File name
        std::string fileName;
        // Enable auto bed leveling
        bool autoBedLeveling = false;
        // Heated bed type, 0 for textured high-temperature plate, 1 for smooth low-temperature plate
        int heatedBedType = 0;
        // Whether to enable time-lapse photography
        bool enableTimeLapse = false;
        /**
         * Force bed leveling, default is false
         * If the printer has existing bed leveling data, it will ignore this setting
         * If set to true, it will force bed leveling before printing
         */
        bool bedLevelForce = false;
        /**
         * Slot map, used to specify which tray to use for which canvas
         * Each item in the slot map specifies a tray for a canvas
         * e.g., `[{"t": 1, "canvasId": "0", "trayId": "1"}, {"t": 2, "canvasId": "1", "trayId": "2"}]`
         * t: Tray ID, canvasId: Canvas ID, trayId: Tray ID
         * @note If multi-canvas printing is not supported, slotMap can be set to empty
         */
        std::vector<SlotMapItem> slotMap;
    };

    using StartPrintResult = VoidResult;

    using StopPrintParams = PrinterBaseParams;
    using StopPrintResult = VoidResult;

    using PausePrintParams = PrinterBaseParams;
    using PausePrintResult = VoidResult;

    using ResumePrintParams = PrinterBaseParams;
    using ResumePrintResult = VoidResult;

    /**
     * Move axis request parameters
     */
    struct MoveAxisParams : public PrinterBaseParams
    {
        std::string axes;       // Axis name, e.g., "x", "y", "z"
        double distance = 0.0f; // Move distance, unit in millimeters
        // Whether to use relative coordinate system
        // bool relative = true; // Whether to use relative coordinate system
    };

    using MoveAxisResult = VoidResult;

    /**
     *  Home axis request parameters
     */
    struct HomeAxisParams : public PrinterBaseParams
    {
        std::string axes; // Axis name, e.g., "x", "y", "z", can be combined like "xy", "xyz", etc.
    };

    using HomeAxisResult = VoidResult;

    /**
     * Set temperature request parameters
     */
    struct SetTemperatureParams : public PrinterBaseParams
    {
        /**
         * Temperature settings, key is target name, value is temperature
         * e.g., {"heatedBed":60, "extruder":200}
         */
        std::map<std::string, double> temperatures;
    };
    using SetTemperatureResult = VoidResult;

    /**
     * Set fan speed request parameters
     */
    struct SetFanSpeedParams : public PrinterBaseParams
    {
        /**
         * Fan settings, key is fan name, value is speed percentage
         * e.g., {"model":100, "chassis":50, "aux":75}
         */
        std::map<std::string, int> fans; // Fan name and speed percentage
    };

    using SetFanSpeedResult = VoidResult;

    /**
     * Set print speed request parameters
     */
    struct SetPrintSpeedParams : public PrinterBaseParams
    {
        /**
         * Print speed mode
         * Silent	0
         * Balanced	1
         * Sport	2
         * Ludicrous	3
         */
        int speedMode = 0; // Print speed mode, 0-3
    };

    using SetPrintSpeedResult = VoidResult;

    /**
     * File transfer request parameters
     * Used as data parameter for ParamsMessage
     */
    struct FileUploadParams : public PrinterBaseParams
    {
        // Upload location, local, udisk, sdcard
        std::string storageLocation;    // Storage location
        std::string localFilePath;      // Local file path
        std::string fileName;           // File name
        bool overwriteExisting = false; // Whether to overwrite existing file
    };

    /**
     * Cancel file upload request parameters
     * Used to cancel an ongoing file upload task
     */
    struct CancelFileUploadParams : public PrinterBaseParams
    {
        // Only printerId is needed, inherited from PrinterBaseParams
    };

    /**
     * File transfer progress event parameters
     * Used as data parameter for StatusMessage
     */
    struct FileUploadProgressData : PrinterEventData
    {
        uint64_t totalBytes = 0;    // Total file size
        uint64_t uploadedBytes = 0; // Uploaded bytes
        int percentage = 0;         // Transfer percentage (0-100)
    };


    using FileUploadResult = VoidResult;

    /**
     * File download request parameters
     */
    struct FileDownloadParams : public PrinterBaseParams
    {
        std::string storageLocation;    // Storage location
        std::string remoteFilePath;     // Remote file path
        std::string localFilePath;      // Local save path
        bool overwriteExisting = false; // Whether to overwrite existing file
    };

    /**
     * File download progress event parameters
     */
    struct FileDownloadProgressData : public PrinterEventData
    {
        // std::string taskId;           // Task ID
        // std::string fileName;         // File name
        uint64_t totalBytes = 0;      // Total file size
        uint64_t downloadedBytes = 0; // Downloaded bytes
        int percentage = 0;           // Download percentage (0-100)
    };

    using FileDownloadResult = VoidResult;

    /**
     * Get download URL request parameters
     */
    struct GetDownloadUrlParams : public PrinterBaseParams
    {
        std::string filePath; // File path
        // Upload location, /local, /udisk, /sdcard
        std::string storageLocation; // Storage location
    };

    /**
     * Get download URL data
     */
    struct GetDownloadUrlData
    {
        std::string downloadUrl; // Download URL
    };
    using GetDownloadUrlResult = BizResult<GetDownloadUrlData>;
    using SetPrinterNameResult = VoidResult;
    /**
     * Connect printer parameters, if printerId exists, it means connecting to a discovered printer or an already connected printer
     */
    struct ConnectPrinterParams : public PrinterBaseParams
    {
        PrinterType printerType;  // (Required*) Printer type
        std::string brand;        // Printer brand
        std::string name;         // Printer name, e.g., "Elegoo Neptune 3", user customizable
        std::string model;        // (Required*) Printer model, e.g., "Neptune 3 Pro"
        std::string host;         // (Required*) Host name or IP address
        std::string serialNumber; // Printer serial number, some printers may not have serial numbers, can be empty
        // Web URL, if available, for accessing printer's web interface, e.g., http://192.168.1.100, empty if not available
        std::string webUrl; // Web URL
        /**
         * Authorization mode, such as `token` or `basic`, empty when no authorization is required
         * `token` authorization mode requires providing a token when connecting
         * `basic` authorization mode requires providing username and password when connecting
         * `accessCode` authorization mode requires providing an access code when connecting
         * `pinCode` authorization mode requires providing a PIN code when connecting
         * Other authorization modes can be added as needed
         */
        std::string authMode; // Authorization mode (such as basic, token, etc., pass empty if no authorization required)
        std::string username; // Username (only used in basic authorization mode)
        std::string password; // Password (only used in basic authorization mode)
        std::string token;    // Token (only used in token authorization mode)
        std::string accessCode; // Access code (only used in accessCode authorization mode)
        std::string pinCode;    // PIN code (only used in pinCode authorization mode)

        /**
         * Whether to check connection success when connecting to the printer, if not checked, it won't return an error even if the connection fails
         * Default value is true, indicating to check connection success
         */
        bool checkConnection = true;
        /**
         * Whether to enable automatic reconnection when the connection is lost
         */
        bool autoReconnect = false;
        int connectionTimeout = 5000; // Default connection timeout is 5 seconds
        NetworkMode networkMode = NetworkMode::LAN;       // 0: LAN, 1: WAN
        std::map<std::string, std::string> extraParams; // Extra parameters
    };

    struct ConnectPrinterData
    {
        bool isConnected = false;
        PrinterInfo printerInfo;
    };

    using ConnectPrinterResult = BizResult<ConnectPrinterData>;

    using DisconnectPrinterParams = PrinterBaseParams;
    using DisconnectPrinterResult = VoidResult;

    struct ConnectionStatusData : public PrinterEventData
    {
        ConnectionStatus status; // Connection status

        ConnectionStatusData() {}

        ConnectionStatusData(const std::string &printerId, ConnectionStatus status)
            : PrinterEventData{printerId}, status(status) {}
    };

    using GetCanvasStatusParams = PrinterBaseParams;

    using GetCanvasStatusResult = BizResult<CanvasStatus>;

    struct SetAutoRefillParams : public PrinterBaseParams
    {
        bool enable = false; // Whether to enable auto refill
    };

    using SetAutoRefillResult = VoidResult;    
} // namespace elink
