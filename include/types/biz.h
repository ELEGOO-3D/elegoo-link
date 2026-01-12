#pragma once

#include <string>
#include <optional>
#include <string_view>
#include "base.h"
#include <variant>

#ifdef BUILD_SERVICE_EXECUTABLE
#include <nlohmann/json.hpp>
#endif

namespace elink
{
    template <typename T = std::monostate>
    struct BizResult
    {
        ELINK_ERROR_CODE code = ELINK_ERROR_CODE::SUCCESS;
        std::string message = "ok";
        std::optional<T> data;

        // Default constructor
        BizResult() = default;

        // Perfect forwarding constructor, supports results with data
        template <typename U>
        BizResult(ELINK_ERROR_CODE code, std::string_view msg, U &&data_val)
            : code(code), message(msg), data(std::forward<U>(data_val)) {}

        // Constructor with only error code and message
        BizResult(ELINK_ERROR_CODE code, std::string_view msg)
            : code(code), message(msg), data(std::nullopt) {}

        // Constructor to convert from BizResult<std::monostate> (supports both lvalue and rvalue)
        BizResult(const BizResult<std::monostate> &other)
            : code(other.code), message(other.message), data(std::nullopt) {}
        // Static factory method - Success with data
        template <typename U>
        static BizResult<std::decay_t<U>> Ok(U &&val)
        {
            return BizResult<std::decay_t<U>>{ELINK_ERROR_CODE::SUCCESS, "ok", std::forward<U>(val)};
        }

        // Static factory method - Error response
        static BizResult<T> Error(ELINK_ERROR_CODE errCode, std::string_view msg)
        {
            return BizResult<T>{errCode, msg};
        }

        // Static factory method - Success without data
        static BizResult<T> Success()
        {
            return BizResult<T>{ELINK_ERROR_CODE::SUCCESS, "ok"};
        }

        // Check if successful
        bool isSuccess() const noexcept
        {
            return code == ELINK_ERROR_CODE::SUCCESS;
        }

        // Check if failed
        bool isError() const noexcept
        {
            return code != ELINK_ERROR_CODE::SUCCESS;
        }

        // Check if data exists
        bool hasValue() const noexcept
        {
            return data.has_value();
        }

        // Alias for hasValue() to maintain consistency with method naming
        bool hasData() const noexcept
        {
            return data.has_value();
        }

        // Get data (if exists), otherwise return default value
        template <typename U = T>
        T valueOr(U &&defaultValue) const &
        {
            return data.value_or(std::forward<U>(defaultValue));
        }

        template <typename U = T>
        T valueOr(U &&defaultValue) &&
        {
            return std::move(data).value_or(std::forward<U>(defaultValue));
        }

        // Get data reference (only if data exists)
        const T &value() const &
        {
            return data.value();
        }

        T &value() &
        {
            return data.value();
        }

        T &&value() &&
        {
            return std::move(data).value();
        }

        // Functional programming support - map operation
        template <typename F>
        auto map(F &&func) const -> BizResult<std::invoke_result_t<F, T>>
        {
            if (isError() || !hasValue())
            {
                return BizResult<std::invoke_result_t<F, T>>::Error(code, message);
            }
            return BizResult<std::invoke_result_t<F, T>>::Ok(func(data.value()));
        }

        // Functional programming support - flatMap operation
        template <typename F>
        auto flatMap(F &&func) const -> std::invoke_result_t<F, T>
        {
            if (isError() || !hasValue())
            {
                using ReturnType = std::invoke_result_t<F, T>;
                return ReturnType::Error(code, message);
            }
            return func(data.value());
        }

#ifdef BUILD_SERVICE_EXECUTABLE
        nlohmann::json toJson() const
        {
            nlohmann::json j;
            j["code"] = static_cast<int>(code);
            j["message"] = message;

            if (hasValue())
            {
                if constexpr (std::is_same_v<T, std::monostate>)
                {
                    // No data field for VoidResult when successful
                }
                else
                {
                    j["data"] = data.value();
                }
            }

            return j;
        }
#endif
    };

    // Type alias for better readability
    using VoidResult = BizResult<std::monostate>;
} // namespace elink
