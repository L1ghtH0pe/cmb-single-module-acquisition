#include "common/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace cmb::common {

Logger::Logger(const std::filesystem::path& path) : stream_(path, std::ios::app) {
    if (!stream_) {
        throw std::runtime_error("failed to open log file");
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard lock(mutex_);
    stream_ << now_iso8601() << " [" << to_string(level) << "] " << message << '\n';
    stream_.flush();
}

std::string to_string(LogLevel level) {
    switch (level) {
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarn:
            return "WARN";
        case LogLevel::kError:
            return "ERROR";
    }
    return "UNKNOWN";
}

std::string now_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

}  // namespace cmb::common
