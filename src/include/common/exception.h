#pragma once

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace francodb {

    enum class ExceptionType {
        INVALID = 0,        // Default / Unknown error
        OUT_OF_RANGE = 1,   // Accessing array index 50 in size 10 array
        CONVERSION = 2,     // Failed to cast types
        UNKNOWN_TYPE = 3,   // Unknown Page type or Value type
        DECIMAL = 4,        // Decimal math errors
        MISMATCH_TYPE = 5,  // Comparing String with Integer
        DIVIDE_BY_ZERO = 6, 
        OBJECT_SIZE = 7,    // Object too large for page
        INCOMPLETE = 8,     // "I haven't coded this yet"
        NOT_IMPLEMENTED = 9,// "I will never code this"
        EXECUTION = 10,     // Executor failed
        PARSER = 11         // Parser syntax errors
    };

    class Exception : public std::exception {
    public:
        // Constructor: Type + Message
        Exception(ExceptionType type, const std::string &message) 
            : type_(type), message_(message) {}

        // Constructor: Just Message (Defaults to INVALID)
        explicit Exception(const std::string &message) 
            : type_(ExceptionType::INVALID), message_(message) {}

        // The standard function that prints the error
        const char *what() const noexcept override {
            return message_.c_str();
        }

        ExceptionType GetType() const { return type_; }

    private:
        ExceptionType type_;
        std::string message_;
    };

} // namespace francodb