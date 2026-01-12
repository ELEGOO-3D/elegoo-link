# Elegoo Link SDK - AI Coding Assistant Guide

## Project Overview

Elegoo Link is a C++17 SDK for controlling Elegoo 3D printers. It provides both local (LAN) and cloud-based printer management through a unified API.

### Core Architecture

**Unified Implementation**:
- `ElegooLink` (singleton, public API) uses internal `Impl` class (PImpl pattern)
- `Impl` coordinates `LanService` (local network) and `CloudService` (cloud-based, conditional via `#ifdef ENABLE_CLOUD_FEATURES`)
- Routing decision: Check printer ID in cached printer lists to determine LAN vs. Cloud service usage
- No mode selection needed - services are initialized together and routing is automatic

**Implementation Pattern**:
- `ElegooLink::getInstance()` (singleton) → `ElegooLink::Impl` (private, source-only)
- `Impl` directly orchestrates `LanService::getInstance()` and `CloudService::getInstance()` (both singletons)
- Events forwarded from both services to `ElegooLink::eventBus_` via callbacks

### Major Components

**1. Printer Adapter System** (`src/lan/core/printer_adapter.h`):
- Plugin architecture via `IPrinterAdapter` interface
- Registered adapters: `ElegooFdmCCAdapter`, `ElegooFdmCC2Adapter`, `GenericMoonrakerAdapter`
- Each adapter provides: message parsing, discovery strategy, file transfer, protocol implementation
- Registry pattern: `PrinterAdapterRegistry::getInstance().registerAdapter(adapter)`
- Initialization happens in `LanServiceImpl::initializeAdapters()`

**2. Protocol Layer** (`src/lan/protocols/`):
- `IProtocol` interface with implementations: `MqttProtocol`, `WebSocketProtocol`
- `IMessageAdapter`: Converts printer-specific messages to/from SDK types
- `IHttpFileTransfer`: Handles file uploads to printers

**3. Service Layer**:
- `LanService`: Local printer discovery (UDP broadcast), connection management, print control
- `CloudService`: Remote printer access via HTTP/MQTT/RTM (Agora), user authentication
- Both controlled via unified `ElegooLink` API

**4. Event System** (`include/events/event_system.h`):
- Type-safe publish-subscribe using `EventBus`
- Subscribe: `elegooLink.subscribeEvent<PrinterStatusChangedEvent>(handler)`
- Events automatically forwarded from impl layer to EventBus via callback

### Data Flow Example (File Upload)

```
ElegooLink::uploadFile() 
  → Impl::isLocalPrinter(printerId) checks cached printers
    → If local: LanService::uploadFile()
        → PrinterManager::getPrinter(printerId)
          → Printer::uploadFile()
            → BasePrinterAdapter::createFileUploader()
              → ElegooFdmCC2HttpTransfer::upload() (adapter-specific)
                → Progress callbacks → FileUploadProgressEvent
    → If cloud (#ifdef ENABLE_CLOUD_FEATURES): CloudService::uploadFile()
```

## Build System

### CMake Configuration

- **Presets**: `CMakePresets.json` defines platform-specific configurations
  - Windows: `windows-vcpkg` (Visual Studio 2022, x64-windows-static-md)
  - Linux: `linux-vcpkg` (Ninja, x64-linux-static)
  - macOS: `macos-vcpkg` (Xcode, arm64-osx)

- **Key Options**:
  - `BUILD_EXAMPLES=ON/OFF`: Build example programs
  - `BUILD_TESTS=ON/OFF`: Build test programs (option exists, no tests yet)
  - `ENABLE_CLOUD_FEATURES=ON/OFF`: Include cloud service support (requires Agora SDK)
  - `BUILD_SHARED_LIBS=ON/OFF`: Build as DLL/shared library instead of static

- **Generated Files**:
  - `build/include/version.h` from `version.h.in` (version macros)
  - `build/include/private_config.h` from `private_config.h.in` (API keys from `.env`)

### vcpkg Dependencies

Dependencies in `vcpkg.json`:
- `paho-mqttpp3` (>= 1.5.2): MQTT protocol
- `ixwebsocket`: WebSocket client/server
- `curl` (>= 8.14.1): HTTP requests

Third-party includes (not via vcpkg):
- `thirdparty/agora/`: Agora RTM SDK (cloud real-time messaging)
- `thirdparty/nlohmann/`: JSON library (header-only)
- `thirdparty/spdlog/`: Logging (header-only)
- `thirdparty/httplib.h`: Simple HTTP server for static files

### Build Commands

```powershell
# Configure (uses preset from CMakePresets.json)
# Windows default preset: windows-vcpkg
cmake --preset windows-vcpkg

# Build
cmake --build build --config Debug

# Or use VS Code tasks: "Build" (Ctrl+Shift+B), "Configure", "Clean"
```

