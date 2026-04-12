#pragma once
#include "Core/DisableAllWarnings.h"

PRAGMA_DISABLE_ALL_WARNINGS
#include "EASTL/fixed_string.h"
#include "EASTL/string.h"
#include <ostream>
PRAGMA_ENABLE_ALL_WARNINGS


namespace Lumina
{
    using FString                                   = eastl::basic_string<char>;
    using FStringView                               = eastl::string_view;
    using FFixedString                              = eastl::fixed_string<char, 255>;
    template<eastl_size_t S> using TFixedString     = eastl::fixed_string<char, S>;
    
    using FWString                                  = eastl::basic_string<wchar_t>;
    using FFixedWString                             = eastl::fixed_string<wchar_t, 255>;

    template<typename T>
    concept StringLike = requires(T s)
    {
        { s.length() } -> std::convertible_to<size_t>;
        { s.data() }   -> std::convertible_to<const char*>;
    };
    
    namespace StringUtils
    {
        inline FWString ToWideString(FStringView str) { return FWString(FWString::CtorConvert(), str.begin(), str.end()); }
        inline FWString ToWideString(const char* pStr) { return FWString(FWString::CtorConvert(), pStr); }
        inline FString FromWideString(const FWString& Str) { return FString(FString::CtorConvert(), Str); }
        
        inline FString FormatSize(size_t Bytes)
        {
            const char* Suffixes[] = { "B", "KB", "MB", "GB" };
            double Size = static_cast<double>(Bytes);
            int Suffix = 0;

            while (Size >= 1024.0 && Suffix < 3)
            {
                Size /= 1024.0;
                ++Suffix;
            }
            FString Value;
            std::format_to(std::back_inserter(Value), "{:.2f} {}", Size, Suffixes[Suffix]);
            return Value;
        }

    }
}

#define TCHAR_TO_UTF8(X) FString(FString::CtorConvert(), X).c_str()
#define UTF8_TO_TCHAR(X) FWString(FWString::CtorConvert(), X).c_str()

namespace std
{
    template <>
    struct formatter<Lumina::FString>
    {
        constexpr auto parse(std::format_parse_context& ctx)
        {
            return ctx.begin();
        }

        template <typename FormatContext>
        auto format(const Lumina::FString& str, FormatContext& ctx) const
        {
            return std::format_to(ctx.out(), "{}", str.c_str());
        }
    };
    
    template <>
    struct formatter<Lumina::FFixedString>
    {
        constexpr auto parse(std::format_parse_context& ctx)
        {
            return ctx.begin();
        }

        template <typename FormatContext>
        auto format(const Lumina::FFixedString& str, FormatContext& ctx) const
        {
            return std::format_to(ctx.out(), "{}", str.c_str());
        }
    };

    template <>
    struct formatter<eastl::string_view>
    {
        constexpr auto parse(format_parse_context& ctx)
        {
            return ctx.begin();
        }
        
        template <typename FormatContext>
        auto format(const eastl::string_view& str, FormatContext& ctx) const
        {
            return std::format_to(ctx.out(), "{}", std::string_view(str.data(), str.length()));
        }
    };
}


template <eastl_size_t S>
struct eastl::hash<eastl::fixed_string<char, S, true>>
{
    size_t operator()(const eastl::fixed_string<char, S, true>& str) const noexcept
    {
        return eastl::hash<eastl::string_view>{}(eastl::string_view(str.c_str(), str.length()));
    }
};

namespace eastl
{
    inline std::ostream& operator<<(std::ostream& os, const Lumina::FString& str)
    {
        os.write(str.c_str(), str.size());
        return os;
    }
}