#include "json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace fbsampler::configjson {

ValuePtr Value::makeNull() { return std::make_shared<Value>(); }

ValuePtr Value::makeBool(bool b)
{
    auto v = std::make_shared<Value>();
    v->type = Type::boolean;
    v->boolValue = b;
    return v;
}

ValuePtr Value::makeNumber(double n)
{
    auto v = std::make_shared<Value>();
    v->type = Type::number;
    v->numberValue = n;
    return v;
}

ValuePtr Value::makeInt(std::int64_t n)
{
    return makeNumber(static_cast<double>(n));
}

ValuePtr Value::makeString(std::string s)
{
    auto v = std::make_shared<Value>();
    v->type = Type::string;
    v->stringValue = std::move(s);
    return v;
}

ValuePtr Value::makeArray()
{
    auto v = std::make_shared<Value>();
    v->type = Type::array;
    return v;
}

ValuePtr Value::makeObject()
{
    auto v = std::make_shared<Value>();
    v->type = Type::object;
    return v;
}

const Value* Value::find(const std::string& key) const
{
    if (type != Type::object)
        return nullptr;
    const auto it = members.find(key);
    return it == members.end() ? nullptr : it->second.get();
}

std::int64_t Value::getInt(const std::string& key, std::int64_t fallback) const
{
    const Value* v = find(key);
    return (v != nullptr && v->type == Type::number)
               ? static_cast<std::int64_t>(std::llround(v->numberValue))
               : fallback;
}

double Value::getNumber(const std::string& key, double fallback) const
{
    const Value* v = find(key);
    return (v != nullptr && v->type == Type::number) ? v->numberValue : fallback;
}

std::string Value::getString(const std::string& key,
                             const std::string& fallback) const
{
    const Value* v = find(key);
    return (v != nullptr && v->type == Type::string) ? v->stringValue : fallback;
}

bool Value::getBool(const std::string& key, bool fallback) const
{
    const Value* v = find(key);
    return (v != nullptr && v->type == Type::boolean) ? v->boolValue : fallback;
}

namespace {

class Parser {
public:
    Parser(const std::string& text, std::string* error)
        : text_(text), error_(error)
    {
    }

    ValuePtr run()
    {
        auto v = parseValue();
        if (v == nullptr)
            return nullptr;
        skipWhitespace();
        if (pos_ != text_.size())
            return fail("trailing characters after JSON value");
        return v;
    }

private:
    ValuePtr fail(const std::string& message)
    {
        if (error_ != nullptr && error_->empty())
            *error_ = message + " (offset " + std::to_string(pos_) + ")";
        return nullptr;
    }

    void skipWhitespace()
    {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++pos_;
            else
                break;
        }
    }

    bool consume(char expected)
    {
        if (pos_ < text_.size() && text_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    ValuePtr parseValue()
    {
        skipWhitespace();
        if (pos_ >= text_.size())
            return fail("unexpected end of input");
        const char c = text_[pos_];
        switch (c) {
        case '{': return parseObject();
        case '[': return parseArray();
        case '"': return parseString();
        case 't':
        case 'f': return parseBool();
        case 'n': return parseNull();
        default: return parseNumber();
        }
    }

    ValuePtr parseObject()
    {
        ++pos_; // '{'
        auto obj = Value::makeObject();
        skipWhitespace();
        if (consume('}'))
            return obj;
        while (true) {
            skipWhitespace();
            if (pos_ >= text_.size() || text_[pos_] != '"')
                return fail("expected object key string");
            auto key = parseString();
            if (key == nullptr)
                return nullptr;
            skipWhitespace();
            if (!consume(':'))
                return fail("expected ':' after object key");
            auto value = parseValue();
            if (value == nullptr)
                return nullptr;
            obj->members[key->stringValue] = value;
            skipWhitespace();
            if (consume(','))
                continue;
            if (consume('}'))
                return obj;
            return fail("expected ',' or '}' in object");
        }
    }

    ValuePtr parseArray()
    {
        ++pos_; // '['
        auto arr = Value::makeArray();
        skipWhitespace();
        if (consume(']'))
            return arr;
        while (true) {
            auto value = parseValue();
            if (value == nullptr)
                return nullptr;
            arr->arrayItems.push_back(value);
            skipWhitespace();
            if (consume(','))
                continue;
            if (consume(']'))
                return arr;
            return fail("expected ',' or ']' in array");
        }
    }

    ValuePtr parseString()
    {
        ++pos_; // '"'
        std::string out;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"')
                return Value::makeString(std::move(out));
            if (c == '\\') {
                if (pos_ >= text_.size())
                    return fail("unterminated escape");
                const char e = text_[pos_++];
                switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (pos_ + 4 > text_.size())
                        return fail("truncated \\u escape");
                    unsigned int cp = 0;
                    for (int i = 0; i < 4; ++i) {
                        const char h = text_[pos_++];
                        cp <<= 4;
                        if (h >= '0' && h <= '9')
                            cp += static_cast<unsigned>(h - '0');
                        else if (h >= 'a' && h <= 'f')
                            cp += static_cast<unsigned>(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F')
                            cp += static_cast<unsigned>(h - 'A' + 10);
                        else
                            return fail("invalid \\u escape");
                    }
                    // Encode as UTF-8 (surrogate pairs are passed through as
                    // separate code units — config files never contain them
                    // in practice; paths are stored as UTF-8 directly).
                    if (cp < 0x80) {
                        out.push_back(static_cast<char>(cp));
                    } else if (cp < 0x800) {
                        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    } else {
                        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
                        out.push_back(
                            static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    }
                    break;
                }
                default: return fail("unknown escape character");
                }
            } else {
                out.push_back(c);
            }
        }
        return fail("unterminated string");
    }

    ValuePtr parseBool()
    {
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            return Value::makeBool(true);
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            return Value::makeBool(false);
        }
        return fail("invalid literal");
    }

