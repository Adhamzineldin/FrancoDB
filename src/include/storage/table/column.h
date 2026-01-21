#pragma once

#include <string>
#include <optional>
#include "common/type.h"
#include "common/value.h"

namespace francodb {

    /**
     * Column: Represents a table column with constraint support
     * Follows Single Responsibility Principle
     * Uses std::optional for nullable columns (C++17 feature)
     */
    class Column {
    public:
        // Constructors with nullable support
        Column(std::string name, TypeId type, bool is_primary_key = false, 
               bool is_nullable = true, bool is_unique = false);
        
        Column(std::string name, TypeId type, uint32_t length, bool is_primary_key = false,
               bool is_nullable = true, bool is_unique = false);

        // Getters follow accessor pattern (SOLID - Single Responsibility)
        std::string GetName() const { return name_; }
        TypeId GetType() const { return type_; }
        uint32_t GetLength() const { return length_; }
        uint32_t GetOffset() const { return column_offset_; }
        bool IsPrimaryKey() const { return is_primary_key_; }
        bool IsNullable() const { return is_nullable_; }
        bool IsUnique() const { return is_unique_; }
        bool IsAutoIncrement() const { return is_auto_increment_; }
        const std::string& GetCheckConstraint() const { return check_constraint_; }
        bool HasCheckConstraint() const { return !check_constraint_.empty(); }
        
        // Optional: Default value support
        const std::optional<Value>& GetDefaultValue() const { return default_value_; }
        bool HasDefaultValue() const { return default_value_.has_value(); }
        
        // Setters
        void SetOffset(uint32_t offset) { column_offset_ = offset; }
        void SetPrimaryKey(bool is_pk) { is_primary_key_ = is_pk; }
        void SetNullable(bool is_nullable) { is_nullable_ = is_nullable; }
        void SetUnique(bool is_unique) { is_unique_ = is_unique; }
        void SetDefaultValue(const Value& value) { default_value_ = value; }
        void SetAutoIncrement(bool auto_inc) { is_auto_increment_ = auto_inc; }
        void SetCheckConstraint(const std::string& check) { check_constraint_ = check; }
        
        
        std::string ToString() const;
        
        // Constraint validation following Single Responsibility Principle
        bool ValidateValue(const Value& value) const;

    private:
        std::string name_;
        TypeId type_;
        uint32_t length_;                  // Max length for Varchar, fixed for others
        uint32_t column_offset_;           // Byte offset in the tuple
        bool is_primary_key_;              // PRIMARY KEY constraint
        bool is_nullable_;                 // NULLABLE constraint (default: true)
        bool is_unique_;                   // UNIQUE constraint
        bool is_auto_increment_ = false;   // AUTO_INCREMENT constraint
        std::optional<Value> default_value_;  // DEFAULT value (optional)
        std::string check_constraint_;     // CHECK constraint expression
    };

} // namespace francodb

