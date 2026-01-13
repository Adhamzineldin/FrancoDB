#pragma once

#include <string>
#include "common/type.h"

namespace francodb {

    class Column {
    public:
        // Constructor for Fixed-Length types (Int, Bool)
        Column(std::string name, TypeId type)
            : name_(std::move(name)), type_(type), length_(Type::GetTypeSize(type)) {}

        // Constructor for Variable-Length types (Varchar)
        Column(std::string name, TypeId type, uint32_t length)
            : name_(std::move(name)), type_(type), length_(length) {}

        // Getters
        std::string GetName() const { return name_; }
        TypeId GetType() const { return type_; }
        uint32_t GetLength() const { return length_; }
        uint32_t GetOffset() const { return column_offset_; }
    
        // Setters (Used by Schema class to verify alignment)
        void SetOffset(uint32_t offset) { column_offset_ = offset; }

        // Debug
        std::string ToString() const {
            return name_ + ":" + Type::TypeToString(type_);
        }
        
        

    private:
        std::string name_;
        TypeId type_;
        uint32_t length_;        // Max length for Varchar, fixed for others
        uint32_t column_offset_; // Byte offset in the tuple (0, 4, 8...)
    };

} // namespace francodb