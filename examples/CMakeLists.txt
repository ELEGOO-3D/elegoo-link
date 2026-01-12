# Examples CMakeLists.txt

# Printer Connection Test Example
add_executable(printer_connection_test
    printer_connection_test.cpp
)

# Link against elegoolink library
target_link_libraries(printer_connection_test PRIVATE
    elegoolink
)

# Set output directory
set_target_properties(printer_connection_test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Disable code signing for Xcode on macOS
if(APPLE)
    set_target_properties(printer_connection_test PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    )
endif()

# Generate resource file for executable on Windows
if(WIN32 AND BUILD_SHARED_LIBS)
    set(FILETYPE "0x1L") # Application
    set(FILE_DESCRIPTION "Elegoo Link Printer Connection Test")
    set(INTERNAL_NAME "printer_connection_test.exe")
    set(ORIGINAL_FILENAME "printer_connection_test.exe")
    set(PRODUCT_NAME "Elegoo Link Test")
    
    configure_file(
        ${CMAKE_SOURCE_DIR}/resources/version.rc.in 
        ${CMAKE_CURRENT_BINARY_DIR}/printer_connection_test.rc 
        @ONLY
    )
    target_sources(printer_connection_test PRIVATE 
        ${CMAKE_CURRENT_BINARY_DIR}/printer_connection_test.rc
    )
endif()

# Install the example executable
install(TARGETS printer_connection_test
    RUNTIME DESTINATION bin
)

message(STATUS "Examples configured:")
message(STATUS "  - printer_connection_test")
