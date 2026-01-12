#include "utils/console_utils.h"
#include <iostream>
#include <locale>

#ifdef _WIN32
#include <Windows.h>
#include <io.h>
#include <fcntl.h>
#include <codecvt>
#endif

namespace elink 
{

    void ConsoleUtils::setupUTF8()
    {
#ifdef _WIN32
        // Set the console code page to UTF-8
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);

        // Attempt to enable virtual terminal processing (Windows 10 and later)
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE)
        {
            DWORD dwMode = 0;
            if (GetConsoleMode(hOut, &dwMode))
            {
                dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
                SetConsoleMode(hOut, dwMode);
            }
        }

        // Set the locale for the C runtime library
        setlocale(LC_ALL, ".UTF8");

        // Set the locale for C++
        try
        {
            std::locale::global(std::locale(""));
        }
        catch (const std::exception &)
        {
            // If setting fails, use the default locale
            std::locale::global(std::locale("C"));
        }
#else
        // On non-Windows systems, UTF-8 is usually supported by default
        try
        {
            std::locale::global(std::locale(""));
        }
        catch (const std::exception &)
        {
            std::locale::global(std::locale("C.UTF-8"));
        }
#endif
    }

    bool ConsoleUtils::supportsUTF8()
    {
#ifdef _WIN32
        // Check the console code page
        return GetConsoleOutputCP() == CP_UTF8;
#else
        // On non-Windows systems, assume UTF-8 support
        return true;
#endif
    }

    void ConsoleUtils::safeOutput(const std::string &text)
    {
        if (supportsUTF8())
        {
            std::cout << text;
        }
        else
        {
            // If UTF-8 is not supported, output an ASCII-safe version
            std::string safeText = toConsoleString(text);
            std::cout << safeText;
        }
    }

    std::string ConsoleUtils::toConsoleString(const std::string &utf8Text)
    {
#ifdef _WIN32
        if (!supportsUTF8())
        {
            // On Windows consoles that do not support UTF-8, replace non-ASCII characters
            std::string result;
            for (char c : utf8Text)
            {
                if (static_cast<unsigned char>(c) < 128)
                {
                    result += c;
                }
                else
                {
                    result += '?'; // Replace non-ASCII characters
                }
            }
            return result;
        }
#endif
        return utf8Text;
    }

} // namespace elink
