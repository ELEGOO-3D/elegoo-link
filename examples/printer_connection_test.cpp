#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <fstream>
#include <cstdio>
#include "elegoo_link.h"

using namespace elink;

/**
 * Manual Printer Connection Test Example
 * This example demonstrates how to use ElegooLink to manually connect to a specified 3D printer
 */
class PrinterConnectionTest
{
public:
    // Test configuration
    struct TestConfig
    {
        bool enableFileUpload = false;      // Enable file upload test
        bool enablePrintStart = false;      // Enable print start test
        bool verifyConnection = true;       // Verify connection after connecting
        bool showAttributes = false;        // Show detailed printer attributes
        int monitorDurationSeconds = 15;    // Duration to monitor printer status
        
        // Printer connection parameters
        std::string printerHost = "10.31.3.110";
        PrinterType printerType = PrinterType::ELEGOO_FDM_CC;
        std::string printerName = "";
        std::string printerModel = "Elegoo Neptune 4";
        std::string printerBrand = "Elegoo";
        std::string authMode = "";
        
        // File upload parameters (when enabled)
        std::string uploadFilePath = R"(C:\Users\Admin\Desktop\cube.gcode)";
        std::string uploadFileName = "cube.gcode";
    };

    PrinterConnectionTest() = default;
    ~PrinterConnectionTest() = default;
    
    void setConfig(const TestConfig& config) { config_ = config; }

    /**
     * Run the test with current configuration
     */
    void run()
    {
        printTestHeader();

        // Step 1: Initialize ElegooLink
        if (!initializeElegooLink())
        {
            std::cerr << "\n[FAILED] ElegooLink initialization failed!" << std::endl;
            return;
        }

        // Step 2: Connect to printer
        std::string printerId = connectTestPrinter();
        if (printerId.empty())
        {
            std::cerr << "\n[FAILED] Printer connection failed!" << std::endl;
            cleanup();
            return;
        }

        // Step 3: Verify connection (optional)
        if (config_.verifyConnection)
        {
            if (!verifyPrinterConnection(printerId))
            {
                std::cerr << "\n[WARNING] Connection verification failed, but continuing..." << std::endl;
            }
        }

        // Step 4: Display printer information
        displayPrinterInfo(printerId);

        // Step 5: Show detailed attributes (optional)
        if (config_.showAttributes)
        {
            getPrinterAttributes(printerId);
        }

        // Step 6: Monitor printer status
        monitorPrinterStatus(printerId);

        // Step 7: Test file upload (optional)
        if (config_.enableFileUpload)
        {
            testFileUpload(printerId);
        }

        // Step 8: Monitor status for configured duration
        if (config_.monitorDurationSeconds > 0)
        {
            std::cout << "\n=== Step 8: Monitoring ===" << std::endl;
            std::cout << "Observing printer status for " << config_.monitorDurationSeconds << " seconds..." << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(config_.monitorDurationSeconds));
        }

        // Step 9: Disconnect printer
        disconnectTestPrinter(printerId);

        // Step 10: Cleanup resources
        cleanup();

        printTestFooter();
    }
    
    /**
     * Run interactive test with menu
     */
    void runInteractive()
    {
        std::cout << "=== ElegooLink Interactive Test Menu ===" << std::endl;
        std::cout << "\nCurrent Configuration:" << std::endl;
        std::cout << "  Printer Host: " << config_.printerHost << std::endl;
        std::cout << "  File Upload: " << (config_.enableFileUpload ? "Enabled" : "Disabled") << std::endl;
        std::cout << "  Start Print: " << (config_.enablePrintStart ? "Enabled" : "Disabled") << std::endl;
        std::cout << "  Monitor Duration: " << config_.monitorDurationSeconds << "s" << std::endl;
        
        std::cout << "\nOptions:" << std::endl;
        std::cout << "  1. Run basic test (connect + info + monitor)" << std::endl;
        std::cout << "  2. Run with file upload" << std::endl;
        std::cout << "  3. Run with file upload + print" << std::endl;
        std::cout << "  4. Show detailed attributes" << std::endl;
        std::cout << "  0. Exit" << std::endl;
        std::cout << "\nEnter your choice: ";
        
        int choice;
        std::cin >> choice;
        
        switch (choice)
        {
        case 1:
            config_.enableFileUpload = false;
            config_.enablePrintStart = false;
            config_.showAttributes = false;
            run();
            break;
        case 2:
            config_.enableFileUpload = true;
            config_.enablePrintStart = false;
            run();
            break;
        case 3:
            config_.enableFileUpload = true;
            config_.enablePrintStart = true;
            run();
            break;
        case 4:
            config_.showAttributes = true;
            run();
            break;
        case 0:
            std::cout << "Exiting..." << std::endl;
            break;
        default:
            std::cerr << "Invalid choice!" << std::endl;
            break;
        }
    }

