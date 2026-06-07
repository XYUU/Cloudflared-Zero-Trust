// SPDX-License-Identifier: Apache-2.0
#include "cfd/log.hpp"
#include <atomic>
#include <ctime>
#include <mutex>
#include <cstring>

namespace cfd::log {

namespace {
std::atomic<Level> g_level{Level::Info};
std::mutex g_mu;

const char* level_str(Level l) noexcept {
    switch (l) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?";
}
}  // namespace

void set_level(Level lvl) noexcept { g_level.store(lvl, std::memory_order_relaxed); }
Level level() noexcept { return g_level.load(std::memory_order_relaxed); }

void emit(Level lvl, const char* file, int line, const char* fmt, ...) noexcept {
    char tsbuf[32];
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::strftime(tsbuf, sizeof(tsbuf), "%Y-%m-%dT%H:%M:%S", &tm);

    // Trim file path to basename without strdup'ing
    const char* base = std::strrchr(file, '/');
#if defined(_WIN32)
    const char* base2 = std::strrchr(file, '\\');
    if (base2 && (!base || base2 > base)) base = base2;
#endif
    base = base ? base + 1 : file;

    std::lock_guard<std::mutex> lk(g_mu);
    std::fprintf(stderr, "%s %s %s:%d  ", tsbuf, level_str(lvl), base, line);
    std::va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    std::fputc('\n', stderr);
}

}  // namespace cfd::log
