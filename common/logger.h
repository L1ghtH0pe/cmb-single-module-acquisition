#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace cmb::common {

enum class LogLevel {
    kInfo,
    kWarn,
    kError,
};

class Logger {
  public:
    explicit Logger(const std::filesystem::path& path);

    void log(LogLevel level, const std::string& message);

  private:
    std::ofstream stream_;
    std::mutex mutex_;
};

std::string to_string(LogLevel level);
std::string now_iso8601();

}  // namespace cmb::common
