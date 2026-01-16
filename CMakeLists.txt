cmake_minimum_required(VERSION 3.14)
project(elegoolink
VERSION 1.0.1
    DESCRIPTION "Elegoo Link"
    LANGUAGES CXX
)
set(PROJECT_COMPANY_NAME "Shenzhen Elegoo Technology Co., Ltd.")
set(PROJECT_COPYRIGHT "Copyright (C) 2025 ELEGOO All Rights Reserved")
set(PROJECT_PRODUCT_NAME "Elegoo Link")

# Set custom install prefix
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Installation prefix" FORCE)
endif()

# Build options
option(BUILD_EXAMPLES "Build example programs" OFF)
option(BUILD_TESTS "Build test programs" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries (DLL)" OFF)
option(ENABLE_CLOUD_FEATURES "Build cloud service support" OFF)

function(parse_dotenv path)
    if(EXISTS "${path}")
        file(READ "${path}" _dotenv_content)
        string(REPLACE "\r\n" "\n" _dotenv_content "${_dotenv_content}")
        string(REPLACE "\r" "\n" _dotenv_content "${_dotenv_content}")
        string(REGEX MATCHALL "[^\n]+" _lines "${_dotenv_content}")
        foreach(_line IN LISTS _lines)
            string(REGEX REPLACE "^\\s*#.*" "" _trimmed "${_line}") 
            string(STRIP "${_trimmed}" _trimmed)
            if(_trimmed STREQUAL "") 
                continue()
            endif()
            string(REGEX MATCH "^([A-Za-z_][A-Za-z0-9_]*)=(.*)$" _m "${_trimmed}")
            if(_m)
                set(_key "${CMAKE_MATCH_1}")
                set(_val "${CMAKE_MATCH_2}")
                string(REGEX REPLACE "^\"(.*)\"$" "\\1" _val "${_val}")
                string(REGEX REPLACE "^'(.*)'$" "\\1" _val "${_val}")
                set("${_key}" "${_val}" PARENT_SCOPE)
            endif()
        endforeach()
    endif()
endfunction()

parse_dotenv("${CMAKE_SOURCE_DIR}/.env")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/include/version.h @ONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/private_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/include/private_config.h @ONLY)


# Set runtime library: decided based on vcpkg triplet and library type
if(MSVC)
    # Add UTF-8 support
    add_compile_options(/utf-8)
    add_compile_options(/wd4251)
endif()

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set macOS minimum deployment target to 10.15 (Catalina)
# Required for POSIX_SPAWN_SETSID support in server_manager.cpp
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS deployment version" FORCE)
    message(STATUS "macOS minimum deployment target: ${CMAKE_OSX_DEPLOYMENT_TARGET}")
endif()

# Enable spdlog wchar filename support for Windows to handle Chinese paths
if(WIN32)
    add_compile_definitions(SPDLOG_WCHAR_FILENAMES)
endif()

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Find dependency packages
find_package(OpenSSL REQUIRED)
find_package(PahoMqttCpp REQUIRED)
find_package(ixwebsocket REQUIRED)
find_package(CURL REQUIRED)


# Platform-specific Agora SDK include directories (only if cloud service is enabled)
if(ENABLE_CLOUD_FEATURES)
    if(WIN32)
        include_directories(${CMAKE_SOURCE_DIR}/thirdparty/agora/windows/include)
    elseif(APPLE)
        include_directories(${CMAKE_SOURCE_DIR}/thirdparty/agora/macos/AgoraRtmKit.framework/Headers)
    elseif(UNIX AND NOT APPLE)
        include_directories(${CMAKE_SOURCE_DIR}/thirdparty/agora/linux/include)
    endif()
endif()

set(COMMON_SOURCES
    # Common source files can be added here if needed
    
    # Event system
    src/events/event_system.cpp

    # Utility modules
    src/utils/utils.cpp
    src/utils/logger.cpp
    src/utils/console_utils.cpp
    src/utils/process_mutex.cpp
    
    # Core implementation layer
    src/elegoo_link.cpp
    src/static_web_server.cpp
)

set(CLIENT_SERVICE_SOURCES
    # WebSocket client
    src/core/websocket_impl/websocket_client.cpp

    src/core/websocket_impl/websocket_impl.cpp
    src/core/websocket_impl/server_manager.cpp
)

