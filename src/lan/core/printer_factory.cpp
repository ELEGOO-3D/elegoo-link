#include "core/printer_factory.h"
#include "core/base_printer.h"
#include "core/elegoo_fdm_cc2_printer.h"
#include "core/elegoo_fdm_cc_printer.h"
#include "core/generic_moonraker_printer.h"
#include "utils/logger.h"
#include "utils/utils.h"

namespace elink
{
    std::shared_ptr<BasePrinter> PrinterFactory::createPrinter(const PrinterInfo &printerInfo)
    {
        try
        {
            std::shared_ptr<BasePrinter> printer;
            
            switch (printerInfo.printerType)
            {
            case PrinterType::ELEGOO_FDM_CC2:
                ELEGOO_LOG_DEBUG("Creating ElegooFdmCC2Printer for printer {}", 
                               StringUtils::maskString(printerInfo.printerId));
                printer = std::make_shared<ElegooFdmCC2Printer>(printerInfo);
                break;

            case PrinterType::ELEGOO_FDM_CC:
                ELEGOO_LOG_DEBUG("Creating ElegooFdmCCPrinter for printer {}", 
                               StringUtils::maskString(printerInfo.printerId));
                printer = std::make_shared<ElegooFdmCCPrinter>(printerInfo);
                break;

            case PrinterType::ELEGOO_FDM_KLIPPER:
            case PrinterType::GENERIC_FDM_KLIPPER:
                ELEGOO_LOG_DEBUG("Creating GenericMoonrakerPrinter for Klipper printer {}", 
                               StringUtils::maskString(printerInfo.printerId));
                printer = std::make_shared<GenericMoonrakerPrinter>(printerInfo);
                break;

            case PrinterType::UNKNOWN:
            default:
                ELEGOO_LOG_ERROR("Unsupported printer type: {} for printer {}",
                               static_cast<int>(printerInfo.printerType),
                               StringUtils::maskString(printerInfo.printerId));
                return nullptr;
            }
            
            // Initialize the printer after construction (calls virtual functions safely)
            if (printer)
            {
                printer->initialize();
            }
            
            return printer;
        }
        catch (const std::exception &e)
        {
            ELEGOO_LOG_ERROR("Failed to create printer for {}: {}",
                           StringUtils::maskString(printerInfo.printerId), e.what());
            return nullptr;
        }
    }

} // namespace elink
