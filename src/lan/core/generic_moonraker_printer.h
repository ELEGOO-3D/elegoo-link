#pragma once

#include "core/base_printer.h"

namespace elink
{
    /**
     * GenericMoonrakerPrinter - Printer implementation for Generic Moonraker/Klipper printers
     * Handles Moonraker-specific features and Klipper printer control
     */
    class GenericMoonrakerPrinter : public BasePrinter
    {
    public:
        explicit GenericMoonrakerPrinter(const PrinterInfo &printerInfo);
        virtual ~GenericMoonrakerPrinter() = default;

        virtual VoidResult startPrint(const StartPrintParams &params) override;
    protected:
        /**
         * Override: Create WebSocket protocol for Moonraker
         */
        std::unique_ptr<IProtocol> createProtocol() override;

        /**
         * Override: Create Moonraker message adapter
         */
        std::unique_ptr<IMessageAdapter> createMessageAdapter() override;

        /**
         * Override: Create Moonraker file uploader
         */
        std::unique_ptr<IHttpFileTransfer> createFileUploader() override;
    };

} // namespace elink
