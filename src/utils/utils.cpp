#include <utils/utils.h>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <regex>
#include <chrono>
#include <random>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <intrin.h>
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "advapi32.lib")
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <net/if.h>
#include <sys/utsname.h>
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <dlfcn.h>
#else
#include <openssl/md5.h>
#include <openssl/sha.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <net/if.h>
#include <sys/utsname.h>
#include <dlfcn.h>
#endif
#include <vector>
namespace elink
{

    // ========== SDKVersion Implementation ==========

    std::string SDKVersion::getVersionString()
    {
        return std::to_string(MAJOR) + "." + std::to_string(MINOR) + "." + std::to_string(PATCH);
    }

    std::string SDKVersion::getBuildInfo()
    {
        return "Elegoo Print Link SDK v" + getVersionString() + " built on " + __DATE__ + " " + __TIME__;
    }

    // ========== NetworkUtils Implementation ==========

    std::vector<BroadcastInfo> NetworkUtils::getBroadcastAddresses()
    {
        std::vector<BroadcastInfo> result;

#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);

        ULONG bufLen = 15000;
        std::vector<char> buffer(bufLen);
        IP_ADAPTER_ADDRESSES *adapterAddresses = reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());

        if (GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, adapterAddresses, &bufLen) == NO_ERROR)
        {
            for (IP_ADAPTER_ADDRESSES *adapter = adapterAddresses; adapter != nullptr; adapter = adapter->Next)
            {
                for (IP_ADAPTER_UNICAST_ADDRESS *addr = adapter->FirstUnicastAddress; addr != nullptr; addr = addr->Next)
                {
                    sockaddr_in *sa = reinterpret_cast<sockaddr_in *>(addr->Address.lpSockaddr);
                    DWORD ip = ntohl(sa->sin_addr.s_addr);
                    ULONG prefixLength = addr->OnLinkPrefixLength;
                    DWORD mask = prefixLength == 0 ? 0 : 0xFFFFFFFF << (32 - prefixLength);
                    DWORD broadcast = ip | ~mask;

                    struct in_addr ip_addr, bcast_addr;
                    ip_addr.s_addr = htonl(ip);
                    bcast_addr.s_addr = htonl(broadcast);

                    char ipStr[INET_ADDRSTRLEN];
                    char bcastStr[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &ip_addr, ipStr, sizeof(ipStr));
                    inet_ntop(AF_INET, &bcast_addr, bcastStr, sizeof(bcastStr));

                    result.push_back({adapter->AdapterName, ipStr, bcastStr});
                }
            }
        }

        WSACleanup();

#else
        struct ifaddrs *ifaddr = nullptr;
        getifaddrs(&ifaddr);

        for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next)
        {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET)
                continue;

            if (ifa->ifa_flags & IFF_LOOPBACK)
                continue;

            sockaddr_in *ip = (sockaddr_in *)ifa->ifa_addr;
            sockaddr_in *netmask = (sockaddr_in *)ifa->ifa_netmask;

            uint32_t ip_val = ntohl(ip->sin_addr.s_addr);
            uint32_t mask_val = ntohl(netmask->sin_addr.s_addr);
            uint32_t broadcast = ip_val | ~mask_val;

            in_addr ip_addr, bcast_addr;
            ip_addr.s_addr = htonl(ip_val);
            bcast_addr.s_addr = htonl(broadcast);

            char ipStr[INET_ADDRSTRLEN];
            char bcastStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &ip_addr, ipStr, sizeof(ipStr));
            inet_ntop(AF_INET, &bcast_addr, bcastStr, sizeof(bcastStr));

            result.push_back({ifa->ifa_name, ipStr, bcastStr});
        }

        freeifaddrs(ifaddr);
#endif

        return result;
    }

    std::vector<std::string> NetworkUtils::getLocalIPAddresses()
    {
        std::vector<std::string> addresses;

#ifdef _WIN32
        ULONG outBufLen = 15000;
        PIP_ADAPTER_ADDRESSES pAddresses = nullptr;

        do
        {
            pAddresses = (IP_ADAPTER_ADDRESSES *)malloc(outBufLen);
            if (pAddresses == nullptr)
            {
                break;
            }

            DWORD dwRetVal = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, nullptr, pAddresses, &outBufLen);

            if (dwRetVal == ERROR_BUFFER_OVERFLOW)
            {
                free(pAddresses);
                pAddresses = nullptr;
            }
            else
            {
                break;
            }
        } while (pAddresses == nullptr);

        if (pAddresses)
        {
            PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses;
            while (pCurrAddresses)
            {
                PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurrAddresses->FirstUnicastAddress;
                while (pUnicast)
                {
                    if (pUnicast->Address.lpSockaddr->sa_family == AF_INET)
                    {
                        sockaddr_in *sockaddr_ipv4 = (sockaddr_in *)pUnicast->Address.lpSockaddr;
                        char ip_str[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &(sockaddr_ipv4->sin_addr), ip_str, INET_ADDRSTRLEN);
                        addresses.push_back(std::string(ip_str));
                    }
                    pUnicast = pUnicast->Next;
                }
                pCurrAddresses = pCurrAddresses->Next;
            }
            free(pAddresses);
        }
