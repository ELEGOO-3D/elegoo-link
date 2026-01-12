#pragma once

#include "core/base_printer.h"

namespace elink
{
    /**
     * ElegooFdmCC2Printer - Printer implementation for Elegoo FDM CC2
     * Handles CC2-specific features like status sequence management
     */
    class ElegooFdmCC2Printer : public BasePrinter
    {
    public:
        explicit ElegooFdmCC2Printer(const PrinterInfo &printerInfo);
        virtual ~ElegooFdmCC2Printer() = default;

    protected:
        /**
         * Override: Perform CC2-specific initialization after connection
         */
        void onConnected(const ConnectPrinterParams &params) override;

        /**
         * Override: Create MQTT protocol for CC2
         */
        std::unique_ptr<IProtocol> createProtocol() override;

        /**
         * Override: Create CC2 message adapter
         */
        std::unique_ptr<IMessageAdapter> createMessageAdapter() override;

        /**
         * Override: Create CC2 file uploader
         */
        std::unique_ptr<IHttpFileTransfer> createFileUploader() override;

    private:
        /**
         * Reset status sequence for CC2 message adapter
         */
        void resetStatusSequence();
    };

} // namespace elink
