#pragma once

// Minimal JSON reader/writer for the config service's own files (AD-9).
// Internal to core/config — NOT part of the public API. Hand-rolled on
// purpose: keeps sampler-core JUCE-free and dependency-free; the config
// service only ever parses files it (or a sibling instance) wrote, plus
// user-edited variants, so a small strict parser suffices. A future backend
// swap (SQLite, spine Deferred) stays behind ConfigService.

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace fbsampler::configjson {

class Value;
using ValuePtr = std::shared_ptr<Value>;

class Value {
public:
    enum class Type { null, boolean, number, string, array, object };

    Type type = Type::null;
    bool boolValue = false;
    double numberValue = 0.0;
    std::string stringValue;
    std::vector<ValuePtr> arrayItems;
    // std::map keeps key order deterministic for stable file diffs.
    std::map<std::string, ValuePtr> members;

    static ValuePtr makeNull();
    static ValuePtr makeBool(bool b);
    static ValuePtr makeNumber(double n);
    static ValuePtr makeInt(std::int64_t n);
    static ValuePtr makeString(std::string s);
    static ValuePtr makeArray();
    static ValuePtr makeObject();

    bool isObject() const { return type == Type::object; }
    bool isArray() const { return type == Type::array; }

    // Object lookups (null when absent or wrong type).
    const Value* find(const std::string& key) const;
    std::int64_t getInt(const std::string& key, std::int64_t fallback) const;
    double getNumber(const std::string& key, double fallback) const;
    std::string getString(const std::string& key,
                          const std::string& fallback) const;
    bool getBool(const std::string& key, bool fallback) const;
};

/// Parses `text`; returns nullptr on any syntax error (message in *error
/// when provided). Strict JSON, no comments, no trailing commas.
ValuePtr parse(const std::string& text, std::string* error = nullptr);

/// Serializes with 2-space indentation and '\n' line ends.
std::string serialize(const Value& value);

} // namespace fbsampler::configjson
