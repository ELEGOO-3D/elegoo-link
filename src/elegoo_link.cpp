#include "elegoo_link.h"
#include "lan/lan_service.h"
#ifdef ENABLE_CLOUD_FEATURES
#include "cloud/cloud_service.h"
#endif
#include "utils/logger.h"
#include "version.h"
#include <algorithm>

namespace elink
{
    // ========== Private Implementation Class ==========
    class ElegooLink::Impl
    {
    public:
        Impl() : initialized_(false) {}

        ~Impl()
        {
            cleanup();
        }

        bool initialize(const ElegooLink::Config &config)
        {
            if (initialized_)
            {
                ELEGOO_LOG_WARN("DirectImpl already initialized");
                return true;
            }
            config_ = config;

            Logger::getInstance().initialize(
                LogConfig{
                    static_cast<LogLevel>(config.log.logLevel),
                    config.log.logEnableConsole,
                    config.log.logEnableFile,
                    config.log.logFileName,
                    config.log.logMaxFileSize,
                    config.log.logMaxFiles});

            LanService::Config localConfig;
            localConfig.staticWebPath = config.local.staticWebPath;

            if (LanService::getInstance().initialize(localConfig))
            {
                ELEGOO_LOG_INFO("Local service initialized successfully");
            }
            else
            {
                ELEGOO_LOG_ERROR("Failed to initialize local service");
                return false;
            }

#ifdef ENABLE_CLOUD_FEATURES
            // Initialize cloud service if enabled
            CloudService::NetworkConfig netConfig;
            netConfig.staticWebPath = config.cloud.staticWebPath;
            netConfig.region = config.cloud.region;
            netConfig.baseApiUrl = config.cloud.baseApiUrl;
            netConfig.userAgent = config.cloud.userAgent;
            netConfig.caCertPath = config.cloud.caCertPath;
            auto result = getCloudService().initialize(netConfig);
            if (result.isSuccess())
            {
                ELEGOO_LOG_INFO("Network service initialized successfully");
            }
            else
            {
                ELEGOO_LOG_ERROR("Failed to initialize cloud service: {}", result.message);
                LanService::getInstance().cleanup();
                return false;
            }
#endif

            initialized_ = true;
            return true;
        }

        void cleanup()
        {
            if (!initialized_)
            {
                return;
            }

            LanService::getInstance().cleanup();
#ifdef ENABLE_CLOUD_FEATURES
            getCloudService().cleanup();
#endif

            initialized_ = false;
        }

        bool isInitialized() const { return initialized_; }
        const ElegooLink::Config &getConfig() const { return config_; }

        bool isLocalServiceEnabled() const { return initialized_; }

        bool isNetworkServiceEnabled() const
        {
#ifdef ENABLE_CLOUD_FEATURES
            return initialized_;
#else
            return false;
#endif
        }

        bool isNetworkPrinter(const std::string &printerId) const
        {
#ifdef ENABLE_CLOUD_FEATURES
            if (initialized_)
            {
                auto printers = getCloudService().getCachedPrinters();

                auto it = std::find_if(printers.begin(), printers.end(),
                                       [&printerId](const PrinterInfo &info)
                                       {
                                           return info.printerId == printerId;
                                       });
                return it != printers.end();
            }
#endif
            return false;
        }

        bool isLocalPrinter(const std::string &printerId) const
        {
            auto printers = LanService::getInstance().getCachedPrinters();

            auto it = std::find_if(printers.begin(), printers.end(),
                                   [&printerId](const PrinterInfo &info)
                                   {
                                       return info.printerId == printerId;
                                   });
            return it != printers.end();
        }

        void setupEventForwarding(EventBus &targetBus)
        {
            LanService::getInstance().setEventCallback(
                [&targetBus](const BizEvent &event)
                {
                    targetBus.publishFromEvent(event);
                    return 0;
                });

#ifdef ENABLE_CLOUD_FEATURES
            // Setup CloudService event forwarding
            getCloudService().setEventCallback(
                [&targetBus](const BizEvent &event)
                {
                    targetBus.publishFromEvent(event);
                    return 0;
                });
#endif
        }

        void teardownEventForwarding()
        {
            // Clear LanService event callback
            LanService::getInstance().setEventCallback(nullptr);

#ifdef ENABLE_CLOUD_FEATURES
            // Clear CloudService event callback
            getCloudService().setEventCallback(nullptr);
#endif
        }

        bool shouldUseNetworkService(const ConnectPrinterParams &params) const
        {
            // If connection type is specified, use it
            if (params.networkMode == NetworkMode::CLOUD)
            {
                return true;
            }
            else if (params.networkMode == NetworkMode::LAN)
            {
                return false;
            }

            // Default: prefer local service if both are available
            return false;
        }

