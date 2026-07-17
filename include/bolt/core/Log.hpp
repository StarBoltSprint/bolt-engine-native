#pragma once
#include <cstdio>
#include <string_view>

namespace bolt {

enum class LogLevel { Trace, Info, Warn, Error };

inline void log(LogLevel level, std::string_view msg) {
  const char* tag = "INFO";
  switch (level) {
    case LogLevel::Trace: tag = "TRACE"; break;
    case LogLevel::Info:  tag = "INFO"; break;
    case LogLevel::Warn:  tag = "WARN"; break;
    case LogLevel::Error: tag = "ERROR"; break;
  }
  std::fprintf(stderr, "[Bolt][%s] %.*s\n", tag, (int)msg.size(), msg.data());
}

inline void logInfo(std::string_view m)  { log(LogLevel::Info, m); }
inline void logWarn(std::string_view m)  { log(LogLevel::Warn, m); }
inline void logError(std::string_view m) { log(LogLevel::Error, m); }

} // namespace bolt
