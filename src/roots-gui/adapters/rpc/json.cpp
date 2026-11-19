// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/rpc/json.h"
#include <algorithm>
#include <cstdint>
#include <set>

namespace roots::gui::rpc {
bool ValidUtf8(std::string_view s)
{
    for (size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i++]);
        if (c < 128) continue;
        uint32_t value; unsigned count;
        if (c >= 0xc2 && c <= 0xdf) { value = c & 31; count = 1; }
        else if (c >= 0xe0 && c <= 0xef) { value = c & 15; count = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { value = c & 7; count = 3; }
        else return false;
        if (s.size() - i < count) return false;
        for (unsigned j = 0; j < count; ++j) {
            const auto next = static_cast<unsigned char>(s[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = value * 64 + (next & 63);
        }
        if ((count == 2 && value < 0x800) || (count == 3 && value < 0x10000) ||
            value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
namespace {
void Utf8(std::string& out, uint32_t c)
{
    if (c < 0x80) out += char(c);
    else if (c < 0x800) { out += char(0xc0 | (c >> 6)); out += char(0x80 | (c & 63)); }
    else if (c < 0x10000) { out += char(0xe0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63)); }
    else { out += char(0xf0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 63)); out += char(0x80 | ((c >> 6) & 63)); out += char(0x80 | (c & 63)); }
}
struct Parser {
    std::string_view input;
    size_t offset{}, nodes{}, depth_limit, node_limit;
    void Space() { while (offset < input.size() && (input[offset] == ' ' || input[offset] == '\n' || input[offset] == '\r' || input[offset] == '\t')) ++offset; }
    bool Take(char c) { if (offset == input.size() || input[offset] != c) return false; ++offset; return true; }
    std::optional<uint32_t> Hex()
    {
        if (input.size() - offset < 4) return {};
        uint32_t result{};
        for (int i = 0; i < 4; ++i) {
            const char c = input[offset++];
            if (c >= '0' && c <= '9') result = result * 16 + c - '0';
            else if (c >= 'a' && c <= 'f') result = result * 16 + c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') result = result * 16 + c - 'A' + 10;
            else return {};
        }
        return result;
    }
    std::optional<std::string> String()
    {
        if (!Take('"')) return {};
        std::string result;
        while (offset < input.size()) {
            unsigned char c = input[offset++];
            if (c == '"') return result;
            if (c < 32) return {};
            if (c != '\\') { result += char(c); continue; }
            if (offset == input.size()) return {};
            switch (input[offset++]) {
            case '"': result += '"'; break;
            case '\\': result += '\\'; break;
            case '/': result += '/'; break;
            case 'b': result += '\b'; break;
            case 'f': result += '\f'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            case 'u': {
                auto code = Hex(); if (!code) return {};
                if (*code >= 0xd800 && *code <= 0xdbff) {
                    if (!Take('\\') || !Take('u')) return {};
                    auto low = Hex(); if (!low || *low < 0xdc00 || *low > 0xdfff) return {};
                    *code = 0x10000 + (*code - 0xd800) * 1024 + (*low - 0xdc00);
                } else if (*code >= 0xdc00 && *code <= 0xdfff) return {};
                Utf8(result, *code); break;
            }
            default: return {};
            }
        }
        return {};
    }
    bool Digit() const { return offset < input.size() && input[offset] >= '0' && input[offset] <= '9'; }
    std::optional<Json> Value(size_t depth)
    {
        if (depth > depth_limit || ++nodes > node_limit) return {};
        Space(); if (offset == input.size()) return {};
        Json value;
        if (input[offset] == '"') {
            auto s = String(); if (!s) return {};
            value.kind = Json::Kind::String; value.text = std::move(*s); return value;
        }
        if (Take('{')) {
            value.kind = Json::Kind::Object; Space(); if (Take('}')) return value;
            std::set<std::string> keys;
            do {
                Space(); auto key = String(); if (!key || !keys.insert(*key).second) return {};
                Space(); if (!Take(':')) return {};
                auto child = Value(depth + 1); if (!child) return {};
                value.members.emplace_back(std::move(*key), std::move(*child)); Space();
                if (Take('}')) return value;
            } while (Take(','));
            return {};
        }
        if (Take('[')) {
            value.kind = Json::Kind::Array; Space(); if (Take(']')) return value;
            do {
                auto child = Value(depth + 1); if (!child) return {};
                value.items.push_back(std::move(*child)); Space(); if (Take(']')) return value;
            } while (Take(','));
            return {};
        }
        for (const auto literal : {"null", "true", "false"}) {
            const std::string_view token(literal);
            if (input.substr(offset, token.size()) == token) {
                offset += token.size(); value.kind = token == "null" ? Json::Kind::Null : Json::Kind::Boolean;
                value.text = token; return value;
            }
        }
        const auto begin = offset;
        Take('-'); if (!Digit()) return {};
        if (!Take('0')) { while (Digit()) ++offset; }
        if (Take('.')) { if (!Digit()) return {}; while (Digit()) ++offset; }
        if (Take('e') || Take('E')) { if (!Take('+')) Take('-'); if (!Digit()) return {}; while (Digit()) ++offset; }
        value.kind = Json::Kind::Number; value.text = input.substr(begin, offset - begin); return value;
    }
};
}
std::optional<Json> Json::Parse(std::string_view text, size_t max_depth, size_t max_nodes)
{
    // Hard outer limits apply even when a caller supplies larger settings.
    if (text.size() > 32 * 1024 * 1024 || !ValidUtf8(text)) return {};
    Parser parser{text, 0, 0, std::min(max_depth, size_t(128)), std::min(max_nodes, size_t(1000000))};
    auto result = parser.Value(0); parser.Space();
    return parser.offset == text.size() ? result : std::nullopt;
}
const Json* Json::Find(std::string_view key) const
{
    if (kind != Kind::Object) return nullptr;
    for (const auto& [name, value] : members) if (name == key) return &value;
    return nullptr;
}
void AppendQuoted(std::string& out,std::string_view text)
{
    constexpr char hex[] = "0123456789abcdef";
    out += '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += char(c);
    }
    out += '"';
}
std::string Quote(std::string_view text)
{
    std::string out;
    AppendQuoted(out,text);
    return out;
}
std::string Json::Encode() const
{
    if (kind == Kind::String) return Quote(text);
    if (kind == Kind::Null) return "null";
    if (kind == Kind::Number || kind == Kind::Boolean) return text;
    std::string out = kind == Kind::Object ? "{" : "[";
    bool first = true;
    if (kind == Kind::Object) for (const auto& [key, value] : members) {
        if (!first) out += ','; first = false; out += Quote(key) + ':' + value.Encode();
    }
    else for (const auto& value : items) { if (!first) out += ','; first = false; out += value.Encode(); }
    return out + (kind == Kind::Object ? '}' : ']');
}
}
