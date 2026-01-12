# Elegoo Link SDK

[![Version](https://img.shields.io/badge/version-1.3.6-blue.svg)](https://github.com/elegoo/elegoo-link)
[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](README.md)

**Elegoo Link** is designed to control and manage Elegoo 3D printers. It provides both local (LAN) and cloud-based printer management through a unified API interface, enabling printer discovery, connection, file upload, print control, and more.

---

## 🚀 Key Features

### Local Network (LAN) Features
- ✅ **Automatic Printer Discovery**: Local network printer discovery based on UDP broadcast and mDNS
- ✅ **Direct Connection Control**: Direct printer connection via WebSocket/MQTT protocols
- ✅ **Real-time Status Monitoring**: Get real-time information including printer temperature, print progress, fan status, etc.
- ✅ **File Transfer**: Upload local files to printer (with progress callback support)
- ✅ **Print Control**: Start, pause, resume, and stop print jobs

### Cloud Service Features (Optional)
- ☁️ **Remote Printer Access**: Access remote printers through Elegoo cloud platform

---

## 💻 System Requirements

### Development Environment
- **C++ Compiler**:
  - Windows: Visual Studio 2022 or higher
  - Linux: GCC 9+ or Clang 10+
  - macOS: Xcode 12+ (Apple Clang)
- **CMake**: 3.14 or higher
- **vcpkg**: For dependency management

### Runtime Dependencies
- paho-mqttpp3 (>= 1.5.2) - MQTT protocol support
- ixwebsocket - WebSocket client/server
- curl (>= 8.14.1) - HTTP requests
- openssl - TLS/SSL support

### Supported Printer Models
- Elegoo Centauri Carbon
- Elegoo Centauri Carbon 2
- Elegoo Neptune 4 Pro/Plus/Max
- Elegoo OrangeStorm Giga
- Other printers supporting Moonraker protocol (generic adapter)

---

## 🎯 Quick Start

### 1. Clone Repository

```bash
git clone https://github.com/elegoo/elegoo-link.git
cd elegoo-link
```

### 2. Configure Project

**Windows (PowerShell)**:
```powershell
cmake --preset windows-vcpkg
```

**Linux**:
```bash
cmake --preset linux-vcpkg
```

**macOS**:
```bash
cmake --preset macos-vcpkg
```

### 3. Build Project

**Windows**:
```powershell
cmake --build build --config Debug
# Or for Release version
cmake --build build --config Release
```

**Linux/macOS**:
```bash
cmake --build build
```

### 4. Run Example Program

```powershell
# Windows
.\build\bin\Debug\printer_connection_test.exe

# Linux/macOS
./build/bin/printer_connection_test
```

---

## 📖 API Usage Guide

### Basic Initialization

```cpp
#include "elegoo_link.h"

using namespace elink;

// Get singleton instance
auto& elegooLink = ElegooLink::getInstance();

// Configuration
ElegooLink::Config config;
config.log.logLevel = 1;  // DEBUG level
config.log.logEnableConsole = true;
config.log.logEnableFile = false;

// Initialize
if (!elegooLink.initialize(config)) {
    std::cerr << "Initialization failed!" << std::endl;
    return -1;
}
```

### Printer Discovery

```cpp
// Start discovery
PrinterDiscoveryParams discoveryParams;
auto result = elegooLink.startPrinterDiscovery(discoveryParams);

if (result.isSuccess() && result.hasValue()) {
    const auto& printers = result.value().printers;
    for (const auto& printer : printers) {
        std::cout << "Found printer: " << printer.name 
                  << " (" << printer.model << ") @ " 
                  << printer.host << std::endl;
    }
}

// Async discovery (with callbacks)
elegooLink.startPrinterDiscoveryAsync(
    discoveryParams,
    [](const PrinterInfo& printer) {
        std::cout << "Discovered: " << printer.name << std::endl;
    },
    [](const std::vector<PrinterInfo>& printers) {
        std::cout << "Discovery completed, found " << printers.size() << " printer(s)" << std::endl;
    }
);
```

### Connect to Printer

```cpp
ConnectPrinterParams connectParams;
connectParams.printerType = PrinterType::ELEGOO_FDM_CC;
connectParams.host = "192.168.1.100";
connectParams.name = "Neptune 4";
connectParams.model = "Elegoo Neptune 4";
connectParams.autoReconnect = true;

auto result = elegooLink.connectPrinter(connectParams);
if (result.isSuccess() && result.hasValue()) {
    std::string printerId = result.value().printerInfo.printerId;
    std::cout << "Connected successfully! Printer ID: " << printerId << std::endl;
}
```

### Subscribe to Events

```cpp
// Subscribe to printer status events
elegooLink.subscribeEvent<PrinterStatusEvent>(
    [](const std::shared_ptr<PrinterStatusEvent>& event) {
        auto& status = event->status;
        std::cout << "Print progress: " << status.printStatus.progress << "%" << std::endl;
        std::cout << "Extruder temp: " << status.temperatureStatus["extruder"].current << "°C" << std::endl;
    }
);

// Subscribe to connection status events
elegooLink.subscribeEvent<PrinterConnectionEvent>(
    [](const std::shared_ptr<PrinterConnectionEvent>& event) {
        if (event->connectionStatus.status == ConnectionStatus::CONNECTED) {
            std::cout << "Printer connected" << std::endl;
        }
    }
);
```

### File Upload

```cpp
FileUploadParams uploadParams;
uploadParams.printerId = printerId;
uploadParams.localFilePath = R"(C:\path\to\model.gcode)";
uploadParams.fileName = "model.gcode";
uploadParams.storageLocation = "local";
uploadParams.overwriteExisting = true;

// Progress callback
auto progressCallback = [](const FileUploadProgressData& progress) -> bool {
    std::cout << "\rUpload progress: " << progress.percentage << "%" << std::flush;
    return true;  // Return false to cancel upload
};

auto result = elegooLink.uploadFile(uploadParams, progressCallback);
if (result.isSuccess()) {
    std::cout << "\nFile uploaded successfully!" << std::endl;
}
```

### Start Print

```cpp
StartPrintParams printParams;
printParams.printerId = printerId;
printParams.fileName = "model.gcode";
printParams.storageLocation = "local";
printParams.autoBedLeveling = false;
printParams.heatedBedType = 0;  // 0=Textured high-temp plate, 1=Smooth low-temp plate
printParams.enableTimeLapse = false;

auto result = elegooLink.startPrint(printParams);
if (result.isSuccess()) {
    std::cout << "Print job started" << std::endl;
}
```

### Print Control

```cpp
// Pause print
elegooLink.pausePrint({printerId});

// Resume print
elegooLink.resumePrint({printerId});

// Stop print
elegooLink.stopPrint({printerId});
```

### Get Printer Status

```cpp
// Synchronous get
auto result = elegooLink.getPrinterStatus({printerId}, 3000);
if (result.isSuccess()) {
    auto& status = result.value();
    std::cout << "Status: " << static_cast<int>(status.printerStatus.state) << std::endl;
}

// Async refresh (result returned via events)
elegooLink.refreshPrinterStatus({printerId});
```

### Cleanup Resources

```cpp
// Disconnect
elegooLink.disconnectPrinter(printerId);

// Cleanup all resources
elegooLink.cleanup();
```

---

## 🔧 Build Options

The following options can be used during CMake configuration:

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_EXAMPLES` | OFF | Build example programs |
| `BUILD_TESTS` | OFF | Build test programs |
| `BUILD_SHARED_LIBS` | OFF | Build as shared library (DLL/SO) |
| `ENABLE_CLOUD_FEATURES` | OFF | Enable cloud service features (requires Agora SDK) |

**Example**:

```powershell
cmake --preset windows-vcpkg -DBUILD_EXAMPLES=ON -DENABLE_CLOUD_FEATURES=ON
```

### Preset Configurations

The project provides the following CMake presets:

- **windows-vcpkg**: Windows (Visual Studio 2022, x64-windows-static-md)
- **linux-vcpkg**: Linux (Ninja, x64-linux-static)
- **macos-vcpkg**: macOS (Xcode, arm64-osx)

---

## 🙏 Acknowledgments

This project uses the following open source libraries:

- [paho.mqtt.cpp](https://github.com/eclipse/paho.mqtt.cpp) - MQTT client
- [IXWebSocket](https://github.com/machinezone/IXWebSocket) - WebSocket library
- [curl](https://curl.se/) - HTTP client
- [nlohmann/json](https://github.com/nlohmann/json) - JSON parsing
- [spdlog](https://github.com/gabime/spdlog) - Logging library

---

## 📄 License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.

Copyright © 2025 Elegoo All rights reserved.

---

**Happy Printing! 🎉**
