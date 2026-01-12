#pragma once
#include "type.h"
#include "utils/utils.h"
#include <algorithm>
#include "types/internal/internal.h"
namespace elink
{

    static PrinterInfo generatePrinterInfo(const std::string &serialNumber, const std::string &model, const std::string &name = "")
    {
        PrinterInfo printer;
        printer.serialNumber = serialNumber;
        printer.name = name.empty() ? model : name;
        printer.model = model;
        printer.printerType = printerModelToPrinterType(model);
        printer.mainboardId = printer.serialNumber;
        printer.printerId = PRINTER_ID_PREFIX_ELEGOO_CLOUD + printer.serialNumber;
        printer.brand = "Elegoo";
        printer.manufacturer = "Elegoo";
        printer.authMode = "pinCode";
        printer.networkMode = NetworkMode::CLOUD;

        // auto moduleDir = FileUtils::getCurrentModuleDirectory();
        // if (moduleDir.empty())
        // {
        //     return printer;
        // }

        // if (moduleDir.back() != '/' && moduleDir.back() != '\\')
        // {
        //     moduleDir += '/';
        // }
        // auto htmlPath = moduleDir + "web/index.html";

        auto htmlPath = cloudStaticWebPath();
        if (!htmlPath.empty())
        {
            // Convert backslashes to forward slashes for URL compatibility
            std::replace(htmlPath.begin(), htmlPath.end(), '\\', '/');
            // Ensure no trailing slash
            if (htmlPath.back() == '/')
            {
                htmlPath.pop_back();
            }
        }
        htmlPath = htmlPath + "/cloud_service_web/index.html";

        printer.webUrl = "file:///" + htmlPath;
        return printer;
    }
}
