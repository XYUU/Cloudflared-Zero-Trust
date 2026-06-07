// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdio>
#include <cstdarg>
#include <string_view>

namespace cfd::log {

enum class Level { Trace, Debug, Info, Warn, Error };

void set_level(Level lvl) noexcept;
Level level() noexcept;

void emit(Level lvl, const char* file, int line, const char* fmt, ...) noexcept
    __attribute__((format(printf, 4, 5)));

}  // namespace cfd::log

#define CFD_LOG(lvl, ...) \
    do { \
        if (static_cast<int>(lvl) >= static_cast<int>(::cfd::log::level())) \
            ::cfd::log::emit((lvl), __FILE__, __LINE__, __VA_ARGS__); \
    } while (0)

#define LOG_TRACE(...) CFD_LOG(::cfd::log::Level::Trace, __VA_ARGS__)
#define LOG_DEBUG(...) CFD_LOG(::cfd::log::Level::Debug, __VA_ARGS__)
#define LOG_INFO(...)  CFD_LOG(::cfd::log::Level::Info,  __VA_ARGS__)
#define LOG_WARN(...)  CFD_LOG(::cfd::log::Level::Warn,  __VA_ARGS__)
#define LOG_ERROR(...) CFD_LOG(::cfd::log::Level::Error, __VA_ARGS__)