#else
        struct ifaddrs *ifaddrs_ptr = nullptr;
        if (getifaddrs(&ifaddrs_ptr) == 0)
        {
            for (struct ifaddrs *ifa = ifaddrs_ptr; ifa != nullptr; ifa = ifa->ifa_next)
            {
                if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET)
                {
                    struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;
                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(sin->sin_addr), ip_str, INET_ADDRSTRLEN);
                    addresses.push_back(std::string(ip_str));
                }
            }
            freeifaddrs(ifaddrs_ptr);
        }
#endif

        return addresses;
    }

    // Helper function to find an available port
    int NetworkUtils::findAvailablePort(const std::string &host)
    {
#ifdef _WIN32
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

        unsigned int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0)
        {
            return 0;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = 0; // Let system assign port
        addr.sin_addr.s_addr = inet_addr(host.c_str());

        if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
#ifdef _WIN32
            closesocket(sockfd);
#else
            close(sockfd);
#endif
            return 0;
        }

        socklen_t len = sizeof(addr);
        if (getsockname(sockfd, (struct sockaddr *)&addr, &len) < 0)
        {
#ifdef _WIN32
            closesocket(sockfd);
#else
            close(sockfd);
#endif
            return 0;
        }

        int port = ntohs(addr.sin_port);

#ifdef _WIN32
        closesocket(sockfd);
#else
        close(sockfd);
#endif

        return port;
    }

    // std::vector<std::string> NetworkUtils::getBroadcastAddresses() {
    //     std::vector<std::string> broadcast_addresses;
    //     auto ip_addresses = getLocalIPAddresses();

    //     for (const auto& ip : ip_addresses) {
    //         // Simple broadcast address calculation (assuming /24 subnet)
    //         size_t last_dot = ip.find_last_of('.');
    //         if (last_dot != std::string::npos) {
    //             std::string subnet = ip.substr(0, last_dot);
    //             broadcast_addresses.push_back(subnet + ".255");
    //         }
    //     }

    //     // Add global broadcast address
    //     broadcast_addresses.push_back("255.255.255.255");

    //     return broadcast_addresses;
    // }

    bool NetworkUtils::isValidIPAddress(const std::string &ip)
    {
        struct sockaddr_in sa;
        int result = inet_pton(AF_INET, ip.c_str(), &(sa.sin_addr));
        return result != 0;
    }

    bool NetworkUtils::isValidPort(int port)
    {
        return port > 0 && port <= 65535;
    }

    int NetworkUtils::createUdpSocket()
    {
        int sock = (int)socket(AF_INET, SOCK_DGRAM, 0);
        if (sock < 0)
        {
            return -1;
        }

        return sock;
    }

    bool NetworkUtils::enableBroadcast(int socket)
    {
        int broadcast_enable = 1;
        if (setsockopt(socket, SOL_SOCKET, SO_BROADCAST, (char *)&broadcast_enable, sizeof(broadcast_enable)) < 0)
        {
            return false;
        }
        return true;
    }

    bool NetworkUtils::setSocketTimeout(int socket, int timeout_ms)
    {
#ifdef _WIN32
        DWORD timeout = timeout_ms;
        if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout)) < 0)
        {
            return false;
        }
#else
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        {
            return false;
        }