### Environment Setup

Create `.env` file in project root for API credentials:
```
AGORA_APP_ID=your_agora_app_id_here
```

This file is parsed during CMake configuration and values are injected into [private_config.h.in](../private_config.h.in).

## Development Conventions

### Error Handling

- All API methods return `BizResult<T>` or specialized types (e.g., `VoidResult`, `ConnectPrinterResult`)
- Success check: `result.code == ELINK_ERROR_CODE::SUCCESS` or `result.isSuccess()`
- Access data: `result.data.value()` or `result.hasValue()`
- Factory methods: `BizResult<T>::Ok(data)`, `BizResult<T>::Error(code, msg)`

### Logging

- Use `ELEGOO_LOG_*` macros (TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL)
- Logger initialized via config: `config.log.logLevel`, `logEnableConsole`, `logEnableFile`
- Windows UTF-8 paths: `SPDLOG_WCHAR_FILENAMES` defined for Chinese path support

### Initialization Checks

All `ElegooLink` methods use macros to ensure initialization:
```cpp
#define CHECK_INIT_RETURN(ReturnType) \
    if (!pImpl_ || !pImpl_->isInitialized()) { \
        return ReturnType::Error(ELINK_ERROR_CODE::NOT_INITIALIZED, "..."); \
    }
```

### String Utilities

- Use `StringUtils::maskString(printerId)` for logging sensitive IDs
- Use `PathUtils::openOutputStream()` for cross-platform file I/O with UTF-8 support

### Multi-threading

- `EventBus` uses `std::mutex` for thread-safe subscriptions
- Service operations are synchronous; long-running tasks (discovery, upload) use callbacks
- Protocol implementations handle their own threading (MQTT, WebSocket protocols use internal event loops)

## Common Workflows

### Adding a New Printer Adapter

1. Create adapter directory: `src/lan/adapters/<adapter_name>/`
2. Implement `IPrinterAdapter` interface (see `elegoo_fdm_cc2_adapter.cpp`)
3. Implement `IMessageAdapter` for message parsing
4. Implement `IDiscoveryStrategy` for UDP/network discovery
5. Implement `IHttpFileTransfer` for file uploads
6. Register in `LanServiceImpl::initializeAdapters()`:
   ```cpp
   auto adapter = std::make_shared<MyAdapter>();
   registry.registerAdapter(adapter);
   ```

### Testing Changes

- Primary example: [examples/printer_connection_test.cpp](../examples/printer_connection_test.cpp)
- Build with `BUILD_EXAMPLES=ON` (enabled by default in presets)
- Executables output to `build/bin/Debug/` or `build/bin/Release/`
- See [examples/README.md](../examples/README.md) for detailed usage

### WebSocket Server Development

**Note**: The current codebase does not include WebSocket server implementation. References to `elegoo-link-service`, `WebSocketImpl`, `DirectImpl`, and `MessageRouter` are from legacy architecture planning and are not present in the current source tree.

## Platform-Specific Notes

### Windows
- Uses `/utf-8` compiler flag for UTF-8 source files
- Disables warning 4251 (DLL export warnings)
- Resource files (`.rc`) for executable/DLL version info

### macOS
- Agora SDK as framework bundles (AgoraRtmKit.framework, aosl.framework)
- Frameworks copied to `build/bin/$<CONFIG>/` post-build via `rsync -a` (preserves symlinks)
- Code signing disabled in CMake for Xcode builds

### Linux
- Static linking preferred (`x64-linux-static` triplet)
- Ninja generator for faster builds

## Configuration System

All components use unified `ElegooLinkConfig` (from [include/config.h](../include/config.h)):
- `log`: Logging settings (level, console/file output, rotation)
- `local`: LAN service settings (static web path)
- `cloud`: Cloud service settings (region, API URLs, CA cert, user-agent)

Configuration is passed directly to `elegooLink.initialize(config)`. No JSON loading utilities in current codebase.

## Key Files for Reference

- Public API: [include/elegoo_link.h](../include/elegoo_link.h)
- Type definitions: [include/types/*.h](../include/types/)
- Event types: [include/types/event.h](../include/types/event.h)
- Main implementation: [src/elegoo_link.cpp](../src/elegoo_link.cpp)
- Adapter registry: [src/lan/core/printer_adapter.h](../src/lan/core/printer_adapter.h)
- LAN service: [src/lan/lan_service.h](../src/lan/lan_service.h), [src/lan/lan_service_impl.h](../src/lan/lan_service_impl.h)
- Cloud service: [src/cloud/cloud_service.h](../src/cloud/cloud_service.h) (when `ENABLE_CLOUD_FEATURES=ON`)
