#pragma once

#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <memory>
#include <atomic>
#include <functional>

namespace chronosdb {

/**
 * Logger - Production-grade logging system
 *
 * Features:
 * - Multiple log levels (TRACE, DEBUG, INFO, WARN, ERROR, FATAL)
 * - Thread-safe with minimal lock contention
 * - File and console output
 * - Structured logging with context
 * - Performance metrics tracking
 * - Async-safe design
 */
class Logger {
public:
    enum class Level {
        TRACE = 0,
        DEBUG = 1,
        INFO = 2,
        WARN = 3,
        ERROR = 4,
        FATAL = 5,
        OFF = 6
    };

    struct LogContext {
        std::string component;
        std::string operation;
        int64_t duration_us = -1;
        int records_processed = -1;

        LogContext& Component(const std::string& c) { component = c; return *this; }
        LogContext& Operation(const std::string& o) { operation = o; return *this; }
        LogContext& Duration(int64_t us) { duration_us = us; return *this; }
        LogContext& Records(int r) { records_processed = r; return *this; }
    };

    // Singleton access
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    // Configuration
    void SetLevel(Level level) { min_level_ = level; }
    void SetFileOutput(const std::string& path) {
        std::lock_guard<std::mutex> lock(file_mutex_);
        if (log_file_.is_open()) log_file_.close();
        log_file_.open(path, std::ios::app);
        file_logging_enabled_ = log_file_.is_open();
    }
    void SetConsoleOutput(bool enabled) { console_enabled_ = enabled; }
    void SetAsyncMode(bool enabled) { async_mode_ = enabled; }
    Level GetLevel() const { return min_level_; }

    // Core logging methods
    template<typename... Args>
    void Log(Level level, const std::string& component, const char* fmt, Args&&... args) {
        if (level < min_level_) return;

        std::string message = Format(fmt, std::forward<Args>(args)...);
        WriteLog(level, component, message);
    }

    void Log(Level level, const std::string& component, const std::string& message) {
        if (level < min_level_) return;
        WriteLog(level, component, message);
    }

    void LogWithContext(Level level, const LogContext& ctx, const std::string& message) {
        if (level < min_level_) return;

        std::ostringstream oss;
        oss << message;
        if (ctx.duration_us >= 0) oss << " [" << ctx.duration_us / 1000 << "ms]";
        if (ctx.records_processed >= 0) oss << " [" << ctx.records_processed << " records]";

        WriteLog(level, ctx.component, oss.str());
    }