private:
    /**
     * Initialize ElegooLink
     */
    bool initializeElegooLink()
    {
        std::cout << "\n=== Step 1: Initialize ElegooLink ===" << std::endl;

        // Configure logging
        ElegooLink::Config config;
        config.log.logLevel = 1; // DEBUG level
        config.log.logEnableConsole = true;
        config.log.logEnableFile = false;

        // Get ElegooLink singleton and initialize
        auto &elegooLink = ElegooLink::getInstance();
        bool success = elegooLink.initialize(config);

        if (success)
        {
            std::cout << "[SUCCESS] ElegooLink initialized!" << std::endl;
            std::cout << "Version: " << elegooLink.getVersion() << std::endl;
        }
        else
        {
            std::cerr << "[ERROR] Initialization failed!" << std::endl;
            return false;
        }

        // Discover available printers
        std::cout << "\nDiscovering printers..." << std::endl;
        elink::PrinterDiscoveryParams discoveryParams;

       auto result = elegooLink.startPrinterDiscovery(discoveryParams);
        if (result.isSuccess() && result.hasValue())
        {
            const auto &discoveredPrinters = result.value().printers;
            std::cout << "\nDiscovered " << discoveredPrinters.size() << " printer(s):" << std::endl;
            for (const auto &printer : discoveredPrinters)
            {
                std::cout << "  - " << printer.name << " (" << printer.model << ") @ " << printer.host << std::endl;
            }
        }
        else
        {
            std::cout << "\n[WARNING] Printer discovery failed: " << result.message << std::endl;
        }
        return success;
    }

    /**
     * Connect test device
     */
    std::string connectTestPrinter()
    {
        std::cout << "\n=== Step 2: Connect to Printer ===" << std::endl;

        auto &elegooLink = ElegooLink::getInstance();
        
        // Configure connection parameters from config
        ConnectPrinterParams connectParams;
        connectParams.printerType = config_.printerType;
        connectParams.host = config_.printerHost;
        connectParams.name = config_.printerName;
        connectParams.model = config_.printerModel;
        connectParams.brand = config_.printerBrand;
        connectParams.authMode = config_.authMode;
        connectParams.autoReconnect = true;
        connectParams.connectionTimeout = 5000;

        std::cout << "Connection Parameters:" << std::endl;
        std::cout << "  Host: " << connectParams.host << std::endl;
        std::cout << "  Type: " << static_cast<int>(connectParams.printerType) << std::endl;
        std::cout << "  Model: " << connectParams.model << std::endl;
        std::cout << "  Brand: " << connectParams.brand << std::endl;

        std::cout << "\nConnecting..." << std::endl;
        auto result = elegooLink.connectPrinter(connectParams);
        
        if (result.isSuccess() && result.hasValue())
        {
            const auto &printerData = result.value();
            std::cout << "\n[SUCCESS] Connected to printer!" << std::endl;
            std::cout << "Printer ID: " << printerData.printerInfo.printerId << std::endl;
            return printerData.printerInfo.printerId;
        }
        else
        {
            std::cerr << "\n[ERROR] Connection failed!" << std::endl;
            std::cerr << "  Code: " << static_cast<int>(result.code) << std::endl;
            std::cerr << "  Message: " << result.message << std::endl;
            return "";
        }
    }

    /**
     * Verify device connection
     */
    bool verifyPrinterConnection(const std::string &printerId)
    {
        std::cout << "\n=== Step 3: Verify Connection ===" << std::endl;

        auto &elegooLink = ElegooLink::getInstance();

        return elegooLink.isPrinterConnected(printerId);
    }

    /**
     * Display device information
     */
    void displayPrinterInfo(const std::string &printerId)
    {
        std::cout << "\n=== Step 4: Printer Information ===" << std::endl;

        auto &elegooLink = ElegooLink::getInstance();

        // Get device information from printer list
        auto printerListResult = elegooLink.getPrinters();
        if (!printerListResult.isSuccess())
        {
            std::cerr << "Failed to get printer list!" << std::endl;
            return;
        }

        const auto &printerList = printerListResult.value();
        for (const auto &printerInfo : printerList.printers)
        {
            if (printerInfo.printerId == printerId)
            {
                std::cout << "Printer ID: " << printerInfo.printerId << std::endl;
                std::cout << "Printer Type: " << static_cast<int>(printerInfo.printerType) << std::endl;
                std::cout << "Brand: " << printerInfo.brand << std::endl;
                std::cout << "Manufacturer: " << printerInfo.manufacturer << std::endl;
                std::cout << "Name: " << printerInfo.name << std::endl;
                std::cout << "Model: " << printerInfo.model << std::endl;
                std::cout << "Host: " << printerInfo.host << std::endl;
                std::cout << "Firmware Version: " << printerInfo.firmwareVersion << std::endl;
                std::cout << "Serial Number: " << printerInfo.serialNumber << std::endl;
                std::cout << "Web URL: " << printerInfo.webUrl << std::endl;
                std::cout << "Auth Mode: " << printerInfo.authMode << std::endl;
                break;
            }
        }
    }

    /**
     * Test file upload functionality
     */
    void testFileUpload(const std::string &printerId)
    {
        std::cout << "\n=== Step 7: File Upload Test ===" << std::endl;

        auto &elegooLink = ElegooLink::getInstance();

        // Configure file upload parameters from config
        FileUploadParams uploadParams;
        uploadParams.printerId = printerId;
        uploadParams.localFilePath = config_.uploadFilePath;
        uploadParams.fileName = config_.uploadFileName;
        uploadParams.storageLocation = "local";
        uploadParams.overwriteExisting = true;

        std::cout << "Upload parameters:" << std::endl;
        std::cout << "  Local file path: " << uploadParams.localFilePath << std::endl;
        std::cout << "  File name: " << uploadParams.fileName << std::endl;
        std::cout << "  Storage location: " << uploadParams.storageLocation << std::endl;
        std::cout << "  Overwrite existing: " << (uploadParams.overwriteExisting ? "Yes" : "No") << std::endl;

        // Define progress callback function
        auto progressCallback = [](const FileUploadProgressData &progress) -> bool
        {
            std::cout << "\rUploading... "
                      << progress.percentage << "% "
                      << "(" << progress.uploadedBytes << "/" << progress.totalBytes << " bytes)"
                      << std::flush;

            // Return true to continue upload, return false to cancel
            return true;
        };

        std::cout << "\nStarting file upload..." << std::endl;

        // Execute file upload
        auto result = elegooLink.uploadFile(uploadParams, progressCallback);

        std::cout << std::endl; // New line because progress display uses \r

        if (result.isSuccess())
        {
            std::cout << "\n[SUCCESS] File upload completed!" << std::endl;

            // Wait for file to be processed
            std::this_thread::sleep_for(std::chrono::seconds(1));
            getPrinterStatus(printerId);
            
            // Start printing if enabled
            if (config_.enablePrintStart)
            {
                startPrint(printerId, uploadParams.fileName, uploadParams.storageLocation);
            }
        }
        else
        {
            std::cerr << "\n[ERROR] File upload failed!" << std::endl;
            std::cerr << "  Code: " << static_cast<int>(result.code) << std::endl;
            std::cerr << "  Message: " << result.message << std::endl;
        }
    }

    /**
     * Start printing
     */
    void startPrint(const std::string &printerId, const std::string &fileName, const std::string &storageLocation)
    {
        std::cout << "\n=== Starting Print ===" << std::endl;

        auto &elegooLink = ElegooLink::getInstance();

        // Configure print parameters
        StartPrintParams printParams;
        printParams.printerId = printerId;
        printParams.fileName = fileName;
        printParams.storageLocation = storageLocation;
        printParams.autoBedLeveling = false; // Disable auto bed leveling
        printParams.heatedBedType = 0;       // Use textured high temperature plate
        printParams.enableTimeLapse = false; // Disable time-lapse for now

        std::cout << "Print parameters:" << std::endl;
        std::cout << "  File name: " << printParams.fileName << std::endl;
        std::cout << "  Storage location: " << printParams.storageLocation << std::endl;
        std::cout << "  Auto bed leveling: " << (printParams.autoBedLeveling ? "Enabled" : "Disabled") << std::endl;
        std::cout << "  Heated bed type: " << (printParams.heatedBedType == 0 ? "Textured (High temp)" : "Smooth (Low temp)") << std::endl;
        std::cout << "  Time-lapse: " << (printParams.enableTimeLapse ? "Enabled" : "Disabled") << std::endl;

        std::cout << "\nStarting print job..." << std::endl;

        // Execute print start command
        auto result = elegooLink.startPrint(printParams);

        if (result.isSuccess())
        {
            std::cout << "Print started successfully!" << std::endl;
            std::cout << "The printer should now begin the printing process." << std::endl;
            std::cout << "You can monitor the print progress through the printer interface." << std::endl;
        }
        else
        {
            std::cerr << "Failed to start print!" << std::endl;
            std::cerr << "Error code: " << static_cast<int>(result.code) << std::endl;
            std::cerr << "Error message: " << result.message << std::endl;
        }
    }

    /**
     * Monitor device status
     */
    void monitorPrinterStatus(const std::string &printerId)
    {
        std::cout << "\n=== Step 6: Monitor Printer Status ===" << std::endl;

        auto &elegooLink = ElegooLink::getInstance();

        // Subscribe to printer status events
        elegooLink.subscribeEvent<PrinterStatusEvent>(
            [printerId](const std::shared_ptr<PrinterStatusEvent> &event)
            {
                if (event->status.printerId == printerId)
                {
                    displayPrinterStatus(event->status);
                }
            });
            
        // Subscribe to connection events
        elegooLink.subscribeEvent<PrinterConnectionEvent>(
            [printerId](const std::shared_ptr<PrinterConnectionEvent> &event)
            {
                if (event->connectionStatus.printerId == printerId)
                {
                    std::cout << "\n[EVENT] Connection Status: ";
                    if (event->connectionStatus.status == ConnectionStatus::CONNECTED)
                    {
                        std::cout << "CONNECTED" << std::endl;
                    }
                    else if (event->connectionStatus.status == ConnectionStatus::DISCONNECTED)
                    {
                        std::cout << "DISCONNECTED" << std::endl;
                    }
                    else
                    {
                        std::cout << static_cast<int>(event->connectionStatus.status) << std::endl;
                    }
                }
            });
            
        std::cout << "Event subscriptions active. Status updates will appear automatically." << std::endl;
    }

    void getPrinterStatus(const std::string &printerId)
    {
        auto &elegooLink = ElegooLink::getInstance();

        // Get device status
        auto result = elegooLink.getPrinterStatus({printerId});
        if (result.isSuccess())
        {
            const auto &status = result.value();
            displayPrinterStatus(status);
        }
        else
        {
            std::cerr << "Failed to get printer status!" << std::endl;
            std::cerr << "Error code: " << static_cast<int>(result.code) << std::endl;
            std::cerr << "Error message: " << result.message << std::endl;
        }
    }
    void getPrinterAttributes(const std::string &printerId)
    {
        std::cout << "\n=== Step 5: Detailed Printer Attributes ===" << std::endl;
        
        auto &elegooLink = ElegooLink::getInstance();

        auto result = elegooLink.getPrinterAttributes({printerId});
        if (result.isSuccess())
        {
            const auto &attributes = result.value();
            std::cout << "  Printer ID: " << attributes.printerId << std::endl;
            std::cout << "  Printer Type: " << static_cast<int>(attributes.printerType) << std::endl;
            std::cout << "  Host: " << attributes.host << std::endl;
            std::cout << "  Name: " << attributes.name << std::endl;
            std::cout << "  Model: " << attributes.model << std::endl;
            std::cout << "  Brand: " << attributes.brand << std::endl;
            std::cout << "  Manufacturer: " << attributes.manufacturer << std::endl;
            std::cout << "  Firmware Version: " << attributes.firmwareVersion << std::endl;
            std::cout << "  Serial Number: " << attributes.serialNumber << std::endl;
            std::cout << "  Web URL: " << attributes.webUrl << std::endl;
            // std::cout << "  Connection URL: " << attributes.connectionUrl << std::endl;
            std::cout << "  Auth Mode: " << attributes.authMode << std::endl;
            std::cout << "  Extra Info: " << std::endl;
            for (const auto &[key, value] : attributes.extraInfo)
            {
                std::cout << "    " << key << ": " << value << std::endl;
            }
        }
        else
        {
            std::cerr << "Failed to get printer attributes!" << std::endl;
            std::cerr << "Error code: " << static_cast<int>(result.code) << std::endl;
            std::cerr << "Error message: " << result.message << std::endl;
        }
    }

    /**
     * Display printer status information
     */
    static void displayPrinterStatus(const PrinterStatusData &status)
    {
        std::cout << "\n--- Printer Status Update ---" << std::endl;
        std::cout << "Printer ID: " << status.printerId << std::endl;

        // Display printer status
        std::cout << "Printer Status:" << std::endl;
        std::cout << "  Main Status: " << printerStateToString(status.printerStatus.state)
                  << " (" << static_cast<int>(status.printerStatus.state) << ")" << std::endl;
        std::cout << "  Sub Status: " << printerSubStateToString(status.printerStatus.subState)
                  << " (" << static_cast<int>(status.printerStatus.subState) << ")" << std::endl;

        if (status.printerStatus.supportProgress)
        {
            std::cout << "  Progress: " << status.printerStatus.progress << "%" << std::endl;
        }

        if (!status.printerStatus.exceptionCodes.empty())
        {
            std::cout << "  Exception Status: ";
            for (const auto &exception : status.printerStatus.exceptionCodes)
            {
                std::cout << exception << " ";
            }
            std::cout << std::endl;
        }

        // Display print status
        if (!status.printStatus.fileName.empty())
        {
            std::cout << "Print Status:" << std::endl;
            std::cout << "  File Name: " << status.printStatus.fileName << std::endl;
            std::cout << "  Task ID: " << status.printStatus.taskId << std::endl;
            std::cout << "  Progress: " << status.printStatus.progress << "%" << std::endl;
            std::cout << "  Current Layer: " << status.printStatus.currentLayer << "/" << status.printStatus.totalLayer << std::endl;
            std::cout << "  Time: " << formatTime(status.printStatus.currentTime)
                      << " / " << formatTime(status.printStatus.totalTime) << std::endl;
            std::cout << "  Estimated Time: " << formatTime(status.printStatus.estimatedTime) << std::endl;
            std::cout << "  Speed Mode: " << printSpeedModeToString(status.printStatus.printSpeedMode) << std::endl;
        }

        // Display temperature status
        if (!status.temperatureStatus.empty())
        {
            std::cout << "Temperature Status:" << std::endl;
            for (const auto &[name, temp] : status.temperatureStatus)
            {
                std::cout << "  " << name << ": " << temp.current << "°C / " << temp.target << "°C" << std::endl;
            }
        }

        // Display fan status
        if (!status.fanStatus.empty())
        {
            std::cout << "Fan Status:" << std::endl;
            for (const auto &[name, fan] : status.fanStatus)
            {
                std::cout << "  " << name << ": " << fan.speed << "% (" << fan.rpm << " RPM)" << std::endl;
            }
        }

        // Display axis position status
        std::cout << "Axes Position:" << std::endl;
        // std::cout << "  X: " << status.printAxesStatus.x << "mm" << std::endl;
        // std::cout << "  Y: " << status.printAxesStatus.y << "mm" << std::endl;
        // std::cout << "  Z: " << status.printAxesStatus.z << "mm" << std::endl;

        // Display storage status
        if (!status.storageStatus.empty())
        {
            std::cout << "Storage Status:" << std::endl;
            for (const auto &[name, storage] : status.storageStatus)
            {
                std::cout << "  " << name << ": " << (storage.connected ? "Connected" : "Disconnected") << std::endl;
            }
        }

        std::cout << "--- End Status Update ---" << std::endl;
    }

    /**
     * Convert printer main state to string
     */
    static std::string printerStateToString(PrinterState state)
    {
        switch (state)
        {
        case PrinterState::OFFLINE:
            return "OFFLINE";
        case PrinterState::IDLE:
            return "IDLE";
        case PrinterState::PRINTING:
            return "PRINTING";
        case PrinterState::SELF_CHECKING:
            return "SELF_CHECKING";
        case PrinterState::AUTO_LEVELING:
            return "AUTO_LEVELING";
        case PrinterState::PID_CALIBRATING:
            return "PID_CALIBRATING";
        case PrinterState::RESONANCE_TESTING:
            return "RESONANCE_TESTING";
        case PrinterState::UPDATING:
            return "UPDATING";
        case PrinterState::FILE_COPYING:
            return "FILE_COPYING";
        case PrinterState::FILE_TRANSFERRING:
            return "FILE_TRANSFERRING";
        case PrinterState::HOMING:
            return "HOMING";
        case PrinterState::PREHEATING:
            return "PREHEATING";
        case PrinterState::FILAMENT_OPERATING:
            return "FILAMENT_OPERATING";
        case PrinterState::EXTRUDER_OPERATING:
            return "EXTRUDER_OPERATING";
        case PrinterState::EXCEPTION:
            return "EXCEPTION";
        case PrinterState::UNKNOWN:
            return "UNKNOWN";
        default:
            return "UNDEFINED";
        }
    }

    /**
     * Convert printer sub state to string
     */
    static std::string printerSubStateToString(PrinterSubState state)
    {
        switch (state)
        {
        case PrinterSubState::NONE:
            return "NONE";
        case PrinterSubState::P_HOMING:
            return "P_HOMING";
        case PrinterSubState::P_AUTO_LEVELING:
            return "P_AUTO_LEVELING";
        case PrinterSubState::P_PRINTING:
            return "P_PRINTING";
        case PrinterSubState::P_PAUSING:
            return "P_PAUSING";
        case PrinterSubState::P_PAUSED:
            return "P_PAUSED";
        case PrinterSubState::P_STOPPING:
            return "P_STOPPING";
        case PrinterSubState::P_STOPPED:
            return "P_STOPPED";
        case PrinterSubState::P_PRINTING_COMPLETED:
            return "P_PRINTING_COMPLETED";

        case PrinterSubState::UNKNOWN:
            return "UNKNOWN";
        default:
            return "UNKNOWN";
        }
    }

    /**
     * Convert print speed mode to string
     */
    static std::string printSpeedModeToString(int mode)
    {
        switch (mode)
        {
        case 0:
            return "Silent";
        case 1:
            return "Balanced";
        case 2:
            return "Sport";
        case 3:
            return "Ludicrous";
        default:
            return "Unknown";
        }
    }

    /**
     * Format time (convert seconds to hours:minutes:seconds)
     */
    static std::string formatTime(int seconds)
    {
        if (seconds <= 0)
            return "00:00:00";

        int hours = seconds / 3600;
        int minutes = (seconds % 3600) / 60;
        int secs = seconds % 60;

        char buffer[16];
        snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d", hours, minutes, secs);
        return std::string(buffer);
    }

    /**
     * Disconnect test device
     */
    void disconnectTestPrinter(const std::string &printerId)
    {
        std::cout << "\n=== Step 9: Disconnect Printer ===" << std::endl;

        auto &elegooLink = ElegooLink::getInstance();
        auto result = elegooLink.disconnectPrinter(printerId);

        if (result.isSuccess())
        {
            std::cout << "[SUCCESS] Printer disconnected!" << std::endl;
        }
        else
        {
            std::cerr << "[ERROR] Disconnect failed!" << std::endl;
            std::cerr << "  Code: " << static_cast<int>(result.code) << std::endl;
            std::cerr << "  Message: " << result.message << std::endl;
        }
    }

    /**
     * Clean up resources
     */
    void cleanup()
    {
        std::cout << "\n=== Step 10: Cleanup ===" << std::endl;

        auto &elegooLink = ElegooLink::getInstance();
        elegooLink.cleanup();

        std::cout << "[SUCCESS] Resources cleaned up!" << std::endl;
    }
    