#endif
        return true;
    }

    // ========== StringUtils Implementation ==========

    std::vector<std::string> StringUtils::split(const std::string &str, const std::string &delimiter)
    {
        std::vector<std::string> tokens;
        size_t start = 0;
        size_t end = str.find(delimiter);

        while (end != std::string::npos)
        {
            tokens.push_back(str.substr(start, end - start));
            start = end + delimiter.length();
            end = str.find(delimiter, start);
        }

        tokens.push_back(str.substr(start));
        return tokens;
    }

    std::string StringUtils::trim(const std::string &str)
    {
        size_t start = str.find_first_not_of(" \t\n\r\f\v");
        if (start == std::string::npos)
        {
            return "";
        }

        size_t end = str.find_last_not_of(" \t\n\r\f\v");
        return str.substr(start, end - start + 1);
    }

    std::string StringUtils::toLowerCase(const std::string &str)
    {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }

    std::string StringUtils::toUpperCase(const std::string &str)
    {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::toupper);
        return result;
    }

    bool StringUtils::startsWith(const std::string &str, const std::string &prefix)
    {
        return str.length() >= prefix.length() &&
               str.compare(0, prefix.length(), prefix) == 0;
    }

    bool StringUtils::endsWith(const std::string &str, const std::string &suffix)
    {
        return str.length() >= suffix.length() &&
               str.compare(str.length() - suffix.length(), suffix.length(), suffix) == 0;
    }

    std::string StringUtils::replaceAll(const std::string &str, const std::string &from, const std::string &to)
    {
        if (str.empty())
        {
            return str;
        }
        if (from.empty())
        {
            return str;
        }
        std::string result = str;
        size_t start_pos = 0;
        while ((start_pos = result.find(from, start_pos)) != std::string::npos)
        {
            result.replace(start_pos, from.length(), to);
            start_pos += to.length();
        }
        return result;
    }

    std::string StringUtils::maskString(const std::string &str, char maskChar)
    {
        size_t len = str.length();

        // If string length is less than or equal to 4, no masking
        if (len <= 4)
        {
            return str;
        }
        
        // If string length is less than 6, mask the first 2 characters
        if (len < 6)
        {
            std::string result;
            result.reserve(len);
            result.append(2, maskChar);
            result.append(str.substr(2));
            return result;
        }

        // For strings with 8 or more characters, mask the middle 2/5 characters
        size_t maskLength = len / 2;
        size_t maskStart = (len - maskLength) / 2;  // Center the mask
        
        std::string result;
        result.reserve(len);

        // Add characters before masked section
        result.append(str.substr(0, maskStart));

        // Add masked characters
        result.append(maskLength, maskChar);

        // Add characters after masked section
        result.append(str.substr(maskStart + maskLength));

        return result;
    }

    std::string StringUtils::formatErrorMessage(const std::string &message, int errorCode)
    {
        std::ostringstream oss;
        oss << message << " [ErrorCode:" << errorCode << "]";
        return oss.str();
    }
    std::string StringUtils::formatErrorMessage(int errorCode, const std::string &message)
    {
        std::ostringstream oss;
        if (!message.empty())
        {
            oss << message << " ";
        }
        else
        {
            oss << "Unknown error.";
        }
        oss << "[ErrorCode:" << errorCode << "]";
        return oss.str();
    }

    // ========== UrlUtils Implementation ==========
    UrlInfo UrlUtils::parseUrl(const std::string &Url)
    {
        UrlInfo info;

        if (Url.empty())
        {
            return info;
        }

        // First try to match full URL with scheme
        std::regex full_url_regex(
            R"(^(https?):\/\/([^:\/\s]+)(?::(\d+))?([^?\s]*)(?:\?([^#\s]*))?(?:#([^\s]*))?$)",
            std::regex_constants::icase);

        std::smatch matches;
        if (std::regex_match(Url, matches, full_url_regex))
        {
            info.scheme = toLowercase(matches[1].str());
            info.host = matches[2].str();

            // Parse port
            if (matches[3].matched)
            {
                try
                {
                    info.port = std::stoi(matches[3].str());
                }
                catch (const std::exception &)
                {
                    info.port = getDefaultPort(info.scheme);
                }
            }
            else
            {
                info.port = getDefaultPort(info.scheme);
            }

            info.path = matches[4].matched ? matches[4].str() : "/";
            info.query = matches[5].matched ? matches[5].str() : "";
            info.fragment = matches[6].matched ? matches[6].str() : "";
            info.isValid = true;
        }
        else
        {
            // Try to match host:port or just host (IP address or hostname)
            std::regex host_port_regex(
                R"(^([^:\/\s]+)(?::(\d+))?(?:\/([^?\s]*))?(?:\?([^#\s]*))?(?:#([^\s]*))?$)");

            if (std::regex_match(Url, matches, host_port_regex))
            {
                info.scheme = "http"; // Default to http for host-only input
                info.host = matches[1].str();

                // Parse port
                if (matches[2].matched)
                {
                    try
                    {
                        info.port = std::stoi(matches[2].str());
                    }
                    catch (const std::exception &)
                    {
                        info.port = getDefaultPort(info.scheme);
                    }
                }
                else
                {
                    info.port = getDefaultPort(info.scheme);
                }

                info.path = matches[3].matched ? "/" + matches[3].str() : "/";
                info.query = matches[4].matched ? matches[4].str() : "";
                info.fragment = matches[5].matched ? matches[5].str() : "";

                // Validate that host is either a valid IP address or hostname
                if (NetworkUtils::isValidIPAddress(info.host) || isValidHostname(info.host))
                {
                    info.isValid = true;
                }
            }
        }

        return info;
    }

    std::string UrlUtils::extractEndpoint(const std::string &Url)
    {
        UrlInfo info = parseUrl(Url);
        if (info.isValid)
        {
            if (info.port != getDefaultPort(info.scheme) && info.port != 0)
            {
                return info.scheme + "://" + info.host + ":" + std::to_string(info.port);
            }
            else
            {
                return info.scheme + "://" + info.host;
            }
        }
        return "";
    }

    std::string UrlUtils::extractHost(const std::string &Url)
    {
        UrlInfo info = parseUrl(Url);
        return info.isValid ? info.host : "";
    }

    int UrlUtils::extractPort(const std::string &Url)
    {
        UrlInfo info = parseUrl(Url);
        return info.isValid ? info.port : 0;
    }

    std::string UrlUtils::extractScheme(const std::string &Url)
    {
        UrlInfo info = parseUrl(Url);
        return info.isValid ? info.scheme : "";
    }

    bool UrlUtils::isHTTPS(const std::string &Url)
    {
        std::string scheme = extractScheme(Url);
        return scheme == "https";
    }

    bool UrlUtils::isHTTP(const std::string &Url)
    {
        std::string scheme = extractScheme(Url);
        return scheme == "http";
    }

    bool UrlUtils::isValidUrl(const std::string &Url)
    {
        UrlInfo info = parseUrl(Url);
        return info.isValid;
    }

    std::string UrlUtils::buildUrl(const std::string &scheme, const std::string &host,
                                   int port, const std::string &path, const std::string &query)
    {
        if (!isValidScheme(scheme) || host.empty())
        {
            return "";
        }

        std::string Url = scheme + "://" + host;

        // Add port if not default
        int defaultPort = getDefaultPort(scheme);
        if (port != 0 && port != defaultPort)
        {
            Url += ":" + std::to_string(port);
        }

        // Add path
        if (!path.empty())
        {
            if (path[0] != '/')
            {
                Url += "/";
            }
            Url += path;
        }
        else
        {
            Url += "/";
        }

        // Add query
        if (!query.empty())
        {
            Url += "?" + query;
        }

        return Url;
    }

    std::string UrlUtils::UrlEncode(const std::string &str)
    {
        std::ostringstream encoded;
        encoded << std::hex << std::uppercase;

        for (unsigned char c : str)
        {
            // Only encode unreserved characters as per RFC 3986
            // Must use explicit ASCII range check to avoid locale-dependent behavior
            // that causes issues with UTF-8 multibyte characters on macOS
            if ((c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~')
            {
                encoded << c;
            }
            else
            {
                // Encode all other bytes (including UTF-8 multibyte sequences)
                encoded << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
            }
        }

        return encoded.str();
    }

    std::string UrlUtils::UrlDecode(const std::string &str)
    {
        std::string decoded;

        for (size_t i = 0; i < str.length(); ++i)
        {
            if (str[i] == '%' && i + 2 < str.length())
            {
                std::string hex = str.substr(i + 1, 2);
                try
                {
                    int value = std::stoi(hex, nullptr, 16);
                    decoded += static_cast<char>(value);
                    i += 2;
                }
                catch (const std::exception &)
                {
                    decoded += str[i];
                }
            }
            else if (str[i] == '+')
            {
                decoded += ' ';
            }
            else
            {
                decoded += str[i];
            }
        }

        return decoded;
    }

    int UrlUtils::getDefaultPort(const std::string &scheme)
    {
        if (scheme == "http")
            return 80;
        if (scheme == "https")
            return 443;
        return 0;
    }

    bool UrlUtils::isValidScheme(const std::string &scheme)
    {
        std::string lower_scheme = toLowercase(scheme);
        return lower_scheme == "http" || lower_scheme == "https";
    }

    bool UrlUtils::isValidHostname(const std::string &hostname)
    {
        if (hostname.empty() || hostname.length() > 253)
        {
            return false;
        }

        // Check for valid characters and format
        std::regex hostname_regex(R"(^[a-zA-Z0-9]([a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])?(\.[a-zA-Z0-9]([a-zA-Z0-9\-]{0,61}[a-zA-Z0-9])?)*$)");
        return std::regex_match(hostname, hostname_regex);
    }

    std::string UrlUtils::toLowercase(const std::string &str)
    {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }

    // ========== FileUtils Implementation ==========

    bool FileUtils::fileExists(const std::string &file_path)
    {
        // Use PathUtils for UTF-8 support
        return PathUtils::exists(file_path);
    }

    long FileUtils::getFileSize(const std::string &file_path)
    {
        // Use PathUtils for UTF-8 support
        auto size = PathUtils::fileSize(file_path);
        if (size == static_cast<std::uintmax_t>(-1))
        {
            return -1;
        }
        return static_cast<long>(size);
    }

    std::string FileUtils::readFile(const std::string &file_path)
    {
        // Use PathUtils for UTF-8 support
        auto file = PathUtils::openInputStream(file_path, std::ios::binary);
        if (!file.is_open())
        {
            return "";
        }

        std::ostringstream content;
        content << file.rdbuf();
        return content.str();
    }

    bool FileUtils::writeFile(const std::string &file_path, const std::string &content)
    {
        // Use PathUtils for UTF-8 support
        auto file = PathUtils::openOutputStream(file_path, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

        file << content;
        return file.good();
    }

    // Helper function to calculate MD5 binary hash of a file
    static bool calculateFileMD5Binary(const std::string &file_path, unsigned char *hash)
    {
        if (hash == nullptr)
        {
            return false;
        }

        // Use PathUtils for UTF-8 support
        auto file = PathUtils::openInputStream(file_path, std::ios::binary);
        if (!file.is_open())
        {
            return false;
        }

#ifdef _WIN32
        // Windows uses WinCrypt API
        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;

        if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        {
            return false;
        }

        if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash))
        {
            CryptReleaseContext(hProv, 0);
            return false;
        }

        const size_t bufferSize = 4096;
        char buffer[bufferSize];

        while (file.read(buffer, bufferSize) || file.gcount() > 0)
        {
            if (!CryptHashData(hHash, (BYTE *)buffer, (DWORD)file.gcount(), 0))
            {
                CryptDestroyHash(hHash);
                CryptReleaseContext(hProv, 0);
                return false;
            }
        }

        DWORD hashSize = 16; // MD5 is 16 bytes
        if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashSize, 0))
        {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            return false;
        }

        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return true;

