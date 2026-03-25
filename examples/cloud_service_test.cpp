#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "elegoo_link.h"

#ifndef ENABLE_CLOUD_FEATURES
int main()
{
    std::cout << "Cloud APIs are not enabled in this build.\n"
              << "Reconfigure with -DENABLE_CLOUD_FEATURES=ON and rebuild examples." << std::endl;
    return 0;
}
#else
using namespace elink;

namespace
{
    struct CloudInitConfig
    {
        bool showHelp = false;
        std::string region = "us";
        std::string baseUrl="https://matrix.elegoo.com";
        std::string caCertPath="C:\\Program Files\\ElegooSlicer\\resources\\cert\\cacert.pem";
        std::string userAgent = "elegoo-link-cloud-service-test/1.0";

        std::string userId="";
        std::string accessToken="";
        std::string refreshToken="";
        int64_t accessTokenExpireTime = 0;
        int64_t refreshTokenExpireTime = 0;
    };

    std::string envOrEmpty(const char *key)
    {
        const char *value = std::getenv(key);
        return value ? std::string(value) : std::string();
    }

    bool parseInt64(const std::string &text, int64_t &value)
    {
        try
        {
            std::size_t pos = 0;
            value = std::stoll(text, &pos);
            return pos == text.size();
        }
        catch (...)
        {
            return false;
        }
    }

    bool parseArgs(int argc, char *argv[], CloudInitConfig &config)
    {
        auto readNext = [&](int &index, std::string &output) -> bool
        {
            if (index + 1 >= argc)
            {
                return false;
            }
            output = argv[++index];
            return true;
        };

        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "-h" || arg == "--help")
            {
                config.showHelp = true;
                return true;
            }
            if (arg == "--region")
            {
                if (!readNext(i, config.region))
                {
                    return false;
                }
            }
            else if (arg == "--base-url")
            {
                if (!readNext(i, config.baseUrl))
                {
                    return false;
                }
            }
            else if (arg == "--ca-cert")
            {
                if (!readNext(i, config.caCertPath))
                {
                    return false;
                }
            }
            else if (arg == "--user-agent")
            {
                if (!readNext(i, config.userAgent))
                {
                    return false;
                }
            }
            else if (arg == "--user-id")
            {
                if (!readNext(i, config.userId))
                {
                    return false;
                }
            }
            else if (arg == "--access-token")
            {
                if (!readNext(i, config.accessToken))
                {
                    return false;
                }
            }
            else if (arg == "--refresh-token")
            {
                if (!readNext(i, config.refreshToken))
                {
                    return false;
                }
            }
            else if (arg == "--access-expire")
            {
                std::string valueText;
                if (!readNext(i, valueText) || !parseInt64(valueText, config.accessTokenExpireTime))
                {
                    return false;
                }
            }
            else if (arg == "--refresh-expire")
            {
                std::string valueText;
                if (!readNext(i, valueText) || !parseInt64(valueText, config.refreshTokenExpireTime))
                {
                    return false;
                }
            }
            else
            {
                std::cerr << "Unknown option: " << arg << std::endl;
                return false;
            }
        }
        return true;
    }

    void loadFromEnv(CloudInitConfig &config)
    {
        const std::string envRegion = envOrEmpty("ELINK_REGION");
        if (!envRegion.empty() && config.region == "us")
        {
            config.region = envRegion;
        }

        const std::string envBaseUrl = envOrEmpty("ELINK_BASE_URL");
        if (!envBaseUrl.empty() && config.baseUrl.empty())
        {
            config.baseUrl = envBaseUrl;
        }

        const std::string envCaCertPath = envOrEmpty("ELINK_CA_CERT_PATH");
        if (!envCaCertPath.empty() && config.caCertPath.empty())
        {
            config.caCertPath = envCaCertPath;
        }

        const std::string envUserAgent = envOrEmpty("ELINK_USER_AGENT");
        if (!envUserAgent.empty() && config.userAgent == "elegoo-link-cloud-service-test/1.0")
        {
            config.userAgent = envUserAgent;
        }

        if (config.userId.empty())
        {
            config.userId = envOrEmpty("ELINK_USER_ID");
        }
        if (config.accessToken.empty())
        {
            config.accessToken = envOrEmpty("ELINK_ACCESS_TOKEN");
        }
        if (config.refreshToken.empty())
        {
            config.refreshToken = envOrEmpty("ELINK_REFRESH_TOKEN");
        }

        if (config.accessTokenExpireTime == 0)
        {
            int64_t value = 0;
            if (parseInt64(envOrEmpty("ELINK_ACCESS_EXPIRE_TIME"), value))
            {
                config.accessTokenExpireTime = value;
            }
        }
        if (config.refreshTokenExpireTime == 0)
        {
            int64_t value = 0;
            if (parseInt64(envOrEmpty("ELINK_REFRESH_EXPIRE_TIME"), value))
            {
                config.refreshTokenExpireTime = value;
            }
        }
    }

    void printHelp(const char *exeName)
    {
        std::cout << "Usage: " << exeName << " [options]\n\n"
                  << "Only two steps are executed:\n"
                  << "  1) ElegooLink initialize\n"
                  << "  2) setHttpCredential\n\n"
                  << "Options:\n"
                  << "  --region <us|eu|cn>\n"
                  << "  --base-url <url>\n"
                  << "  --ca-cert <path>\n"
                  << "  --user-agent <value>\n"
                  << "  --user-id <id>\n"
                  << "  --access-token <token>\n"
                  << "  --refresh-token <token>\n"
                  << "  --access-expire <unixSec>\n"
                  << "  --refresh-expire <unixSec>\n"
                  << "  -h, --help\n\n"
                  << "Environment fallback:\n"
                  << "  ELINK_REGION ELINK_BASE_URL ELINK_CA_CERT_PATH ELINK_USER_AGENT\n"
                  << "  ELINK_USER_ID ELINK_ACCESS_TOKEN ELINK_REFRESH_TOKEN\n"
                  << "  ELINK_ACCESS_EXPIRE_TIME ELINK_REFRESH_EXPIRE_TIME\n";
    }
} // namespace

