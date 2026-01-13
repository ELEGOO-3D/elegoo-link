#include "core/generic_moonraker_printer.h"
#include "adapters/generic_moonraker_adapters.h"
#include "utils/logger.h"
#include "utils/utils.h"

namespace elink
{
    GenericMoonrakerPrinter::GenericMoonrakerPrinter(const PrinterInfo &printerInfo)
        : BasePrinter(printerInfo)
    {
        ELEGOO_LOG_INFO("Initialized GenericMoonrakerPrinter for printer {}",
                        StringUtils::maskString(printerInfo.printerId));
    }

    std::unique_ptr<IProtocol> GenericMoonrakerPrinter::createProtocol()
    {
        return std::make_unique<GenericMoonrakerProtocol>();
    }

    std::unique_ptr<IMessageAdapter> GenericMoonrakerPrinter::createMessageAdapter()
    {
        return std::make_unique<GenericMoonrakerMessageAdapter>(printerInfo_);
    }

    std::unique_ptr<IHttpFileTransfer> GenericMoonrakerPrinter::createFileUploader()
    {
        return std::make_unique<GenericMoonrakerHttpTransfer>();
    }

    VoidResult GenericMoonrakerPrinter::startPrint(const StartPrintParams &params)
    {
        executeRequest<std::monostate>(
            MethodType::START_PRINT,
            params,
            "Starting print",
            std::chrono::milliseconds(1000));

        // Starting print takes a long time, possibly several minutes, so we don't wait and return success immediately
        return VoidResult::Success();
    }
} // namespace elink
