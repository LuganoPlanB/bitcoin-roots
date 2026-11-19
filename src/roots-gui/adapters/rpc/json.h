// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace roots::gui::rpc {
// Numbers remain their original JSON decimal lexemes. Consumers must use the
// domain's exact Money/FeeRate parsers, never a binary floating conversion.
struct Json {
    enum class Kind { Null, Boolean, Number, String, Array, Object };
    Kind kind{Kind::Null};
    std::string text;
    std::vector<Json> items;
    std::vector<std::pair<std::string, Json>> members;
    const Json* Find(std::string_view key) const;
    static std::optional<Json> Parse(std::string_view, size_t max_depth = 64, size_t max_nodes = 100000);
    std::string Encode() const;
};
std::string Quote(std::string_view);
void AppendQuoted(std::string&,std::string_view);
bool ValidUtf8(std::string_view);
}