int main(int argc, char *argv[])
{
    CloudInitConfig config;
    if (!parseArgs(argc, argv, config))
    {
        printHelp(argv[0]);
        return 1;
    }
    if (config.showHelp)
    {
        printHelp(argv[0]);
        return 0;
    }

    loadFromEnv(config);

    if (config.accessToken.empty())
    {
        std::cerr << "Missing access token. Please provide --access-token or ELINK_ACCESS_TOKEN." << std::endl;
        return 1;
    }

    auto &elegooLink = ElegooLink::getInstance();
    ElegooLink::Config sdkConfig;
    sdkConfig.log.logLevel = 2;
    sdkConfig.log.logEnableConsole = true;
    sdkConfig.log.logEnableFile = false;
    sdkConfig.cloud.region = config.region;
    sdkConfig.cloud.baseApiUrl = config.baseUrl;
    sdkConfig.cloud.caCertPath = config.caCertPath;
    sdkConfig.cloud.userAgent = config.userAgent;

    if (!elegooLink.initialize(sdkConfig))
    {
        std::cerr << "[FAIL] initialize()" << std::endl;
        return 1;
    }

    if (!elegooLink.isNetworkServiceEnabled())
    {
        std::cerr << "[FAIL] Network service is not enabled." << std::endl;
        elegooLink.cleanup();
        return 1;
    }

    HttpCredential credential;
    credential.userId = config.userId;
    credential.accessToken = config.accessToken;
    credential.refreshToken = config.refreshToken;
    credential.accessTokenExpireTime = config.accessTokenExpireTime;
    credential.refreshTokenExpireTime = config.refreshTokenExpireTime;

    auto result = elegooLink.setHttpCredential(credential);
    if (result.isSuccess())
    {
        std::cout << "[OK] initialize + setHttpCredential completed." << std::endl;
    }
    else
    {
        std::cout << "[FAIL] setHttpCredential | code=" << static_cast<int>(result.code)
                  << " | message=" << result.message << std::endl;
        elegooLink.cleanup();
        return 1;
    }

    getchar(); // Wait for user input before exiting
    elegooLink.cleanup();
    return 0;
}
#endif
