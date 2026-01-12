#pragma once

// Legacy header - redirects to new base_printer.h
// This file is kept for backward compatibility
// New code should include base_printer.h, elegoo_fdm_cc2_printer.h, etc.

#include "core/base_printer.h"
#include "core/elegoo_fdm_cc2_printer.h"
#include "core/elegoo_fdm_cc_printer.h"
#include "core/generic_moonraker_printer.h"
#include "core/printer_factory.h"

namespace elink
{
    // For backward compatibility, use BasePrinter as Printer
    using Printer = BasePrinter;

    /**
     * Printer smart pointer type definition
     */
    using PrinterPtr = std::shared_ptr<Printer>;

} // namespace elink