    private:
        ElegooLink::Config config_;
        bool initialized_;
    };

    // ========== ElegooLink Implementation ==========

    ElegooLink::ElegooLink() : pImpl_(std::make_unique<Impl>())
    {
    }

    ElegooLink::~ElegooLink()
    {
        cleanup();
    }

    ElegooLink &ElegooLink::getInstance()
    {
        static ElegooLink instance;
        return instance;
    }

    bool ElegooLink::initialize(const Config &config)
    {
        bool wasInitialized = pImpl_->isInitialized();
        bool ok = pImpl_->initialize(config);

        // Setup event forwarding after initialization
        if (ok && !wasInitialized)
        {
            pImpl_->setupEventForwarding(eventBus_);
        }

        return ok;
    }

    void ElegooLink::cleanup()
    {
        if (!pImpl_->isInitialized())
        {
            return;
        }
        // Teardown event forwarding before cleanup
        pImpl_->teardownEventForwarding();
        pImpl_->cleanup();
    }

    bool ElegooLink::isInitialized() const
    {
        return pImpl_->isInitialized();
    }

    // ========== Local Printer Discovery ==========

    BizResult<PrinterDiscoveryData> ElegooLink::startPrinterDiscovery(const PrinterDiscoveryParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return BizResult<PrinterDiscoveryData>::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "Local service is not enabled");
        }
        return LanService::getInstance().startPrinterDiscovery(params);
    }

    VoidResult ElegooLink::startPrinterDiscoveryAsync(
        const PrinterDiscoveryParams &params,
        std::function<void(const PrinterInfo &)> discoveredCallback,
        std::function<void(const std::vector<PrinterInfo> &)> completionCallback)
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "Local service is not enabled");
        }
        return LanService::getInstance().startPrinterDiscoveryAsync(
            params, discoveredCallback, completionCallback);
    }

    VoidResult ElegooLink::stopPrinterDiscovery()
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "Local service is not enabled");
        }
        return LanService::getInstance().stopPrinterDiscovery();
    }

    std::vector<PrinterInfo> ElegooLink::getDiscoveredPrinters() const
    {
        if (!pImpl_->isInitialized())
        {
            return {};
        }
        return LanService::getInstance().getDiscoveredPrinters();
    }

    // ========== Printer Connection Management ==========

    ConnectPrinterResult ElegooLink::connectPrinter(const ConnectPrinterParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return ConnectPrinterResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
        if (pImpl_->shouldUseNetworkService(params))
        {
#ifdef ENABLE_CLOUD_FEATURES
            return getCloudService().connectPrinter(params);
#else
            return ConnectPrinterResult::Error(
                ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
                "Cloud service is not enabled");
#endif
        }
        else
        {
            return LanService::getInstance().connectPrinter(params);
        }
    }

    VoidResult ElegooLink::disconnectPrinter(const std::string &printerId)
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }

        if (pImpl_->isLocalPrinter(printerId))
        {
            return LanService::getInstance().disconnectPrinter(printerId);
        }

#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().disconnectPrinter({printerId});
#else
        return VoidResult::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    GetPrinterListResult ElegooLink::getPrinters()
    {
        std::vector<PrinterInfo> allPrinters;

        // Get printers from local service
        auto localResult = LanService::getInstance().getPrinters();
        if (localResult.isSuccess())
        {
            allPrinters.insert(allPrinters.end(),
                               localResult.value().printers.begin(),
                               localResult.value().printers.end());
        }

        // Get printers from network service
#ifdef ENABLE_CLOUD_FEATURES
        auto networkResult = getCloudService().getPrinters();
        if (networkResult.isSuccess())
        {
            allPrinters.insert(allPrinters.end(),
                               networkResult.value().printers.begin(),
                               networkResult.value().printers.end());
        }
        else
        {
            return GetPrinterListResult::Error(
                networkResult.code,
                networkResult.message);
        }
#endif

        GetPrinterListData data;
        data.printers = std::move(allPrinters);
        return GetPrinterListResult::Ok(std::move(data));
    }

    bool ElegooLink::isPrinterConnected(const std::string &printerId) const
    {
        if (pImpl_->isLocalPrinter(printerId))
        {
            return LanService::getInstance().isPrinterConnected(printerId);
        }

        if (pImpl_->isNetworkPrinter(printerId))
        {
            return true;
        }

        return false;
    }