    ValuePtr parseNull()
    {
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return Value::makeNull();
        }
        return fail("invalid literal");
    }

    ValuePtr parseNumber()
    {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-')
            ++pos_;
        while (pos_ < text_.size()
               && (std::isdigit(static_cast<unsigned char>(text_[pos_]))
                   || text_[pos_] == '.' || text_[pos_] == 'e'
                   || text_[pos_] == 'E' || text_[pos_] == '+'
                   || text_[pos_] == '-'))
            ++pos_;
        if (pos_ == start)
            return fail("invalid number");
        const std::string token = text_.substr(start, pos_ - start);
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (end == nullptr || *end != '\0')
            return fail("invalid number");
        return Value::makeNumber(value);
    }

    const std::string& text_;
    std::string* error_;
    std::size_t pos_ = 0;
};

void appendEscaped(std::string& out, const std::string& s)
{
    out.push_back('"');
    for (const char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x",
                              static_cast<unsigned>(c));
                out += buf;
            } else {
                out.push_back(c);
            }
        }
    }
    out.push_back('"');
}

void appendNumber(std::string& out, double n)
{
    // Integral values (the common case: generations, sizes, timestamps)
    // serialize without a fractional part.
    if (n == std::floor(n) && std::fabs(n) < 9.0e15) {
        out += std::to_string(static_cast<std::int64_t>(n));
    } else {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.17g", n);
        out += buf;
    }
}

void serializeInto(std::string& out, const Value& v, int indent)
{
    const std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
    const std::string childPad(static_cast<std::size_t>(indent + 1) * 2, ' ');
    switch (v.type) {
    case Value::Type::null: out += "null"; break;
    case Value::Type::boolean: out += v.boolValue ? "true" : "false"; break;
    case Value::Type::number: appendNumber(out, v.numberValue); break;
    case Value::Type::string: appendEscaped(out, v.stringValue); break;
    case Value::Type::array: {
        if (v.arrayItems.empty()) {
            out += "[]";
            break;
        }
        out += "[\n";
        for (std::size_t i = 0; i < v.arrayItems.size(); ++i) {
            out += childPad;
            serializeInto(out, *v.arrayItems[i], indent + 1);
            if (i + 1 < v.arrayItems.size())
                out.push_back(',');
            out.push_back('\n');
        }
        out += pad + "]";
        break;
    }
    case Value::Type::object: {
        if (v.members.empty()) {
            out += "{}";
            break;
        }
        out += "{\n";
        std::size_t i = 0;
        for (const auto& [key, member] : v.members) {
            out += childPad;
            appendEscaped(out, key);
            out += ": ";
            serializeInto(out, *member, indent + 1);
            if (++i < v.members.size())
                out.push_back(',');
            out.push_back('\n');
        }
        out += pad + "}";
        break;
    }
    }
}

} // namespace

ValuePtr parse(const std::string& text, std::string* error)
{
    return Parser(text, error).run();
}

std::string serialize(const Value& value)
{
    std::string out;
    serializeInto(out, value, 0);
    out.push_back('\n');
    return out;
}

} // namespace fbsampler::configjson