#else
        // Linux/Unix uses OpenSSL
        MD5_CTX md5Context;
        MD5_Init(&md5Context);

        const size_t bufferSize = 4096;
        char buffer[bufferSize];

        while (file.read(buffer, bufferSize) || file.gcount() > 0)
        {
            MD5_Update(&md5Context, buffer, file.gcount());
        }

        MD5_Final(hash, &md5Context);
        return true;
#endif
    }

    std::string FileUtils::calculateMD5(const std::string &file_path)
    {
        unsigned char hash[16]; // MD5 is always 16 bytes (128 bits)

        if (!calculateFileMD5Binary(file_path, hash))
        {
            return "";
        }

        // Convert to hexadecimal string
        std::stringstream ss;
        for (int i = 0; i < 16; i++)
        {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }

        return ss.str();
    }

    std::string FileUtils::calculateMD5Base64(const std::string &file_path)
    {
        unsigned char hash[16]; // MD5 is always 16 bytes (128 bits)

        if (!calculateFileMD5Binary(file_path, hash))
        {
            return "";
        }

        // Encode the 16-byte binary hash to base64
        return CryptoUtils::encodeBase64(hash, 16);
    }

    std::string FileUtils::getFileExtension(const std::string &file_path)
    {
        size_t last_dot = file_path.find_last_of('.');
        if (last_dot == std::string::npos || last_dot == file_path.length() - 1)
        {
            return "";
        }
        return file_path.substr(last_dot + 1);
    }

    std::string FileUtils::getFileName(const std::string &file_path)
    {
        if (file_path.empty())
        {
            return "";
        }
        size_t last_slash = file_path.find_last_of("/\\");
        size_t last_dot = file_path.find_last_of('.');

        size_t start = (last_slash == std::string::npos) ? 0 : last_slash + 1;
        size_t end = (last_dot == std::string::npos || last_dot < start) ? file_path.length() : last_dot;

        return file_path.substr(start, end - start);
    }

    std::string FileUtils::getCurrentModuleDirectory()
    {
#ifdef _WIN32
        HMODULE hModule = nullptr;
        // Get the handle to the current DLL/module
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                (LPCWSTR)&FileUtils::getCurrentModuleDirectory,
                                &hModule))
        {
            return "";
        }

        wchar_t path[MAX_PATH];
        DWORD length = GetModuleFileNameW(hModule, path, MAX_PATH);
        if (length == 0 || length == MAX_PATH)
        {
            return "";
        }

        // Convert wide string to UTF-8
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
        if (size_needed <= 0)
        {
            return "";
        }

        std::string utf8Path(size_needed - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, path, -1, &utf8Path[0], size_needed, nullptr, nullptr);

        // Extract directory path
        size_t last_slash = utf8Path.find_last_of("/\\");
        if (last_slash == std::string::npos)
        {
            return "";
        }

        return utf8Path.substr(0, last_slash);