#ifdef ENABLE_CLOUD_FEATURES
    // ========== Network/Cloud Service Functions ==========

    VoidResult ElegooLink::setRegion(const SetRegionParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().setRegion(params);
#else
        return VoidResult::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    GetUserInfoResult ElegooLink::getUserInfo(const GetUserInfoParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return GetUserInfoResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().getUserInfo(params);
#else
        return GetUserInfoResult::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    VoidResult ElegooLink::setHttpCredential(const HttpCredential &credential)
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().setHttpCredential(credential);
#else
        return VoidResult::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    BizResult<HttpCredential> ElegooLink::getHttpCredential() const
    {
        if (!pImpl_->isInitialized())
        {
            return BizResult<HttpCredential>::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().getHttpCredential();
#else
        return BizResult<HttpCredential>::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    BizResult<HttpCredential> ElegooLink::refreshHttpCredential(const HttpCredential &credential)
    {
        if (!pImpl_->isInitialized())
        {
            return BizResult<HttpCredential>::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().refreshHttpCredential(credential);
#else
        return BizResult<HttpCredential>::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    VoidResult ElegooLink::clearHttpCredential()
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().clearHttpCredential();
#else
        return VoidResult::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    VoidResult ElegooLink::logout()
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().logout();
#else
        return VoidResult::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    GetRtcTokenResult ElegooLink::getRtcToken() const
    {
        if (!pImpl_->isInitialized())
        {
            return GetRtcTokenResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().getRtcToken();
#else
        return GetRtcTokenResult::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    VoidResult ElegooLink::sendRtmMessage(const SendRtmMessageParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().sendRtmMessage(params);
#else
        return VoidResult::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    BindPrinterResult ElegooLink::bindPrinter(const BindPrinterParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return BindPrinterResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().bindPrinter(params);
#else
        return BindPrinterResult::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    VoidResult ElegooLink::cancelBindPrinter(const CancelBindPrinterParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().cancelBindPrinter(params);
#else
        return VoidResult::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    VoidResult ElegooLink::unbindPrinter(const UnbindPrinterParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().unbindPrinter(params);
#else
        return VoidResult::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }
#endif // ENABLE_CLOUD_FEATURES
    // ========== File Management ==========

    GetFileListResult ElegooLink::getFileList(const GetFileListParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return GetFileListResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().getFileList(params);
#else
        return GetFileListResult::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    GetFileDetailResult ElegooLink::getFileDetail(const GetFileDetailParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return GetFileDetailResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().getFileDetail(params);
#else
        return GetFileDetailResult::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    FileUploadResult ElegooLink::uploadFile(
        const FileUploadParams &params,
        FileUploadProgressCallback progressCallback)
    {
        if (!pImpl_->isInitialized())
        {
            return FileUploadResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }

        if (pImpl_->isNetworkPrinter(params.printerId))
        {
#ifdef ENABLE_CLOUD_FEATURES
            return getCloudService().uploadFile(params, progressCallback);
#else
            return FileUploadResult::Error(
                ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
                "Cloud service is not enabled");
#endif
        }
        return LanService::getInstance().uploadFile(params, progressCallback);
    }

    // ========== Print Task Management ==========

    PrintTaskListResult ElegooLink::getPrintTaskList(const PrintTaskListParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return PrintTaskListResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().getPrintTaskList(params);
#else
        return PrintTaskListResult::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    DeletePrintTasksResult ElegooLink::deletePrintTasks(const DeletePrintTasksParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return DeletePrintTasksResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().deletePrintTasks(params);
#else
        return DeletePrintTasksResult::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    StartPrintResult ElegooLink::startPrint(const StartPrintParams &params)
    {
        // Try to determine which service manages this printer
        if (!pImpl_->isInitialized())
        {
            return StartPrintResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }

        if (pImpl_->isNetworkPrinter(params.printerId))
        {
#ifdef ENABLE_CLOUD_FEATURES
            return getCloudService().startPrint(params);
#else
            return StartPrintResult::Error(
                ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
                "Cloud service is not enabled");
#endif
        }

        return LanService::getInstance().startPrint(params);
    }

    VoidResult ElegooLink::pausePrint(const PausePrintParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }

        return LanService::getInstance().pausePrint(params);
    }

    VoidResult ElegooLink::resumePrint(const ResumePrintParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }

        return LanService::getInstance().resumePrint(params);
    }

    VoidResult ElegooLink::stopPrint(const StopPrintParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }

        return LanService::getInstance().stopPrint(params);
    }

    // ========== Printer Status and Control ==========

    PrinterAttributesResult ElegooLink::getPrinterAttributes(const PrinterAttributesParams &params, int timeout)
    {
        if (!pImpl_->isInitialized())
        {
            return PrinterAttributesResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
        // Try network service first
        if (pImpl_->isNetworkPrinter(params.printerId))
        {
#ifdef ENABLE_CLOUD_FEATURES
            return getCloudService().getPrinterAttributes(params);
#else
            return PrinterAttributesResult::Error(
                ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
                "Cloud service is not enabled");
#endif
        }
        return LanService::getInstance().getPrinterAttributes(params, timeout);
    }

    PrinterStatusResult ElegooLink::getPrinterStatus(const PrinterStatusParams &params, int timeout)
    {
        if (!pImpl_->isInitialized())
        {
            return PrinterStatusResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }

        // Check network printer first
        if (pImpl_->isNetworkPrinter(params.printerId))
        {
#ifdef ENABLE_CLOUD_FEATURES
            return getCloudService().getPrinterStatus(params);
#else
            return PrinterStatusResult::Error(
                ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
                "Cloud service is not enabled");
#endif
        }

        return LanService::getInstance().getPrinterStatus(params, timeout);
    }

    VoidResult ElegooLink::refreshPrinterAttributes(const PrinterAttributesParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }

        // Check network printer first
        if (pImpl_->isNetworkPrinter(params.printerId))
        {
#ifdef ENABLE_CLOUD_FEATURES
            return getCloudService().refreshPrinterAttributes(params);
#else
            return VoidResult::Error(
                ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
                "Cloud service is not enabled");
#endif
        }

        return LanService::getInstance().refreshPrinterAttributes(params);
    }

    VoidResult ElegooLink::refreshPrinterStatus(const PrinterStatusParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }

        // Check network printer first
        if (pImpl_->isNetworkPrinter(params.printerId))
        {
#ifdef ENABLE_CLOUD_FEATURES
            return getCloudService().refreshPrinterStatus(params);
#else
            return VoidResult::Error(
                ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
                "Cloud service is not enabled");
#endif
        }
        return LanService::getInstance().refreshPrinterStatus(params);
    }

    GetCanvasStatusResult ElegooLink::getCanvasStatus(const GetCanvasStatusParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return GetCanvasStatusResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }

        // Check network printer first
        if (pImpl_->isNetworkPrinter(params.printerId))
        {
#ifdef ENABLE_CLOUD_FEATURES
            return getCloudService().getCanvasStatus(params);
#else
            return GetCanvasStatusResult::Error(
                ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
                "Cloud service is not enabled");
#endif
        }

        return LanService::getInstance().getCanvasStatus(params);
    }

    VoidResult ElegooLink::setAutoRefill(const SetAutoRefillParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }

        // Check network printer first
        if (pImpl_->isNetworkPrinter(params.printerId))
        {
#ifdef ENABLE_CLOUD_FEATURES
            return getCloudService().setAutoRefill(params);
#else
            return VoidResult::Error(
                ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
                "Cloud service is not enabled");
#endif
        }
        return LanService::getInstance().setAutoRefill(params);
    }

    VoidResult ElegooLink::updatePrinterName(const UpdatePrinterNameParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return VoidResult::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }

        if (pImpl_->isNetworkPrinter(params.printerId))
        {
#ifdef ENABLE_CLOUD_FEATURES
            return getCloudService().updatePrinterName(params);
#else
            return VoidResult::Error(
                ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
                "Cloud service is not enabled");
#endif
        }

        return LanService::getInstance().updatePrinterName(params);
    }

    BizResult<std::string> ElegooLink::getPrinterStatusRaw(const PrinterStatusParams &params)
    {
        if (!pImpl_->isInitialized())
        {
            return BizResult<std::string>::Error(
                ELINK_ERROR_CODE::NOT_INITIALIZED,
                "ElegooLink is not initialized");
        }
#ifdef ENABLE_CLOUD_FEATURES
        return getCloudService().getPrinterStatusRaw(params);
#else
        return BizResult<std::string>::Error(
            ELINK_ERROR_CODE::OPERATION_NOT_IMPLEMENTED,
            "Cloud service is not enabled");
#endif
    }

    // ========== Event Management ==========

    void ElegooLink::clearAllEventSubscriptions()
    {
        eventBus_.clear();
    }

    // ========== Utility Functions ==========

    std::string ElegooLink::getVersion() const
    {
        return ELEGOO_LINK_VERSION_STRING;
    }

    std::vector<PrinterType> ElegooLink::getSupportedPrinterTypes() const
    {
        if (pImpl_->isLocalServiceEnabled())
        {
            return LanService::getInstance().getSupportedPrinterTypes();
        }
        return {};
    }

    bool ElegooLink::isLocalServiceEnabled() const
    {
        return pImpl_->isLocalServiceEnabled();
    }

    bool ElegooLink::isNetworkServiceEnabled() const
    {
        return pImpl_->isNetworkServiceEnabled();
    }

} // namespace elink
