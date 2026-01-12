#pragma once

#include "core/base_printer.h"

namespace elink
{
    /**
     * ElegooFdmCCPrinter - Printer implementation for Elegoo FDM CC
     * Handles CC-specific features
     */
    class ElegooFdmCCPrinter : public BasePrinter
    {
    public:
        explicit ElegooFdmCCPrinter(const PrinterInfo &printerInfo);
        virtual ~ElegooFdmCCPrinter() = default;

    protected:
        /**
         * Override: Create MQTT protocol for CC
         */
        std::unique_ptr<IProtocol> createProtocol() override;

        /**
         * Override: Create CC message adapter
         */
        std::unique_ptr<IMessageAdapter> createMessageAdapter() override;

        /**
         * Override: Create CC file uploader
         */
        std::unique_ptr<IHttpFileTransfer> createFileUploader() override;
    };

} // namespace elink