#elif defined(__APPLE__)
        // macOS implementation using dladdr
        Dl_info info;
        if (dladdr((void *)&FileUtils::getCurrentModuleDirectory, &info) == 0)
        {
            return "";
        }

        std::string path = info.dli_fname;
        size_t last_slash = path.find_last_of('/');
        if (last_slash == std::string::npos)
        {
            return "";
        }

        return path.substr(0, last_slash);
#else
        // Linux implementation using dladdr
        Dl_info info;
        if (dladdr((void *)&FileUtils::getCurrentModuleDirectory, &info) == 0)
        {
            return "";
        }

        std::string path = info.dli_fname;
        size_t last_slash = path.find_last_of('/');
        if (last_slash == std::string::npos)
        {
            return "";
        }

        return path.substr(0, last_slash);
#endif
    }

    // ========== TimeUtils Implementation ==========

    long long TimeUtils::getCurrentTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
        return timestamp.count();
    }

    std::string TimeUtils::getCurrentTimeString(const std::string &format)
    {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);

        std::ostringstream oss;
#ifdef _WIN32
        struct tm tm_buf;
        localtime_s(&tm_buf, &time_t);
        oss << std::put_time(&tm_buf, format.c_str());
#else
        oss << std::put_time(std::localtime(&time_t), format.c_str());
