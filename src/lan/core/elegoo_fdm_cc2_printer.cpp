#include "core/elegoo_fdm_cc2_printer.h"
#include "adapters/elegoo_cc2_adapters.h"
#include "utils/logger.h"
#include "utils/utils.h"

namespace elink
{
    ElegooFdmCC2Printer::ElegooFdmCC2Printer(const PrinterInfo &printerInfo)
        : BasePrinter(printerInfo)
    {
    }

    std::unique_ptr<IProtocol> ElegooFdmCC2Printer::createProtocol()
    {
        return std::make_unique<ElegooCC2MqttProtocol>();
    }

    std::unique_ptr<IMessageAdapter> ElegooFdmCC2Printer::createMessageAdapter()
    {
        return std::make_unique<ElegooFdmCC2MessageAdapter>(printerInfo_);
    }

    std::unique_ptr<IHttpFileTransfer> ElegooFdmCC2Printer::createFileUploader()
    {
        return std::make_unique<ElegooFdmCC2HttpTransfer>();
    }

    void ElegooFdmCC2Printer::onConnected(const ConnectPrinterParams &params)
    {
        // Call base class implementation first
        BasePrinter::onConnected(params);

        // Reset status event sequence for CC2 printer
        resetStatusSequence();
    }

    void ElegooFdmCC2Printer::resetStatusSequence()
    {
        if (adapter_)
        {
            auto elegooAdapter = dynamic_cast<ElegooFdmCC2MessageAdapter *>(adapter_.get());
            if (elegooAdapter)
            {
                elegooAdapter->resetStatusSequence();
                ELEGOO_LOG_DEBUG("Reset status event sequence for ElegooFdmCC2 printer {}", 
                               StringUtils::maskString(printerInfo_.printerId));
            }
            else
            {
                ELEGOO_LOG_WARN("Failed to cast adapter to ElegooFdmCC2MessageAdapter for printer {}", 
                              StringUtils::maskString(printerInfo_.printerId));
            }
        }
    }

} // namespace elink
