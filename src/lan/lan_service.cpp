#include "lan_service.h"
#include "lan_service_impl.h"
#include "core/printer_manager.h"
#include "core/printer_factory.h"
#include "discovery/printer_discovery.h"
#include "core/printer.h"
#include "adapters/elegoo_cc_adapters.h"
#include "adapters/elegoo_cc2_adapters.h"
#include "adapters/generic_moonraker_adapters.h"
#include "utils/logger.h"
#include "utils/utils.h"
#include <algorithm>
#include <thread>
#include <chrono>
#include <mutex>
#include <condition_variable>
#include "version.h"
#include "adapters/elegoo_cc_adapters.h"
#include "adapters/elegoo_cc2_adapters.h"
#include "adapters/generic_moonraker_adapters.h"
#include "types/internal/json_serializer.h"

// Macro to validate and get printer, returning early if validation fails
#define VALIDATE_AND_GET_PRINTER(printerId, printer, ReturnType)                              \
    auto [printer, validationResult##printer] = pImpl_->validateAndGetPrinter(printerId);     \
    if (!printer)                                                                             \
    {                                                                                         \
        return ReturnType{validationResult##printer.code, validationResult##printer.message}; \
    }

namespace elink
{
    static bool s_enableStaticWebServer;
    static int s_webServerPort;
    static std::string s_staticWebPath;
    bool enableStaticWebServer()
    {
        return s_enableStaticWebServer;
    }
    int webServerPort()
    {
        return s_webServerPort;
    }
    bool isWebServerRunning()
    {
        return s_enableStaticWebServer && s_webServerPort > 0;
    }
    std::string localStaticWebPath()
    {
        return s_staticWebPath;
    }
    // ========== LanServiceImpl Implementation ==========

    LanServiceImpl::LanServiceImpl()
        : initialized_(false)
    {
    }

    LanServiceImpl::~LanServiceImpl()
    {
    }

    // ========== LanService Implementation ==========

    LanService &LanService::getInstance()
    {
        // Meyer's Singleton - C++11 guarantees thread-safe initialization of static local variables
        static LanService instance;
        return instance;
    }

    LanService::LanService()
        : eventBus_(), pImpl_(std::make_unique<LanServiceImpl>())
    {
    }

    LanService::~LanService()
    {
        cleanup();
    }

    bool LanService::initialize(const Config &config)
    {
        if (pImpl_->initialized_)
        {
            ELEGOO_LOG_WARN("LanService is already initialized");
            return true;
        }

        pImpl_->config_ = config;
        try
        {
            ELEGOO_LOG_INFO("Initializing LanService...");

            // 1. Initialize adapters
            if (!pImpl_->initializeAdapters())
            {
                ELEGOO_LOG_ERROR("Failed to initialize adapters");
                return false;
            }

            // 2. Create printer manager
            pImpl_->printerManager_ = std::make_shared<PrinterManager>();
            if (!pImpl_->printerManager_ || !pImpl_->printerManager_->initialize())
            {
                ELEGOO_LOG_ERROR("Failed to initialize printer manager");
                return false;
            }

            // 3. Create printer discovery
            pImpl_->printerDiscovery_ = std::make_shared<PrinterDiscovery>();
            if (!pImpl_->printerDiscovery_)
            {
                ELEGOO_LOG_ERROR("Failed to create printer discovery");
                return false;
            }

            // 4. Set printer manager event callback
            pImpl_->printerManager_->setPrinterEventCallback([this](const BizEvent &event)
                                                             {
                // Dispatch events through strongly-typed event system
                eventBus_.publishFromEvent(event); });

            s_staticWebPath = config.staticWebPath;

            if (config.enableWebServer && !s_staticWebPath.empty())
            {
                // 6. Initialize static web server if enabled
                pImpl_->server_ = std::make_unique<StaticWebServer>(config.webServerPort);
                pImpl_->server_->setStaticPath(s_staticWebPath);
                if (pImpl_->server_->start())
                {
                    ELEGOO_LOG_INFO("Static web server started on port {}", config.webServerPort);
                    s_enableStaticWebServer = true;
                    s_webServerPort = config.webServerPort;
                }
                else
                {
                    ELEGOO_LOG_ERROR("Failed to start static web server");
                    // return false; // Don't fail initialization if web server fails, some printer features may not require it
                }
            }

            pImpl_->initialized_ = true;
            ELEGOO_LOG_INFO("LanService initialized successfully");
            return true;
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Exception during LanService initialization: {}", e.what());
            cleanup();
            return false;
        }
    }

    void LanService::cleanup()
    {
        if (!pImpl_->initialized_)
        {
            return;
        }

        ELEGOO_LOG_INFO("Cleaning up LanService...");

        // Clean up event bus
        eventBus_.clear();

        // Clean up printer manager
        if (pImpl_->printerManager_)
        {
            pImpl_->printerManager_->setPrinterEventCallback(nullptr);
            pImpl_->printerManager_->cleanup();
            pImpl_->printerManager_.reset();
        }

        // Clean up printer discovery
        if (pImpl_->printerDiscovery_)
        {
            pImpl_->printerDiscovery_->stopDiscovery();
            pImpl_->printerDiscovery_.reset();
        }

        // Clean up static web server
        if (pImpl_->server_)
        {
            pImpl_->server_->stop();
            pImpl_->server_.reset();
            s_enableStaticWebServer = false;
            s_webServerPort = 0;
        }

        pImpl_->initialized_ = false;
        ELEGOO_LOG_INFO("LanService cleanup completed");
    }

    bool LanService::isInitialized() const
    {
        return pImpl_->initialized_;
    }

    BizResult<PrinterDiscoveryData> LanService::startPrinterDiscovery(const PrinterDiscoveryParams &params)
    {
        nlohmann::json paramJson = params;
        ELEGOO_LOG_INFO("Printer discovery parameters: {}", paramJson.dump());

        if (!pImpl_->initialized_)
        {
            ELEGOO_LOG_ERROR("LanService is not initialized");
            return BizResult<PrinterDiscoveryData>{
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "LanService is not initialized"};
        }

        if (!pImpl_->printerDiscovery_)
        {
            ELEGOO_LOG_ERROR("Printer discovery is not available");
            return BizResult<PrinterDiscoveryData>{
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "Printer discovery is not available"};
        }

        PrinterDiscoveryParams config = params;

        // Convert from request to configuration
        DiscoveryConfig discoveryConfig;
        discoveryConfig.timeoutMs = config.timeoutMs;
        discoveryConfig.broadcastInterval = config.broadcastInterval;
        discoveryConfig.enableAutoRetry = config.enableAutoRetry;
        discoveryConfig.preferredListenPorts = config.preferredListenPorts;

        BizResult<PrinterDiscoveryData> res;
        res.data = PrinterDiscoveryData();

        // Check if discovery is already in progress
        if (pImpl_->printerDiscovery_->isDiscovering())
        {
            ELEGOO_LOG_INFO("Printer discovery is already in progress, waiting for completion");
            std::this_thread::sleep_for(std::chrono::milliseconds(discoveryConfig.timeoutMs));
            auto allPrinters = pImpl_->printerDiscovery_->getDiscoveredPrinters();

            res.data.value().printers = allPrinters;
            res.code = ELINK_ERROR_CODE::SUCCESS;
        }
        else
        {
            // Start printer discovery
            auto printers = pImpl_->printerDiscovery_->discoverPrintersSync(discoveryConfig);
            res.code = ELINK_ERROR_CODE::SUCCESS;
            res.message = "Printer discovery successful";
            res.data.value().printers = printers;
            ELEGOO_LOG_INFO("Printer discovery completed, found {} new printers", res.data.value().printers.size());
        }
        return res;
    }
    VoidResult LanService::startPrinterDiscoveryAsync(const PrinterDiscoveryParams &params, std::function<void(const PrinterInfo &)> discoveredCallback,
                                                      std::function<void(const std::vector<PrinterInfo> &)> completionCallback)
    {
        nlohmann::json paramJson = params;
        ELEGOO_LOG_INFO("Printer discovery parameters: {}", paramJson.dump());

        if (!pImpl_->initialized_)
        {
            ELEGOO_LOG_ERROR("LanService is not initialized");
            return VoidResult{
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "LanService is not initialized"};
        }

        if (!pImpl_->printerDiscovery_)
        {
            ELEGOO_LOG_ERROR("Printer discovery is not available");
            return VoidResult{
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "Printer discovery is not available"};
        }

        PrinterDiscoveryParams config = params;

        // Convert from request to configuration
        DiscoveryConfig discoveryConfig;
        discoveryConfig.timeoutMs = config.timeoutMs;
        discoveryConfig.broadcastInterval = config.broadcastInterval;
        discoveryConfig.enableAutoRetry = config.enableAutoRetry;
        discoveryConfig.preferredListenPorts = config.preferredListenPorts;

        // Check if discovery is already in progress
        if (pImpl_->printerDiscovery_->isDiscovering())
        {
            ELEGOO_LOG_INFO("Printer discovery is already in progress, waiting for completion");
            return VoidResult{
                ELINK_ERROR_CODE::OPERATION_IN_PROGRESS,
                "Printer discovery is already in progress"};
        }
        else
        {
            // Start printer discovery
            bool ret = pImpl_->printerDiscovery_->startDiscovery(discoveryConfig, discoveredCallback, completionCallback);
            if (ret)
            {
                ELEGOO_LOG_INFO("Printer discovery started successfully");
            }
            else
            {
                ELEGOO_LOG_ERROR("Failed to start printer discovery");
                return VoidResult{
                    ELINK_ERROR_CODE::UNKNOWN_ERROR,
                    "Failed to start printer discovery"};
            }
        }
        return VoidResult::Success();
    }
    VoidResult LanService::stopPrinterDiscovery()
    {
        if (!pImpl_->initialized_)
        {
            ELEGOO_LOG_ERROR("LanService is not initialized");
            return VoidResult{
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "LanService is not initialized"};
        }

        if (pImpl_->printerDiscovery_)
        {
            pImpl_->printerDiscovery_->stopDiscovery();
        }

        return VoidResult::Success();
    }

    std::vector<PrinterInfo> LanService::getDiscoveredPrinters() const
    {
        if (!pImpl_->initialized_ || !pImpl_->printerDiscovery_)
        {
            return {};
        }

        auto allPrinters = pImpl_->printerDiscovery_->getDiscoveredPrinters();
        return allPrinters;
    }

    ConnectPrinterResult LanService::connectPrinter(const ConnectPrinterParams &params)
    {
        // LOG PARAMS
        nlohmann::json paramJson = params;
        paramJson["printerId"] = StringUtils::maskString(paramJson.value("printerId", ""));
        paramJson["serialNumber"] = StringUtils::maskString(paramJson.value("serialNumber", ""));
        ELEGOO_LOG_INFO("Connect printer parameters: {}", paramJson.dump());

        if (!pImpl_->initialized_)
        {
            ELEGOO_LOG_ERROR("LanService is not initialized");
            return ConnectPrinterResult{
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "LanService is not initialized"};
        }

        if (!pImpl_->printerManager_)
        {
            ELEGOO_LOG_ERROR("Printer manager is not available");
            return ConnectPrinterResult{
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "Printer manager is not available"};
        }

        std::string host = UrlUtils::extractHost(params.host);
        if (host.empty())
        {
            ELEGOO_LOG_ERROR("Invalid host in connection parameters: {}", params.host);
            return ConnectPrinterResult{
                ELINK_ERROR_CODE::INVALID_PARAMETER,
                "Invalid host in connection parameters"};
        }

        // If printer not found or printer ID is empty, construct PrinterInfo using connection parameters
        if (params.model.empty() || params.printerType == PrinterType::UNKNOWN)
        {
            ELEGOO_LOG_ERROR("Printer model and host are required for connection");
            return ConnectPrinterResult{
                ELINK_ERROR_CODE::INVALID_PARAMETER,
                "Printer model are required for connection"};
        }

        // Determine the printer identifier to track
        std::string printerIdentifier = params.printerId.empty() ? params.serialNumber : params.printerId;

        // Check if this printer is already being connected
        if (!printerIdentifier.empty())
        {
            std::lock_guard<std::mutex> lock(pImpl_->connectingPrintersMutex_);
            if (pImpl_->connectingPrinters_.count(printerIdentifier) > 0)
            {
                ELEGOO_LOG_WARN("Printer {} is already being connected, rejecting duplicate request", StringUtils::maskString(printerIdentifier));
                return ConnectPrinterResult{
                    ELINK_ERROR_CODE::OPERATION_IN_PROGRESS,
                    "Printer connection is already in progress"};
            }
            // Add to connecting set
            pImpl_->connectingPrinters_.insert(printerIdentifier);
        }

        // Only check existing connections when printer ID is provided
        if (!params.printerId.empty())
        {
            // Check if printer is already connected
            if (auto existingResponse = pImpl_->checkExistingConnection(params.printerId))
            {
                // Remove from connecting set before returning
                std::lock_guard<std::mutex> lock(pImpl_->connectingPrintersMutex_);
                pImpl_->connectingPrinters_.erase(printerIdentifier);
                return *existingResponse;
            }
        }

        PrinterInfo printerInfo = pImpl_->createPrinterInfoFromParams(params);
        auto result = pImpl_->createAndConnectPrinter(printerInfo, params, params.checkConnection);

        if (!printerIdentifier.empty())
        {
            std::lock_guard<std::mutex> lock(pImpl_->connectingPrintersMutex_);
            pImpl_->connectingPrinters_.erase(printerIdentifier);
        }

        return result;
    }

    VoidResult LanService::disconnectPrinter(const std::string &printerId)
    {
        if (!pImpl_->initialized_)
        {
            ELEGOO_LOG_ERROR("LanService is not initialized");
            return VoidResult{
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "LanService is not initialized"};
        }

        if (!pImpl_->printerManager_)
        {
            ELEGOO_LOG_ERROR("Printer manager is not available");
            return VoidResult{
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "Printer manager is not available"};
        }

        auto printer = pImpl_->printerManager_->getPrinter(printerId);
        if (!printer)
        {
            ELEGOO_LOG_ERROR("Printer {} not found for disconnection", StringUtils::maskString(printerId));
            return VoidResult{ELINK_ERROR_CODE::PRINTER_NOT_FOUND, "Printer not found: " + printerId};
        }

        // First disconnect the printer
        BizResult<nlohmann::json> disconnectResponse = printer->disconnect();

        // Remove printer from printer list regardless of disconnection success
        if (pImpl_->printerManager_->removePrinter(printer->getId()))
        {
            ELEGOO_LOG_INFO("Printer {} disconnected and removed from printer list", StringUtils::maskString(printerId));
        }
        else
        {
            ELEGOO_LOG_ERROR("Failed to remove printer {} from printer list after disconnection", StringUtils::maskString(printerId));
            return VoidResult{ELINK_ERROR_CODE::UNKNOWN_ERROR, "Failed to remove printer from printer list after disconnection"};
        }

        return VoidResult{ELINK_ERROR_CODE::SUCCESS, "Printer disconnected successfully"};
    }

    GetPrinterListResult LanService::getPrinters()
    {
        if (!pImpl_->initialized_)
        {
            ELEGOO_LOG_ERROR("LanService is not initialized");
            return GetPrinterListResult{
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "LanService is not initialized"};
        }

        if (!pImpl_->printerManager_)
        {
            ELEGOO_LOG_ERROR("Printer manager is not available");
            return GetPrinterListResult{
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "Printer manager is not available"};
        }

        auto printers = pImpl_->printerManager_->getAllPrinters();
        GetPrinterListResult response;
        response.code = ELINK_ERROR_CODE::SUCCESS;

        std::vector<PrinterInfo> printerInfos;
        for (const auto &printer : printers)
        {
            if (printer)
            {
                printerInfos.push_back(printer->getPrinterInfo());
            }
        }
        response.data = GetPrinterListData();
        response.data.value().printers = printerInfos;

        return response;
    }

    std::shared_ptr<Printer> LanService::getPrinter(const std::string &printerId)
    {
        if (!pImpl_->initialized_ || !pImpl_->printerManager_)
        {
            return nullptr;
        }

        return pImpl_->printerManager_->getPrinter(printerId);
    }

    bool LanService::isPrinterConnected(const std::string &printerId) const
    {
        if (!pImpl_->initialized_ || !pImpl_->printerManager_)
        {
            ELEGOO_LOG_ERROR("LanService is not initialized or printer manager is not available");
            return false;
        }

        auto printer = pImpl_->printerManager_->getPrinter(printerId);
        if (printer)
        {
            return printer->isConnected();
        }
        ELEGOO_LOG_WARN("Printer {} not found", StringUtils::maskString(printerId));
        return false;
    }

    std::string LanService::getVersion() const
    {
        return ELEGOO_LINK_SDK_VERSION; // Version number can be obtained from configuration file or macro definition
    }

    std::vector<PrinterType> LanService::getSupportedPrinterTypes() const
    {
        if (!pImpl_->initialized_)
        {
            ELEGOO_LOG_ERROR("LanService is not initialized");
            return {};
        }
        // Return all supported printer types
        return {
            PrinterType::ELEGOO_FDM_CC,
            PrinterType::ELEGOO_FDM_CC2,
            PrinterType::ELEGOO_FDM_KLIPPER,
            PrinterType::GENERIC_FDM_KLIPPER};
    }

    std::vector<PrinterInfo> LanService::getCachedPrinters() const
    {
        if (!pImpl_->initialized_)
        {
            ELEGOO_LOG_ERROR("LanService is not initialized");
            return {};
        }
        if (!pImpl_->printerManager_)
        {
            ELEGOO_LOG_ERROR("Printer manager is not available");
            return {};
        }
        return pImpl_->printerManager_->getCachedPrinters();
    }

    // ========== Private Method Implementation ==========

    bool LanServiceImpl::initializeAdapters()
    {
        // No longer need to register adapters - printer subclasses create their own components
        ELEGOO_LOG_INFO("Adapter initialization skipped - using direct instantiation in printer subclasses");
        return true;
    }

    std::vector<PrinterInfo> LanServiceImpl::filterUnregisteredPrinters(const std::vector<PrinterInfo> &printers) const
    {
        if (!printerManager_)
        {
            // If no PrinterManager, return all printers
            ELEGOO_LOG_DEBUG("PrinterManager not available, returning all {} printers", printers.size());
            return printers;
        }

        std::vector<PrinterInfo> unregisteredPrinters;
        unregisteredPrinters.reserve(printers.size()); // Pre-allocate memory for performance improvement

        for (const auto &printer : printers)
        {
            if (!printerManager_->getPrinter(printer.printerId))
            {
                ELEGOO_LOG_DEBUG("Printer {} is not registered in PrinterManager", StringUtils::maskString(printer.printerId));
                unregisteredPrinters.push_back(printer);
            }
            else
            {
                ELEGOO_LOG_DEBUG("Printer {} is already registered in PrinterManager", StringUtils::maskString(printer.printerId));
            }
        }

        ELEGOO_LOG_DEBUG("Filtered {} unregistered printers from {} total printers",
                         unregisteredPrinters.size(), printers.size());
        return unregisteredPrinters;
    }

    std::optional<ConnectPrinterResult> LanServiceImpl::checkExistingConnection(const std::string &printerId) const
    {
        if (printerId.empty() || !printerManager_)
        {
            return std::nullopt;
        }

        auto existingPrinter = printerManager_->getPrinter(printerId);
        if (existingPrinter && existingPrinter->isConnected())
        {
            ELEGOO_LOG_INFO("Printer {} is already connected", StringUtils::maskString(printerId));
            ConnectPrinterResult result;
            result.code = ELINK_ERROR_CODE::SUCCESS;
            result.message = "Printer already connected";

            ConnectPrinterData connectResult;
            connectResult.printerInfo = existingPrinter->getPrinterInfo();
            connectResult.isConnected = true;
            result.data = connectResult;
            return result;
        }
        return std::nullopt;
    }

    std::optional<PrinterInfo> LanServiceImpl::findDiscoveredPrinter(const std::string &printerId) const
    {
        if (printerId.empty() || !printerDiscovery_)
        {
            return std::nullopt;
        }

        auto printers = printerDiscovery_->getDiscoveredPrinters();
        auto it = std::find_if(printers.begin(), printers.end(),
                               [&printerId](const PrinterInfo &printer)
                               { return printer.printerId == printerId; });

        if (it != printers.end())
        {
            return *it;
        }
        return std::nullopt;
    }

    PrinterInfo LanServiceImpl::createPrinterInfoFromParams(const ConnectPrinterParams &params) const
    {
        PrinterInfo printerInfo;
        // printerInfo.printerId = params.printerId.empty() ? CryptoUtils::generateUUID() : params.printerId;
        printerInfo.printerId = params.printerId;
        if (printerInfo.printerId.empty())
        {
            std::string uniquePart = CryptoUtils::generateUUID();
            printerInfo.printerId = PRINTER_ID_PREFIX_ELEGOO_LAN + params.serialNumber.empty() ? uniquePart : params.serialNumber;
        }
        printerInfo.printerType = params.printerType;
        printerInfo.brand = params.brand;
        printerInfo.name = params.name;
        printerInfo.model = params.model;
        std::string host = params.host;
        printerInfo.host = host;
        printerInfo.webUrl = params.webUrl;
        printerInfo.authMode = params.authMode;
        printerInfo.serialNumber = params.serialNumber;

        // Get discovery strategy to retrieve webUrl
        auto strategy = PrinterDiscovery::getDiscoveryStrategy(printerInfo.printerType);
        if (strategy)
        {
            const auto webUrl = strategy->getWebUrl(printerInfo.host, 0);
            printerInfo.webUrl = webUrl;
        }

        return printerInfo;
    }

    ConnectPrinterResult LanServiceImpl::createAndConnectPrinter(const PrinterInfo &printerInfo,
                                                                 const ConnectPrinterParams &params,
                                                                 bool addOnlyIfConnected)
    {
        // Create printer instance using factory
        std::shared_ptr<Printer> printer;
        PrinterPtr oldPrinter;
        try
        {
            // Use PrinterFactory to create the appropriate printer subclass
            printer = PrinterFactory::createPrinter(printerInfo);

            if (!printer)
            {
                ELEGOO_LOG_ERROR("Failed to create printer instance for {}", StringUtils::maskString(printerInfo.printerId));
                return ConnectPrinterResult{
                    ELINK_ERROR_CODE::UNKNOWN_ERROR,
                    "Failed to create printer instance for " + printerInfo.printerId};
            }

            // If need to replace existing printer, remove old one first
            if (!params.printerId.empty())
            {
                oldPrinter = printerManager_->getPrinter(params.printerId);
                if (oldPrinter)
                {
                    printerManager_->removePrinter(params.printerId);
                }
            }

            if (printerManager_->addConnectedPrinter(printer))
            {
                ELEGOO_LOG_DEBUG("Printer {} successfully created and added to printer list", StringUtils::maskString(printerInfo.printerId));
            }
            else
            {
                ELEGOO_LOG_ERROR("Printer {} created but failed to add to printer list", StringUtils::maskString(printerInfo.printerId));
                return ConnectPrinterResult{
                    ELINK_ERROR_CODE::UNKNOWN_ERROR,
                    "Printer created but failed to add to printer list: " + printerInfo.printerId};
            }
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Exception creating printer for {}: {}", StringUtils::maskString(printerInfo.printerId), e.what());
            return ConnectPrinterResult{
                ELINK_ERROR_CODE::UNKNOWN_ERROR,
                "Exception creating printer instance for " + printerInfo.printerId + ": " + e.what()};
        }

        // Try to connect printer
        BizResult<nlohmann::json> connectResponse = printer->connect(params);

        // Handle printer manager operations
        bool needRemovePrinter = false;

        if (addOnlyIfConnected)
        {
            // Only add to printer list if connection succeeds (connectPrinter behavior)
            needRemovePrinter = (connectResponse.code != ELINK_ERROR_CODE::SUCCESS);
        }

        if (needRemovePrinter && oldPrinter == nullptr)
        {
            printerManager_->removePrinter(printerInfo.printerId);
            ELEGOO_LOG_INFO("Printer {} connection failed, not adding to printer list. Error: {}",
                            StringUtils::maskString(printerInfo.printerId), connectResponse.message);
        }

        // Construct response
        ConnectPrinterResult result;
        // If the printer is added to the printer list regardless of connection success, return success status
        if (addOnlyIfConnected)
        {
            result.code = connectResponse.code;
            result.message = connectResponse.message;
        }
        else
        {
            result.code = ELINK_ERROR_CODE::SUCCESS;
            result.message = "Printer created successfully";
        }

        ConnectPrinterData connectResult;
        connectResult.printerInfo = printer->getPrinterInfo();
        connectResult.isConnected = (connectResponse.code == ELINK_ERROR_CODE::SUCCESS);
        result.data = connectResult;

        return result;
    }

    // ========== File Upload Functionality Implementation ==========

    std::pair<std::shared_ptr<Printer>, VoidResult> LanServiceImpl::validateAndGetPrinter(const std::string &printerId)
    {
        if (!initialized_)
        {
            ELEGOO_LOG_ERROR("LanService is not initialized");
            return {nullptr, VoidResult{ELINK_ERROR_CODE::NOT_INITIALIZED, "LanService is not initialized"}};
        }

        if (!printerManager_)
        {
            ELEGOO_LOG_ERROR("Printer manager is not available");
            return {nullptr, VoidResult{ELINK_ERROR_CODE::NOT_INITIALIZED, "Printer manager is not available"}};
        }

        // Get printer
        auto printer = printerManager_->getPrinter(printerId);
        if (!printer)
        {
            ELEGOO_LOG_ERROR("Printer not found: {}", StringUtils::maskString(printerId));
            return {nullptr, VoidResult{ELINK_ERROR_CODE::PRINTER_NOT_FOUND, "Printer not found: " + printerId}};
        }

        // // Check if printer is connected (only check for file upload)
        // if (!printer->isConnected())
        // {
        //     ELEGOO_LOG_ERROR("Printer {} is not connected", StringUtils::maskString(printerId));
        //     return {nullptr, VoidResult{ELINK_ERROR_CODE::PRINTER_CONNECTION_ERROR, "Printer is not connected: " + printerId}};
        // }

        return {printer, VoidResult{ELINK_ERROR_CODE::SUCCESS, "Printer validation successful"}};
    }

    FileUploadResult LanService::uploadFile(
        const FileUploadParams &params,
        FileUploadProgressCallback progressCallback)
    {
        ELEGOO_LOG_INFO("[{}] Starting file upload", StringUtils::maskString(params.printerId));
        ELEGOO_LOG_DEBUG("[{}] File upload parameters: {}", StringUtils::maskString(params.printerId), nlohmann::json(params).dump());

        VALIDATE_AND_GET_PRINTER(params.printerId, printer, FileUploadResult)

        std::shared_ptr<IHttpFileTransfer> fileUploader = printer->getFileUploader();
        if (!fileUploader)
        {
            ELEGOO_LOG_ERROR("File uploader is not available for printer: {}", StringUtils::maskString(params.printerId));
            return FileUploadResult{ELINK_ERROR_CODE::UNKNOWN_ERROR, "File uploader is not available for printer: " + StringUtils::maskString(params.printerId)};
        }

        // Directly call uploadFile and get the result
        auto result = fileUploader->uploadFile(
            printer->getPrinterInfo(),
            params,
            [progressCallback](const FileUploadProgressData &progressData) -> bool
            {
                // Progress callback
                if (progressCallback)
                {
                    return progressCallback(progressData);
                }
                return true; // Return true to continue upload, false to cancel upload
            });

        if (result.code == ELINK_ERROR_CODE::SUCCESS)
        {
            ELEGOO_LOG_INFO("File upload completed successfully for printer: {}, file: {}",
                            StringUtils::maskString(params.printerId), params.fileName);
        }
        else
        {
            ELEGOO_LOG_ERROR("File upload failed for printer: {}, error: {}",
                             StringUtils::maskString(params.printerId), result.message);
        }

        return result;
    }

    VoidResult LanService::cancelFileUpload(const CancelFileUploadParams &params)
    {
        ELEGOO_LOG_INFO("[{}] Cancelling file upload", StringUtils::maskString(params.printerId));

        VALIDATE_AND_GET_PRINTER(params.printerId, printer, VoidResult)

        std::shared_ptr<IHttpFileTransfer> fileUploader = printer->getFileUploader();
        if (!fileUploader)
        {
            ELEGOO_LOG_ERROR("File uploader is not available for printer: {}", StringUtils::maskString(params.printerId));
            return VoidResult::Error(ELINK_ERROR_CODE::UNKNOWN_ERROR, "File uploader is not available for printer: " + StringUtils::maskString(params.printerId));
        }

        // Call cancel on the file uploader
        auto result = fileUploader->cancelFileUpload();

        if (result.isSuccess())
        {
            ELEGOO_LOG_INFO("File upload cancellation requested successfully for printer: {}",
                            StringUtils::maskString(params.printerId));
        }
        else
        {
            ELEGOO_LOG_ERROR("Failed to cancel file upload for printer: {}, error: {}",
                             StringUtils::maskString(params.printerId), result.message);
        }

        return result;
    }

    PrinterAttributesResult LanService::getPrinterAttributes(const PrinterAttributesParams &params, int timeout)
    {
        VALIDATE_AND_GET_PRINTER(params.printerId, printer, PrinterAttributesResult)
        return printer->getPrinterAttributes(params, timeout);
    }

    PrinterStatusResult LanService::getPrinterStatus(const PrinterStatusParams &params, int timeout)
    {
        VALIDATE_AND_GET_PRINTER(params.printerId, printer, PrinterStatusResult)
        return printer->getPrinterStatus(params, timeout);
    }

    VoidResult LanService::refreshPrinterAttributes(const PrinterAttributesParams &params)
    {
        // Fire and forget - use very short timeout (1ms) to return immediately
        getPrinterAttributes(params, 1);
        return VoidResult::Success();
    }

    VoidResult LanService::refreshPrinterStatus(const PrinterStatusParams &params)
    {
        // Fire and forget - use very short timeout (1ms) to return immediately
        getPrinterStatus(params, 1);
        return VoidResult::Success();
    }

    // ========== Print Control Functionality Implementation ==========

    VoidResult LanService::startPrint(const StartPrintParams &params)
    {
        VALIDATE_AND_GET_PRINTER(params.printerId, printer, VoidResult)
        return printer->startPrint(params);
    }

    VoidResult LanService::pausePrint(const PrinterBaseParams &params)
    {
        VALIDATE_AND_GET_PRINTER(params.printerId, printer, VoidResult)
        return printer->pausePrint(params);
    }

    VoidResult LanService::resumePrint(const PrinterBaseParams &params)
    {
        VALIDATE_AND_GET_PRINTER(params.printerId, printer, VoidResult)
        return printer->resumePrint(params);
    }

    VoidResult LanService::stopPrint(const PrinterBaseParams &params)
    {
        VALIDATE_AND_GET_PRINTER(params.printerId, printer, VoidResult)
        return printer->stopPrint(params);
    }

    GetCanvasStatusResult LanService::getCanvasStatus(const GetCanvasStatusParams &params)
    {
        VALIDATE_AND_GET_PRINTER(params.printerId, printer, GetCanvasStatusResult)
        return printer->getCanvasStatus(params);
    }

    VoidResult LanService::setAutoRefill(const SetAutoRefillParams &params)
    {
        VALIDATE_AND_GET_PRINTER(params.printerId, printer, VoidResult)
        return printer->setAutoRefill(params);
    }

    void LanService::setEventCallback(std::function<int(const BizEvent &)> callback)
    {
        if (!pImpl_->initialized_)
        {
            ELEGOO_LOG_ERROR("LanService is not initialized");
            return;
        }

        if (!pImpl_->printerManager_)
        {
            ELEGOO_LOG_ERROR("Printer manager is not available");
            return;
        }

        // Set the callback on the printer manager
        // This will be called in addition to the internal event bus publishing
        pImpl_->printerManager_->setPrinterEventCallback(
            [this, callback](const BizEvent &event)
            {
                // First publish to internal event bus
                eventBus_.publishFromEvent(event);

                // Then call the external callback if provided
                if (callback)
                {
                    callback(event);
                }
            });
    }

    VoidResult LanService::updatePrinterName(const UpdatePrinterNameParams &params)
    {
        VALIDATE_AND_GET_PRINTER(params.printerId, printer, VoidResult)
        return printer->updatePrinterName(params);
    }
} // namespace elink