#endif
        return oss.str();
    }

    std::string TimeUtils::timestampToString(long long timestamp, const std::string &format)
    {
        auto time_point = std::chrono::system_clock::from_time_t(timestamp / 1000);
        auto time_t = std::chrono::system_clock::to_time_t(time_point);

        std::ostringstream oss;
#ifdef _WIN32
        struct tm tm_buf;
        localtime_s(&tm_buf, &time_t);
        oss << std::put_time(&tm_buf, format.c_str());
#else
        oss << std::put_time(std::localtime(&time_t), format.c_str());
#endif
        return oss.str();
    }

    long long TimeUtils::getTimeDifference(const std::chrono::system_clock::time_point &start,
                                           const std::chrono::system_clock::time_point &end)
    {
        auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        return diff.count();
    }

    // ========== CryptoUtils Implementation ==========

    std::string CryptoUtils::generateUUID()
    {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        static std::uniform_int_distribution<> dis2(8, 11);

        std::stringstream ss;
        ss << std::hex;

        for (int i = 0; i < 8; i++)
        {
            ss << dis(gen);
        }
        ss << "-";

        for (int i = 0; i < 4; i++)
        {
            ss << dis(gen);
        }
        ss << "-4"; // Version 4 UUID

        for (int i = 0; i < 3; i++)
        {
            ss << dis(gen);
        }
        ss << "-";

        ss << dis2(gen); // Variant bits

        for (int i = 0; i < 3; i++)
        {
            ss << dis(gen);
        }
        ss << "-";

        for (int i = 0; i < 12; i++)
        {
            ss << dis(gen);
        }

        return ss.str();
    }

    std::string CryptoUtils::getMachineId()
    {
        std::string machine_id;

#ifdef _WIN32
        // Windows implementation - simple and reliable
        try
        {
            // Method 1: Try to get MachineGuid from system registry
            HKEY hKey = nullptr;
            LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                                        "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ, &hKey);

            if (result == ERROR_SUCCESS && hKey != nullptr)
            {
                char data[256] = {0};
                DWORD dataSize = sizeof(data) - 1;
                DWORD type = 0;

                result = RegQueryValueExA(hKey, "MachineGuid", nullptr, &type,
                                          reinterpret_cast<LPBYTE>(data), &dataSize);

                if (result == ERROR_SUCCESS && type == REG_SZ && dataSize > 0)
                {
                    data[dataSize] = '\0';
                    machine_id = std::string(data);
                }
                RegCloseKey(hKey);
            }

            // Method 2: If no system MachineGuid, try to read from user registry
            if (machine_id.empty())
            {
                result = RegOpenKeyExA(HKEY_CURRENT_USER,
                                       "SOFTWARE\\Elegoo\\Network", 0, KEY_READ, &hKey);

                if (result == ERROR_SUCCESS && hKey != nullptr)
                {
                    char data[256] = {0};
                    DWORD dataSize = sizeof(data) - 1;
                    DWORD type = 0;

                    result = RegQueryValueExA(hKey, "MachineId", nullptr, &type,
                                              reinterpret_cast<LPBYTE>(data), &dataSize);

                    if (result == ERROR_SUCCESS && type == REG_SZ && dataSize > 0)
                    {
                        data[dataSize] = '\0';
                        machine_id = std::string(data);
                    }
                    RegCloseKey(hKey);
                }
            }

            // Method 3: If still no ID, generate one using existing UUID function and save to user registry
            if (machine_id.empty())
            {
                // Generate a new UUID as machine ID
                machine_id = generateUUID();

                // Try to save the generated ID to user registry (no admin rights needed)
                HKEY hUserKey = nullptr;
                result = RegCreateKeyExA(HKEY_CURRENT_USER, "SOFTWARE\\Elegoo\\Network",
                                         0, nullptr, 0, KEY_WRITE, nullptr, &hUserKey, nullptr);

                if (result == ERROR_SUCCESS && hUserKey != nullptr)
                {
                    RegSetValueExA(hUserKey, "MachineId", 0, REG_SZ,
                                   reinterpret_cast<const BYTE *>(machine_id.c_str()),
                                   static_cast<DWORD>(machine_id.length() + 1));
                    RegCloseKey(hUserKey);
                }
            }
        }
        catch (...)
        {
            // Fallback: generate a new UUID
            machine_id = generateUUID();
        }

#elif defined(__APPLE__)
        // macOS implementation - use IOKit API to get hardware serial number
        try
        {
            // Method 1: Try to get hardware serial number using IOKit
#if defined(__APPLE__) && defined(kIOMainPortDefault)
            io_service_t platformExpert = IOServiceGetMatchingService(kIOMainPortDefault,
                                                                      IOServiceMatching("IOPlatformExpertDevice"));
#else
            io_service_t platformExpert = IOServiceGetMatchingService(kIOMasterPortDefault,
                                                                      IOServiceMatching("IOPlatformExpertDevice"));
#endif

            if (platformExpert)
            {
                CFStringRef serialNumberAsCFString = (CFStringRef)IORegistryEntryCreateCFProperty(
                    platformExpert,
                    CFSTR(kIOPlatformSerialNumberKey),
                    kCFAllocatorDefault, 0);

                if (serialNumberAsCFString)
                {
                    char serialNumber[256];
                    Boolean result = CFStringGetCString(serialNumberAsCFString,
                                                        serialNumber, sizeof(serialNumber), kCFStringEncodingUTF8);

                    if (result)
                    {
                        machine_id = std::string(serialNumber);
                    }

                    CFRelease(serialNumberAsCFString);
                }
                IOObjectRelease(platformExpert);
            }

            // Method 2: If serial number not available, try to get system UUID
            if (machine_id.empty())
            {
#if defined(__APPLE__) && defined(kIOMainPortDefault)
                CFStringRef systemUUID = (CFStringRef)IORegistryEntryCreateCFProperty(
                    IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOPlatformExpertDevice")),
                    CFSTR(kIOPlatformUUIDKey),
                    kCFAllocatorDefault, 0);
#else
                CFStringRef systemUUID = (CFStringRef)IORegistryEntryCreateCFProperty(
                    IOServiceGetMatchingService(kIOMasterPortDefault, IOServiceMatching("IOPlatformExpertDevice")),
                    CFSTR(kIOPlatformUUIDKey),
                    kCFAllocatorDefault, 0);
#endif

                if (systemUUID)
                {
                    char uuid[256];
                    Boolean result = CFStringGetCString(systemUUID,
                                                        uuid, sizeof(uuid), kCFStringEncodingUTF8);

                    if (result)
                    {
                        machine_id = std::string(uuid);
                    }

                    CFRelease(systemUUID);
                }
            }

            // Method 3: Fallback to system info if no hardware ID found
            if (machine_id.empty())
            {
                struct utsname unameData;
                if (uname(&unameData) == 0)
                {
                    machine_id = std::string(unameData.sysname) + "_" +
                                 std::string(unameData.nodename) + "_" +
                                 std::string(unameData.machine);
                }
            }
        }
        catch (...)
        {
            // Ultimate fallback - generate UUID
            machine_id = generateUUID();
        }

#else
        // Linux and other Unix-like systems
        try
        {
            // Try to read machine-id first
            std::ifstream machine_id_file("/etc/machine-id");
            if (machine_id_file.is_open())
            {
                std::getline(machine_id_file, machine_id);
                machine_id_file.close();
            }
            else
            {
                // Fallback: try /var/lib/dbus/machine-id
                std::ifstream dbus_id_file("/var/lib/dbus/machine-id");
                if (dbus_id_file.is_open())
                {
                    std::getline(dbus_id_file, machine_id);
                    dbus_id_file.close();
                }
            }

            // If still no machine ID, generate one based on system info
            if (machine_id.empty())
            {
                struct utsname unameData;
                if (uname(&unameData) == 0)
                {
                    machine_id = std::string(unameData.sysname) + "_" +
                                 std::string(unameData.nodename) + "_" +
                                 std::string(unameData.machine);
                }
            }

            // Add process info for additional uniqueness
            machine_id += "_" + std::to_string(getpid());
        }
        catch (...)
        {
            // Ultimate fallback
            machine_id = "UNIX_FALLBACK_" + std::to_string(time(nullptr));
        }
#endif

        // Hash the machine ID using MD5 for consistent format
        std::string hashed_id = calculateMD5(machine_id);

        // If MD5 hashing failed, use fallback
        if (hashed_id.empty())
        {
            std::hash<std::string> hasher;
            size_t hash = hasher(machine_id);
            std::stringstream ss;
            ss << std::hex << hash;
            hashed_id = ss.str();

            // Ensure consistent 32-character length (MD5 produces 32 hex characters)
            if (hashed_id.length() < 32)
            {
                hashed_id += std::string(32 - hashed_id.length(), '0');
            }
            else if (hashed_id.length() > 32)
            {
                hashed_id = hashed_id.substr(0, 32);
            }
        }

        return hashed_id;
    }

    std::string CryptoUtils::getCachedMachineId()
    {
        static std::string cached_machine_id;
        static bool initialized = false;

        if (!initialized)
        {
            cached_machine_id = getMachineId();
            initialized = true;
        }

        return cached_machine_id;
    }

    bool CryptoUtils::calculateMD5Binary(const char *data, size_t size, unsigned char *hash)
    {
        if (data == nullptr || size == 0 || hash == nullptr)
        {
            return false;
        }

#ifdef _WIN32
        // Windows: Use WinCrypt API for MD5
        HCRYPTPROV hProv = 0;
        HCRYPTHASH hHash = 0;

        if (!CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
        {
            return false;
        }

        if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash))
        {
            CryptReleaseContext(hProv, 0);
            return false;
        }

        if (!CryptHashData(hHash, (BYTE *)data, (DWORD)size, 0))
        {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            return false;
        }

        DWORD hashSize = 16; // MD5 is 16 bytes
        if (!CryptGetHashParam(hHash, HP_HASHVAL, hash, &hashSize, 0))
        {
            CryptDestroyHash(hHash);
            CryptReleaseContext(hProv, 0);
            return false;
        }

        CryptDestroyHash(hHash);
        CryptReleaseContext(hProv, 0);
        return true;
