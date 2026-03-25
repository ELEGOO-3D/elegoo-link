# Examples CMakeLists.txt

function(add_elegoo_example target_name source_file file_description)
    add_executable(${target_name}
        ${source_file}
    )

    target_link_libraries(${target_name} PRIVATE
        elegoolink
    )

    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )

    if(APPLE)
        set_target_properties(${target_name} PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
        )
    endif()

    if(WIN32 AND BUILD_SHARED_LIBS)
        set(FILETYPE "0x1L") # Application
        set(FILE_DESCRIPTION "${file_description}")
        set(INTERNAL_NAME "${target_name}.exe")
        set(ORIGINAL_FILENAME "${target_name}.exe")
        set(PRODUCT_NAME "Elegoo Link Test")

        configure_file(
            ${CMAKE_SOURCE_DIR}/resources/version.rc.in
            ${CMAKE_CURRENT_BINARY_DIR}/${target_name}.rc
            @ONLY
        )

        target_sources(${target_name} PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR}/${target_name}.rc
        )
    endif()

    install(TARGETS ${target_name}
        RUNTIME DESTINATION bin
    )
endfunction()

add_elegoo_example(printer_connection_test printer_connection_test.cpp "Elegoo Link Printer Connection Test")
add_elegoo_example(cloud_service_test cloud_service_test.cpp "Elegoo Link Cloud Service Test")

message(STATUS "Examples configured:")
message(STATUS "  - printer_connection_test")
message(STATUS "  - cloud_service_test")
