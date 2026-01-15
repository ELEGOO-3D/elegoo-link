#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include "type.h"
#include "events/event_system.h"
#include "elegoo_export.h"
#include "config.h"
namespace elink
{
    /**
     * ElegooLink - Unified SDK Interface
     *
     * This class provides a unified interface that integrates both local and remote printer management.
     * It combines the functionality of LanService (for local printer discovery and control)
     * and CloudService (for cloud-based printer management).
     *
     * Main features:
     * 1. Local printer discovery and connection (LAN)
     * 2. Remote printer management through cloud services
     * 3. File upload and print control
     * 4. Printer status monitoring
     * 5. Event-based notifications
     * 6. Unified configuration management
     */
    class ELEGOO_LINK_API ElegooLink
    {
    public:
        /**
         * ElegooLink initialization configuration
         */
        using Config = ElegooLinkConfig;

        /**
         * File upload progress callback function type
         * @param progress Progress data
         * @return Return false to cancel upload, true to continue
         */
        using FileUploadProgressCallback = std::function<bool(const FileUploadProgressData &progress)>;

        /**
         * Event subscription ID type
         */
        using EventSubscriptionId = EventBus::EventId;

    public:
        /**
         * Get singleton instance
         * @return Reference to ElegooLink singleton
         */
        static ElegooLink &getInstance();

        /**
         * Destructor
         */
        ~ElegooLink();

        // Disable copy constructor and assignment
        ElegooLink(const ElegooLink &) = delete;
        ElegooLink &operator=(const ElegooLink &) = delete;

        /**
         * Initialize ElegooLink
         * @param config Configuration parameters
         * @return true if successful
         */
        bool initialize(const Config &config = Config());

        /**
         * Cleanup resources
         */
        void cleanup();

        /**
         * Check if initialized
         * @return true if initialized
         */
        bool isInitialized() const;

        // ========== Local Printer Discovery (LAN) ==========

        /**
         * Start local printer discovery
         * @param params Discovery parameters
         * @return Discovery result
         */
        BizResult<PrinterDiscoveryData> startPrinterDiscovery(const PrinterDiscoveryParams &params);

        /**
         * Start printer discovery asynchronously
         * @param params Discovery parameters
         * @param discoveredCallback Callback for each discovered printer
         * @param completionCallback Callback when discovery completes
         * @return Operation result
         */
        VoidResult startPrinterDiscoveryAsync(
            const PrinterDiscoveryParams &params,
            std::function<void(const PrinterInfo &)> discoveredCallback,
            std::function<void(const std::vector<PrinterInfo> &)> completionCallback);

        /**
         * Stop printer discovery
         * @return Operation result
         */
        VoidResult stopPrinterDiscovery();

        /**
         * Get list of discovered printers (unregistered printers)
         * @return Printer list
         */
        std::vector<PrinterInfo> getDiscoveredPrinters() const;

        // ========== Printer Connection Management ==========

        /**
         * Connect to a printer (local or remote based on parameters)
         * @param params Connection parameters
         * @return Connection result
         */
        ConnectPrinterResult connectPrinter(const ConnectPrinterParams &params);

        /**
         * Disconnect from a printer
         * @param printerId Printer ID
         * @return Operation result
         */
        VoidResult disconnectPrinter(const std::string &printerId);

        /**
         * Get list of connected printers
         * @return Printer list result
         */
        GetPrinterListResult getPrinters();

        /**
         * Check if printer is connected
         * @param printerId Printer ID
         * @return true if connected
         */
        bool isPrinterConnected(const std::string &printerId) const;

#ifdef ENABLE_CLOUD_FEATURES
        // ========== Network/Cloud Service Functions ==========

        /**
         * Set region for network service
         * @param params Region parameters
         * @return Operation result
         */
        VoidResult setRegion(const SetRegionParams &params);

        /**
         * Get user information
         * @param params User info parameters
         * @return User info result
         */
        GetUserInfoResult getUserInfo(const GetUserInfoParams &params);

        /**
         * Set HTTP credential for network service
         * @param credential HTTP credential
         * @return Operation result
         */
        VoidResult setHttpCredential(const HttpCredential &credential);

        /**
         * Get current HTTP credential
         * @return HTTP credential result
         */
        BizResult<HttpCredential> getHttpCredential() const;

        /**
         * Refresh HTTP credential
         * @param credential Current credential
         * @return New credential result
         */
        BizResult<HttpCredential> refreshHttpCredential(const HttpCredential &credential);

        /**
         * Clear HTTP credential
         * @return Operation result
         */
        VoidResult clearHttpCredential();

        VoidResult logout();
        /**
         * Get RTC token for real-time communication
         * @return RTC token result
         */
        GetRtcTokenResult getRtcToken() const;

        /**
         * Send RTM message
         * @param params RTM message parameters
         * @return Operation result
         */
        VoidResult sendRtmMessage(const SendRtmMessageParams &params);

        /**
         * Bind printer to account
         * @param params Bind parameters
         * @return Bind result
         */
        BindPrinterResult bindPrinter(const BindPrinterParams &params);

        /**
         * Cancel ongoing bind printer operation
         * @param params Cancel bind parameters
         * @return Operation result
         */
        VoidResult cancelBindPrinter(const CancelBindPrinterParams &params);

        /**
         * Unbind printer from account
         * @param params Unbind parameters
         * @return Operation result
         */
        VoidResult unbindPrinter(const UnbindPrinterParams &params);