#else
        // Unix/Linux: Use OpenSSL for MD5
        MD5_CTX md5Context;
        MD5_Init(&md5Context);
        MD5_Update(&md5Context, data, size);
        MD5_Final(hash, &md5Context);
        return true;
#endif
    }

    std::string CryptoUtils::calculateMD5(const std::string &input)
    {
        return calculateMD5(input.c_str(), input.length());
    }

    std::string CryptoUtils::calculateMD5(const char *data, size_t size)
    {
        unsigned char hash[16]; // MD5 is always 16 bytes (128 bits)

        if (!calculateMD5Binary(data, size, hash))
        {
            return "";
        }

        // Convert to lowercase hexadecimal string
        std::stringstream ss;
        for (int i = 0; i < 16; i++)
        {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }

        return ss.str();
    }

    std::string CryptoUtils::encodeBase64(const unsigned char *data, size_t size)
    {
        if (data == nullptr || size == 0)
        {
            return "";
        }

        // Base64 encoding table
        static const char base64_chars[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";

        std::string result;
        result.reserve(((size + 2) / 3) * 4); // Each 3 bytes -> 4 base64 chars

        int i = 0;
        int j = 0;
        unsigned char char_array_3[3];
        unsigned char char_array_4[4];

        for (size_t idx = 0; idx < size; idx++)
        {
            char_array_3[i++] = data[idx];
            if (i == 3)
            {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;

                for (i = 0; i < 4; i++)
                {
                    result += base64_chars[char_array_4[i]];
                }
                i = 0;
            }
        }

        // Handle remaining bytes
        if (i > 0)
        {
            for (j = i; j < 3; j++)
            {
                char_array_3[j] = '\0';
            }

            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

            for (j = 0; j < i + 1; j++)
            {
                result += base64_chars[char_array_4[j]];
            }

            while (i++ < 3)
            {
                result += '=';
            }
        }

        return result;
    }

    std::string CryptoUtils::calculateMD5Base64(const std::string &input)
    {
        return calculateMD5Base64(input.c_str(), input.length());
    }

    std::string CryptoUtils::calculateMD5Base64(const char *data, size_t size)
    {
        unsigned char hash[16]; // MD5 is always 16 bytes (128 bits)

        if (!calculateMD5Binary(data, size, hash))
        {
            return "";
        }

        // Encode the 16-byte binary hash to base64
        return encodeBase64(hash, 16);
    }

    // ========== PathUtils Implementation ==========

#ifdef _WIN32
    std::wstring PathUtils::utf8ToWide(const std::string &utf8_str)
    {
        if (utf8_str.empty())
        {
            return std::wstring();
        }

        int size_needed = MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), (int)utf8_str.size(), NULL, 0);
        if (size_needed == 0)
        {
            return std::wstring();
        }

        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, utf8_str.c_str(), (int)utf8_str.size(), &wstr[0], size_needed);
        return wstr;
    }
