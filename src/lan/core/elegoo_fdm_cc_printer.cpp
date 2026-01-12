#include "core/elegoo_fdm_cc_printer.h"
#include "adapters/elegoo_cc_adapters.h"
#include "utils/logger.h"
#include "utils/utils.h"

namespace elink
{
    ElegooFdmCCPrinter::ElegooFdmCCPrinter(const PrinterInfo &printerInfo)
        : BasePrinter(printerInfo)
    {
    }

    std::unique_ptr<IProtocol> ElegooFdmCCPrinter::createProtocol()
    {
        return std::make_unique<ElegooFdmCCProtocol>();
    }

    std::unique_ptr<IMessageAdapter> ElegooFdmCCPrinter::createMessageAdapter()
    {
        return std::make_unique<ElegooFdmCCMessageAdapter>(printerInfo_);
    }

    std::unique_ptr<IHttpFileTransfer> ElegooFdmCCPrinter::createFileUploader()
    {
        return std::make_unique<ElegooFdmCCHttpTransfer>();
    }

} // namespace elink