    // Convenience macros implemented as methods
    template<typename... Args>
    void Trace(const std::string& component, const char* fmt, Args&&... args) {
        Log(Level::TRACE, component, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Debug(const std::string& component, const char* fmt, Args&&... args) {
        Log(Level::DEBUG, component, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Info(const std::string& component, const char* fmt, Args&&... args) {
        Log(Level::INFO, component, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Warn(const std::string& component, const char* fmt, Args&&... args) {
        Log(Level::WARN, component, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Error(const std::string& component, const char* fmt, Args&&... args) {
        Log(Level::ERROR, component, fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Fatal(const std::string& component, const char* fmt, Args&&... args) {
        Log(Level::FATAL, component, fmt, std::forward<Args>(args)...);
    }

    // Simple string versions
    void Trace(const std::string& component, const std::string& msg) { Log(Level::TRACE, component, msg); }
    void Debug(const std::string& component, const std::string& msg) { Log(Level::DEBUG, component, msg); }
    void Info(const std::string& component, const std::string& msg) { Log(Level::INFO, component, msg); }
    void Warn(const std::string& component, const std::string& msg) { Log(Level::WARN, component, msg); }
    void Error(const std::string& component, const std::string& msg) { Log(Level::ERROR, component, msg); }
    void Fatal(const std::string& component, const std::string& msg) { Log(Level::FATAL, component, msg); }

    // Performance tracking
    class ScopedTimer {
    public:
        ScopedTimer(Logger& logger, Level level, const std::string& component,
                   const std::string& operation, int threshold_ms = 100)
            : logger_(logger), level_(level), component_(component),
              operation_(operation), threshold_ms_(threshold_ms),
              start_(std::chrono::high_resolution_clock::now()) {}

        ~ScopedTimer() {
            auto end = std::chrono::high_resolution_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start_).count();
            if (ms >= threshold_ms_) {
                logger_.Log(level_, component_, "%s completed in %lldms",
                           operation_.c_str(), static_cast<long long>(ms));
            }
        }

        void SetRecords(int count) { records_ = count; }

    private:
        Logger& logger_;
        Level level_;
        std::string component_;
        std::string operation_;
        int threshold_ms_;
        int records_ = 0;
        std::chrono::high_resolution_clock::time_point start_;
    };

    ScopedTimer TimeOperation(Level level, const std::string& component,
                              const std::string& operation, int threshold_ms = 100) {
        return ScopedTimer(*this, level, component, operation, threshold_ms);
    }

private:
    Logger() : min_level_(Level::INFO), console_enabled_(true),
               file_logging_enabled_(false), async_mode_(false) {}
    ~Logger() {
        if (log_file_.is_open()) log_file_.close();
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void WriteLog(Level level, const std::string& component, const std::string& message) {
        std::ostringstream oss;

        // Timestamp
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        oss << '.' << std::setfill('0') << std::setw(3) << ms.count();

        // Level
        oss << " [" << LevelToString(level) << "]";

        // Component
        if (!component.empty()) {
            oss << " [" << component << "]";
        }

        // Message
        oss << " " << message << "\n";

        std::string formatted = oss.str();

        // Write to console
        if (console_enabled_) {
            std::lock_guard<std::mutex> lock(console_mutex_);
            if (level >= Level::ERROR) {
                std::cerr << formatted;
            } else {
                std::cout << formatted;
            }
        }

        // Write to file
        if (file_logging_enabled_) {
            std::lock_guard<std::mutex> lock(file_mutex_);
            if (log_file_.is_open()) {
                log_file_ << formatted;
                log_file_.flush();
            }
        }
    }

    static const char* LevelToString(Level level) {
        switch (level) {
            case Level::TRACE: return "TRACE";
            case Level::DEBUG: return "DEBUG";
            case Level::INFO:  return "INFO ";
            case Level::WARN:  return "WARN ";
            case Level::ERROR: return "ERROR";
            case Level::FATAL: return "FATAL";
            default: return "?????";
        }
    }

    // Simple format function
    template<typename... Args>
    std::string Format(const char* fmt, Args&&... args) {
        // Use snprintf for formatting
        char buffer[4096];
        int len = std::snprintf(buffer, sizeof(buffer), fmt, std::forward<Args>(args)...);
        if (len < 0) return fmt;
        return std::string(buffer, std::min(static_cast<size_t>(len), sizeof(buffer) - 1));
    }

    std::string Format(const char* fmt) {
        return std::string(fmt);
    }

    std::atomic<Level> min_level_;
    std::atomic<bool> console_enabled_;
    std::atomic<bool> file_logging_enabled_;
    std::atomic<bool> async_mode_;

    std::mutex console_mutex_;
    std::mutex file_mutex_;
    std::ofstream log_file_;
};

// Convenience macros for compile-time component names
#define LOG_TRACE(component, ...) chronosdb::Logger::Instance().Trace(component, __VA_ARGS__)
#define LOG_DEBUG(component, ...) chronosdb::Logger::Instance().Debug(component, __VA_ARGS__)
#define LOG_INFO(component, ...) chronosdb::Logger::Instance().Info(component, __VA_ARGS__)
#define LOG_WARN(component, ...) chronosdb::Logger::Instance().Warn(component, __VA_ARGS__)
#define LOG_ERROR(component, ...) chronosdb::Logger::Instance().Error(component, __VA_ARGS__)
#define LOG_FATAL(component, ...) chronosdb::Logger::Instance().Fatal(component, __VA_ARGS__)

// Scoped timer macro
#define LOG_TIMED(component, operation) \
    auto _timer_##__LINE__ = chronosdb::Logger::Instance().TimeOperation( \
        chronosdb::Logger::Level::INFO, component, operation)

} // namespace chronosdb