private:
    TestConfig config_;
    
    void printTestHeader()
    {
        std::cout << "\n";
        std::cout << "========================================" << std::endl;
        std::cout << "  ElegooLink Printer Connection Test" << std::endl;
        std::cout << "========================================" << std::endl;
    }
    
    void printTestFooter()
    {
        std::cout << "\n";
        std::cout << "========================================" << std::endl;
        std::cout << "  Test Completed Successfully" << std::endl;
        std::cout << "========================================" << std::endl;
        std::cout << "\n";
    }
};

/**
 * Main function
 */
int main(int argc, char* argv[])
{
    try
    {
        PrinterConnectionTest test;
        
        // Configure test based on command line arguments
        PrinterConnectionTest::TestConfig config;
        
        // Parse simple command line options
        bool interactiveMode = true;
        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == "--upload" || arg == "-u")
            {
                config.enableFileUpload = true;
            }
            else if (arg == "--print" || arg == "-p")
            {
                config.enableFileUpload = true;
                config.enablePrintStart = true;
            }
            else if (arg == "--attributes" || arg == "-a")
            {
                config.showAttributes = true;
            }
            else if (arg == "--interactive" || arg == "-i")
            {
                interactiveMode = true;
            }
            else if (arg == "--help" || arg == "-h")
            {
                std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
                std::cout << "\nOptions:" << std::endl;
                std::cout << "  -u, --upload       Enable file upload test" << std::endl;
                std::cout << "  -p, --print        Enable file upload and print test" << std::endl;
                std::cout << "  -a, --attributes   Show detailed printer attributes" << std::endl;
                std::cout << "  -i, --interactive  Run in interactive mode" << std::endl;
                std::cout << "  -h, --help         Show this help message" << std::endl;
                std::cout << "\nExamples:" << std::endl;
                std::cout << "  " << argv[0] << "                # Basic connection test" << std::endl;
                std::cout << "  " << argv[0] << " -u            # Test with file upload" << std::endl;
                std::cout << "  " << argv[0] << " -p            # Test with upload and print" << std::endl;
                std::cout << "  " << argv[0] << " -i            # Interactive menu" << std::endl;
                return 0;
            }
        }
        
        test.setConfig(config);
        
        if (interactiveMode)
        {
            test.runInteractive();
        }
        else
        {
            test.run();
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "\n[EXCEPTION] " << e.what() << std::endl;
        return 1;
    }
    catch (...)
    {
        std::cerr << "\n[EXCEPTION] Unknown error occurred!" << std::endl;
        return 1;
    }

    return 0;
}
