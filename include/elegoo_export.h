#pragma once

// Macro definitions for controlling DLL import/export
#ifdef _WIN32
    #ifdef ELEGOO_LINK_STATIC
        // Static library
        #define ELEGOO_LINK_API
    #elif defined(ELEGOO_LINK_EXPORTS)
        // Dynamic library export
        #define ELEGOO_LINK_API __declspec(dllexport)
    #else
        // Dynamic library import
        #define ELEGOO_LINK_API __declspec(dllimport)
    #endif
#else
    // Non-Windows platform
    #if defined(ELEGOO_LINK_EXPORTS) && defined(__GNUC__) && __GNUC__ >= 4
        #define ELEGOO_LINK_API __attribute__ ((visibility ("default")))
    #else
        #define ELEGOO_LINK_API
    #endif
#endif