# elegoolink core source file list (excluding service layer and web server)
set(LOCAL_CORE_SOURCES
    
    src/lan/lan_service.cpp
    # Core modules
    src/lan/core/printer_manager.cpp
    src/lan/core/base_printer.cpp
    src/lan/core/elegoo_fdm_cc2_printer.cpp
    src/lan/core/elegoo_fdm_cc_printer.cpp
    src/lan/core/generic_moonraker_printer.cpp
    src/lan/core/printer_factory.cpp

    src/lan/adapters/elegoo_fdm_cc/elegoo_fdm_cc_message_adapter.cpp
    src/lan/adapters/elegoo_fdm_cc/elegoo_fdm_cc_discovery_strategy.cpp
    src/lan/adapters/elegoo_fdm_cc/elegoo_fdm_cc_http_transfer.cpp
    src/lan/adapters/elegoo_fdm_cc/elegoo_fdm_cc_protocol.cpp

    src/lan/adapters/elegoo_fdm_cc2/elegoo_fdm_cc2_message_adapter.cpp
    src/lan/adapters/elegoo_fdm_cc2/elegoo_fdm_cc2_discovery_strategy.cpp
    src/lan/adapters/elegoo_fdm_cc2/elegoo_fdm_cc2_http_transfer.cpp
    src/lan/adapters/elegoo_fdm_cc2/elegoo_fdm_cc2_protocol.cpp

    src/lan/adapters/generic_moonraker/generic_moonraker_http_transfer.cpp
    src/lan/adapters/generic_moonraker/generic_moonraker_message_adapter.cpp
    src/lan/adapters/generic_moonraker/generic_moonraker_discovery_strategy.cpp
    src/lan/adapters/generic_moonraker/generic_moonraker_protocol.cpp
    # Discovery modules
    src/lan/discovery/printer_discovery.cpp
    # Protocol modules
    src/lan/protocols/connection_manager_base.cpp
    src/lan/protocols/error_handler.cpp
    src/lan/protocols/mqtt_protocol.cpp
    src/lan/protocols/websocket_base.cpp
    src/lan/protocols/websocket_protocol.cpp
    src/lan/protocols/message_adapter.cpp
    src/lan/protocols/file_transfer.cpp
)

set(REMOTE_CORE_SOURCES
    # Remote core source files can be added here if needed

    src/cloud/cloud_service.cpp
    src/cloud/protocols/http_client.cpp
    src/cloud/protocols/mqtt_client.cpp
    src/cloud/protocols/rtm_client.cpp
    
    src/cloud/services/http_service.cpp
    src/cloud/services/mqtt_service.cpp
    src/cloud/services/rtm_service.cpp

    src/cloud/adapters/elegoo_fdm_cc2_message_adapter.cpp
    # src/cloud/adapters/message_adapter.cpp  # Removed: duplicate of src/lan/protocols/message_adapter.cpp
)

list(APPEND LOCAL_CORE_SOURCES ${COMMON_SOURCES})

# Combine all sources
set(ALL_SOURCES ${LOCAL_CORE_SOURCES})

if(ENABLE_CLOUD_FEATURES)
    list(APPEND ALL_SOURCES ${REMOTE_CORE_SOURCES})
endif()

# Create library (static or dynamic)
if(BUILD_SHARED_LIBS)
    add_library(elegoolink SHARED ${ALL_SOURCES})
    
    # Set export symbols for dynamic library
    target_compile_definitions(elegoolink PRIVATE ELEGOO_LINK_EXPORTS)
    target_compile_definitions(elegoolink INTERFACE ELEGOO_LINK_IMPORTS)
    
    # Set properties for dynamic library
    set_target_properties(elegoolink PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION 1
        OUTPUT_NAME "elegoolink"
    )
    
    # Generate resource file for DLL on Windows
    if(WIN32)
        set(FILETYPE "0x2L") # Dynamic library
        set(FILE_DESCRIPTION "Elegoo Link")
        set(INTERNAL_NAME "elegoolink.dll")
        set(ORIGINAL_FILENAME "elegoolink.dll")
        set(PRODUCT_NAME "Elegoo Link")
        
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/resources/version_dll.rc.in ${CMAKE_CURRENT_BINARY_DIR}/version_dll.rc @ONLY)
        target_sources(elegoolink PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/version_dll.rc)
    endif()
