# Elegoo Link Examples

This directory contains example programs demonstrating the use of the Elegoo Link SDK.

## Examples

### printer_connection_test

A comprehensive test program that demonstrates:
- Initializing ElegooLink
- Discovering printers on the network
- Connecting to a printer
- Displaying printer information
- Monitoring printer status
- File upload (optional)
- Starting a print job (optional)

## Building Examples

### Prerequisites

- CMake 3.14 or later
- vcpkg (for dependency management)
- C++17 compatible compiler

### Build Steps

#### 1. Configure with Examples Enabled

**Windows (PowerShell):**
```powershell
cmake --preset windows-vcpkg -DBUILD_EXAMPLES=ON
```

**Linux:**
```bash
cmake --preset linux-vcpkg -DBUILD_EXAMPLES=ON
```

**macOS:**
```bash
cmake --preset macos-vcpkg -DBUILD_EXAMPLES=ON
```

#### 2. Build

**Windows:**
```powershell
cmake --build build --config Debug
# or
cmake --build build --config Release
```

**Linux/macOS:**
```bash
cmake --build build
```

#### 3. Run Examples

The compiled executables will be in:
- Windows: `build/bin/Debug/` or `build/bin/Release/`
- Linux/macOS: `build/bin/`

**Basic test (connect and monitor):**
```powershell
# Windows
.\build\bin\Debug\printer_connection_test.exe

# Linux/macOS
./build/bin/printer_connection_test
```

**With file upload:**
```powershell
.\build\bin\Debug\printer_connection_test.exe --upload
```

**With file upload and print:**
```powershell
.\build\bin\Debug\printer_connection_test.exe --print
```

**Interactive mode:**
```powershell
.\build\bin\Debug\printer_connection_test.exe --interactive
```

**Show help:**
```powershell
.\build\bin\Debug\printer_connection_test.exe --help
```

## Configuration

Before running, you may need to modify the test configuration in `printer_connection_test.cpp`:

```cpp
struct TestConfig
{
    // Printer connection parameters
    std::string printerHost = "10.31.3.110";        // Change to your printer IP
    PrinterType printerType = PrinterType::ELEGOO_FDM_CC;
    std::string printerModel = "Elegoo Neptune 4";
    
    // File upload parameters
    std::string uploadFilePath = R"(C:\path\to\your\file.gcode)";
    std::string uploadFileName = "test.gcode";
};
```

## Command Line Options

- `-u, --upload` - Enable file upload test
- `-p, --print` - Enable file upload and print test
- `-a, --attributes` - Show detailed printer attributes
- `-i, --interactive` - Run in interactive mode with menu
- `-h, --help` - Show help message

## Troubleshooting

### Example not building

Make sure you configured with `-DBUILD_EXAMPLES=ON`:
```powershell
cmake --preset windows-vcpkg -DBUILD_EXAMPLES=ON
cmake --build build --config Debug
```

### Cannot find printer

1. Ensure the printer is on the same network
2. Check firewall settings
3. Verify the printer IP address in the configuration
4. Make sure the printer supports the protocol type specified

### Connection fails

1. Verify printer type matches your actual printer
2. Check if authentication is required (set `authMode` if needed)
3. Ensure no other software is connected to the printer