        /**
         * Get list of devices with expired Agora license
         * @return Result containing list of expired devices
         */
        GetLicenseExpiredDevicesResult getLicenseExpiredDevices();

        /**
         * Renew Agora license for a device
         * @param params Renew parameters (serialNumber is required)
         * @return Operation result
         */
        RenewLicenseResult renewLicense(const RenewLicenseParams &params);
#endif
        // ========== File Management ==========

        /**
         * Get file list from printer or cloud
         * @param params File list parameters
         * @return File list result
         */
        GetFileListResult getFileList(const GetFileListParams &params);

        /**
         * Get file details
         * @param params File detail parameters
         * @return File detail result
         */
        GetFileDetailResult getFileDetail(const GetFileDetailParams &params);

        /**
         * Upload file to printer
         * @param params Upload parameters
         * @param progressCallback Progress callback
         * @return Upload completion result
         */
        FileUploadResult uploadFile(
            const FileUploadParams &params,
            FileUploadProgressCallback progressCallback = nullptr);

        // ========== Print Task Management ==========

        /**
         * Get print task list
         * @param params Task list parameters
         * @return Task list result
         */
        PrintTaskListResult getPrintTaskList(const PrintTaskListParams &params);

        /**
         * Delete print tasks
         * @param params Delete parameters
         * @return Delete result
         */
        DeletePrintTasksResult deletePrintTasks(const DeletePrintTasksParams &params);

        /**
         * Start print
         * @param params Print parameters
         * @return Start print result
         */
        StartPrintResult startPrint(const StartPrintParams &params);

        /**
         * Pause print
         * @param params Pause parameters
         * @return Operation result
         */
        VoidResult pausePrint(const PausePrintParams &params);

        /**
         * Resume print
         * @param params Resume parameters
         * @return Operation result
         */
        VoidResult resumePrint(const ResumePrintParams &params);

        /**
         * Stop print
         * @param params Stop parameters
         * @return Operation result
         */
        VoidResult stopPrint(const StopPrintParams &params);

        // ========== Printer Status and Control ==========

        /**
         * Get printer attributes
         * @param params Attribute parameters
         * @param timeout Timeout in milliseconds
         * @return Attributes result
         */
        PrinterAttributesResult getPrinterAttributes(const PrinterAttributesParams &params, int timeout = 3000);

        /**
         * Get printer status
         * @param params Status parameters
         * @param timeout Timeout in milliseconds
         * @return Status result
         */
        PrinterStatusResult getPrinterStatus(const PrinterStatusParams &params, int timeout = 3000);

        /**
         * Refresh printer attributes (async, result via event)
         * @param params Attribute parameters
         * @return Operation result
         */
        VoidResult refreshPrinterAttributes(const PrinterAttributesParams &params);

        /**
         * Refresh printer status (async, result via event)
         * @param params Status parameters
         * @return Operation result
         */
        VoidResult refreshPrinterStatus(const PrinterStatusParams &params);

        /**
         * Get canvas status
         * @param params Canvas status parameters
         * @return Canvas status result
         */
        GetCanvasStatusResult getCanvasStatus(const GetCanvasStatusParams &params);

        /**
         * Set auto refill
         * @param params Auto refill parameters
         * @return Operation result
         */
        VoidResult setAutoRefill(const SetAutoRefillParams &params);

        /**
         * Update printer name
         * @param params Update parameters
         * @return Operation result
         */
        VoidResult updatePrinterName(const UpdatePrinterNameParams &params);

        /**
         * Get printer status raw data
         * @param params Status parameters
         * @return Raw status string result
         */
        BizResult<std::string> getPrinterStatusRaw(const PrinterStatusParams &params);

        // ========== Event Management ==========

        /**
         * Subscribe to strongly-typed events
         * @tparam EventType Event type
         * @param handler Event handler function
         * @return Subscription ID for unsubscribing
         */
        template <typename EventType>
        EventSubscriptionId subscribeEvent(
            std::function<void(const std::shared_ptr<EventType> &)> handler)
        {
            return eventBus_.subscribe<EventType>(handler);
        }

        /**
         * Unsubscribe from event
         * @tparam EventType Event type
         * @param id Subscription ID
         * @return true if unsubscribed successfully
         */
        template <typename EventType>
        bool unsubscribeEvent(EventSubscriptionId id)
        {
            return eventBus_.unsubscribe<EventType>(id);
        }

        /**
         * Clear all event subscriptions
         */
        void clearAllEventSubscriptions();

        // ========== Utility Functions ==========

        /**
         * Get version information
         * @return Version string
         */
        std::string getVersion() const;

        /**
         * Get list of supported printer types
         * @return Supported printer types
         */
        std::vector<PrinterType> getSupportedPrinterTypes() const;

        /**
         * Check if local service is enabled
         * @return true if enabled
         */
        bool isLocalServiceEnabled() const;

        /**
         * Check if network service is enabled
         * @return true if enabled
         */
        bool isNetworkServiceEnabled() const;

    private:
        /**
         * Private constructor (singleton pattern)
         */
        ElegooLink();

    private:
        // Event bus (needs to be accessible in template methods)
        EventBus eventBus_;

        // Pimpl idiom to hide implementation details
        class Impl;
        std::unique_ptr<Impl> pImpl_;
    };

} // namespace elink