else()
    add_library(elegoolink STATIC ${ALL_SOURCES})
    
    # Set compile definitions for static library
    target_compile_definitions(elegoolink PUBLIC ELEGOO_LINK_STATIC)
    
    # Define CURL_STATICLIB for static linking
    target_compile_definitions(elegoolink PUBLIC CURL_STATICLIB)
endif()

# Disable code signing for Xcode on macOS
if(APPLE)
    set_target_properties(elegoolink PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    )
endif()

target_include_directories(elegoolink 
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src  
        ${CMAKE_CURRENT_SOURCE_DIR}/src/lan
        ${CMAKE_CURRENT_SOURCE_DIR}/src/cloud
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty
)



# Choose appropriate MQTT library based on library type and vcpkg triplet
if(BUILD_SHARED_LIBS)
    set(MQTT_LIB PahoMqttCpp::paho-mqttpp3-static)
else()
    # Static library uses static version of MQTT library
    set(MQTT_LIB PahoMqttCpp::paho-mqttpp3-static)
endif()

target_link_libraries(elegoolink PUBLIC
    ${MQTT_LIB}
    ixwebsocket::ixwebsocket
    CURL::libcurl
    OpenSSL::SSL 
    OpenSSL::Crypto
)

# Link macOS system frameworks (required for utils.cpp getMachineId())
if(APPLE)
    target_link_libraries(elegoolink PUBLIC
        "-framework CoreFoundation"
        "-framework IOKit"
    )
endif()

# Platform-specific Agora SDK linking (only if cloud service is enabled)
if(ENABLE_CLOUD_FEATURES)
    if(WIN32)
        target_link_libraries(elegoolink PUBLIC
            ${CMAKE_SOURCE_DIR}/thirdparty/agora/windows/lib/x86_64/agora_rtm_sdk.dll.lib
        )
    elseif(APPLE)
        # Link Agora RTM Framework on macOS
        set(AGORA_FRAMEWORK_PATH "${CMAKE_SOURCE_DIR}/thirdparty/agora/macos/AgoraRtmKit.framework")
        set(AOSL_FRAMEWORK_PATH "${CMAKE_SOURCE_DIR}/thirdparty/agora/macos/aosl.framework")
        target_link_libraries(elegoolink PUBLIC
            ${AGORA_FRAMEWORK_PATH}
            ${AOSL_FRAMEWORK_PATH}
        )

        # # Configure RPATH for dlopen scenarios
        # # dylib and framework are in the same folder, use @loader_path
        # set_target_properties(elegoolink PROPERTIES
        #     INSTALL_RPATH "@loader_path"
        #     BUILD_WITH_INSTALL_RPATH TRUE
        #     MACOSX_RPATH TRUE
        # )
        
        # Set framework search path
        target_link_options(elegoolink PUBLIC
            -F${CMAKE_SOURCE_DIR}/thirdparty/agora/macos
        )
    elseif(UNIX AND NOT APPLE)
        # Link Agora RTM SDK on Linux
        target_link_libraries(elegoolink PUBLIC
            ${CMAKE_SOURCE_DIR}/thirdparty/agora/linux/libagora_rtm_sdk.so
            ${CMAKE_SOURCE_DIR}/thirdparty/agora/linux/libaosl.so
        )
        
        # Configure RPATH for runtime library loading
        set_target_properties(elegoolink PROPERTIES
            INSTALL_RPATH "$ORIGIN"
            BUILD_WITH_INSTALL_RPATH TRUE
        )
    endif()
endif()

# Link system crypto library on Windows systems
if(WIN32)
    target_link_libraries(elegoolink PUBLIC crypt32)
endif()

