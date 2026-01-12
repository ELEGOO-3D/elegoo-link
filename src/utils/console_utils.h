#pragma once

#include <string>

namespace elink  {

/**
 * Console utility class for handling encoding and display issues
 */
class ConsoleUtils {
public:
    /**
     * Set console encoding to UTF-8 (Windows)
     */
    static void setupUTF8();
    
    /**
     * Check if the console supports UTF-8
     */
    static bool supportsUTF8();
    
    /**
     * Safely output strings that may contain non-ASCII characters
     */
    static void safeOutput(const std::string& text);
    
    /**
     * Convert a UTF-8 string to a console-compatible string
     */
    static std::string toConsoleString(const std::string& utf8Text);
};

} // namespace elink