#endif

    std::ifstream PathUtils::openInputStream(const std::string &file_path, std::ios_base::openmode mode)
    {
#ifdef _WIN32
        std::wstring wide_path = utf8ToWide(file_path);
        return std::ifstream(wide_path, mode);
#else
        return std::ifstream(file_path, mode);
#endif
    }

    std::ofstream PathUtils::openOutputStream(const std::string &file_path, std::ios_base::openmode mode)
    {
#ifdef _WIN32
        std::wstring wide_path = utf8ToWide(file_path);
        return std::ofstream(wide_path, mode);
#else
        return std::ofstream(file_path, mode);
#endif
    }

    bool PathUtils::exists(const std::string &file_path)
    {
#ifdef _WIN32
        std::wstring wide_path = utf8ToWide(file_path);
        // Use lexically_normal to resolve .. and . without filesystem access
        auto normalized = std::filesystem::path(wide_path).lexically_normal();
        return std::filesystem::exists(normalized);
#else
        // Use lexically_normal to resolve .. and . without filesystem access
        auto normalized = std::filesystem::path(file_path).lexically_normal();
        return std::filesystem::exists(normalized);
#endif
    }

    std::uintmax_t PathUtils::fileSize(const std::string &file_path)
    {
#ifdef _WIN32
        std::wstring wide_path = utf8ToWide(file_path);
        std::error_code ec;
        auto size = std::filesystem::file_size(wide_path, ec);
        return ec ? static_cast<std::uintmax_t>(-1) : size;
#else
        std::error_code ec;
        auto size = std::filesystem::file_size(file_path, ec);
        return ec ? static_cast<std::uintmax_t>(-1) : size;
#endif
    }

    bool PathUtils::isRegularFile(const std::string &file_path)
    {
#ifdef _WIN32
        std::wstring wide_path = utf8ToWide(file_path);
        return std::filesystem::exists(wide_path) && std::filesystem::is_regular_file(wide_path);
#else
        return std::filesystem::exists(file_path) && std::filesystem::is_regular_file(file_path);
#endif
    }

    bool PathUtils::isDirectory(const std::string &file_path)
    {
#ifdef _WIN32
        std::wstring wide_path = utf8ToWide(file_path);
        return std::filesystem::exists(wide_path) && std::filesystem::is_directory(wide_path);
#else
        return std::filesystem::exists(file_path) && std::filesystem::is_directory(file_path);
#endif
    }

    std::vector<std::pair<std::string, bool>> PathUtils::listDirectory(const std::string &dir_path)
    {
        std::vector<std::pair<std::string, bool>> result;

        try
        {
#ifdef _WIN32
            std::wstring wide_path = utf8ToWide(dir_path);
            for (const auto &entry : std::filesystem::directory_iterator(wide_path))
            {
                // Convert back to UTF-8
                std::wstring wide_filename = entry.path().filename().wstring();
                int size_needed = WideCharToMultiByte(CP_UTF8, 0, wide_filename.c_str(), (int)wide_filename.size(), NULL, 0, NULL, NULL);
                if (size_needed > 0)
                {
                    std::string utf8_filename(size_needed, 0);
                    WideCharToMultiByte(CP_UTF8, 0, wide_filename.c_str(), (int)wide_filename.size(), &utf8_filename[0], size_needed, NULL, NULL);
                    result.emplace_back(utf8_filename, entry.is_directory());
                }
            }
#else
            for (const auto &entry : std::filesystem::directory_iterator(dir_path))
            {
                result.emplace_back(entry.path().filename().string(), entry.is_directory());
            }
#endif
        }
        catch (const std::exception &)
        {
            // Return empty result on error
        }

        return result;
    }

} // namespace elegoo::cloud