# Platform-specific Agora SDK files deployment (only if cloud service is enabled)
if(ENABLE_CLOUD_FEATURES)
    if(WIN32)
        # Copy Agora SDK DLL files to bin directory after build
        add_custom_command(TARGET elegoolink POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/thirdparty/agora/windows/lib/x86_64/agora_rtm_sdk.dll"
                "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/agora_rtm_sdk.dll"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/thirdparty/agora/windows/lib/x86_64/libaosl.dll"
                "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/libaosl.dll"
            COMMENT "Copying Agora SDK DLL files to bin directory"
        )
        
        # Install Agora SDK DLL files
        install(FILES
            "${CMAKE_SOURCE_DIR}/thirdparty/agora/windows/lib/x86_64/agora_rtm_sdk.dll"
            "${CMAKE_SOURCE_DIR}/thirdparty/agora/windows/lib/x86_64/libaosl.dll"
            DESTINATION bin
        )
    elseif(APPLE)
        # Copy Agora Frameworks to bin directory using rsync to preserve symlinks
        # rsync -a preserves symbolic links, permissions, and directory structure
        add_custom_command(TARGET elegoolink POST_BUILD
            COMMAND rsync -a 
                "${CMAKE_SOURCE_DIR}/thirdparty/agora/macos/AgoraRtmKit.framework/"
                "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/AgoraRtmKit.framework/"
            COMMAND rsync -a 
                "${CMAKE_SOURCE_DIR}/thirdparty/agora/macos/aosl.framework/"
                "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/aosl.framework/"
            COMMENT "Copying Agora RTM Frameworks to bin directory (preserving symlinks)"
        )
        
        # Install Agora Frameworks
        install(DIRECTORY
            "${CMAKE_SOURCE_DIR}/thirdparty/agora/macos/AgoraRtmKit.framework"
            "${CMAKE_SOURCE_DIR}/thirdparty/agora/macos/aosl.framework"
            DESTINATION bin
        )
    elseif(UNIX AND NOT APPLE)
        # Copy Agora SDK shared libraries to bin directory after build
        add_custom_command(TARGET elegoolink POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/thirdparty/agora/linux/libagora_rtm_sdk.so"
                "${CMAKE_BINARY_DIR}/bin/libagora_rtm_sdk.so"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/thirdparty/agora/linux/libaosl.so"
                "${CMAKE_BINARY_DIR}/bin/libaosl.so"
            COMMENT "Copying Agora SDK shared libraries to bin directory"
        )
        
        # Install Agora SDK shared libraries
        install(FILES
            "${CMAKE_SOURCE_DIR}/thirdparty/agora/linux/libagora_rtm_sdk.so"
            "${CMAKE_SOURCE_DIR}/thirdparty/agora/linux/libaosl.so"
            DESTINATION bin
        )
    endif()
endif()

# Build example programs
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Install configuration
set(INSTALL_TARGETS elegoolink)

install(TARGETS ${INSTALL_TARGETS}
    EXPORT elegoolink_targets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install main header files
install(FILES 
    include/elegoo_link.h
    include/elegoo_export.h
    include/type.h
    include/config.h
    DESTINATION include/elegoolink
)

# Install all header files in types directory (excluding internal folder)
install(DIRECTORY include/types/ 
    DESTINATION include/elegoolink/types
    FILES_MATCHING PATTERN "*.h"
    PATTERN "internal" EXCLUDE
)

# Install event system header files
install(FILES 
    include/events/event_system.h
    DESTINATION include/elegoolink/events
)

# Install generated version header file
install(FILES 
    ${CMAKE_CURRENT_BINARY_DIR}/include/version.h
    DESTINATION include/elegoolink
)

# Generate and install package configuration files
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# Set install path variables
set(CMAKE_INSTALL_INCLUDEDIR "include/elegoolink")
set(CMAKE_INSTALL_LIBDIR "lib")
set(CMAKE_INSTALL_BINDIR "bin")

# Generate version configuration file
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/elegoolink-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

# Generate package configuration file
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/elegoolink-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/elegoolink-config.cmake"
    INSTALL_DESTINATION lib/cmake/elegoolink
    PATH_VARS
        CMAKE_INSTALL_INCLUDEDIR
        CMAKE_INSTALL_LIBDIR
        CMAKE_INSTALL_BINDIR
)

# Install export targets
install(EXPORT elegoolink_targets
    FILE elegoolink_targets.cmake
    NAMESPACE elegoolink::
    DESTINATION lib/cmake/elegoolink
)

# Install package configuration files
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/elegoolink-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/elegoolink-config-version.cmake"
    DESTINATION lib/cmake/elegoolink
)

