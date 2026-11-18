// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/rpc/json.h"
#include "services/connection.h"
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
int main()
{
    int checks{};
    const auto check = [&](bool value) { ++checks; if (!value) throw std::runtime_error("JSON/profile contract failure " + std::to_string(checks)); };
    for (const std::string text : {"null", "true", "false", "0", "-0", "1.23000000", "1e-8", "9007199254740993", "[1,{},[null]]", "{\"text\":\"漢字 / 🙂\"}"}) {
        auto value = rpc::Json::Parse(text); check(value && value->Encode() == text);
    }
    const auto escaped = rpc::Json::Parse("{\"a\":\"\\ud83d\\ude42\\u0000\"}");
    check(escaped && escaped->Find("a")->text == std::string("🙂\0", 5));
    for (const std::string text : {"", "[1,]", "{\"a\":1,}", "{\"a\":1,\"\\u0061\":2}", "01", "-01", "1.", ".1", "1e", "+1", "NaN", "Infinity", "null false", "\"\\ud800\"", "\"\\udc00\"", "\"\\ud800\\u0001\"", "\"\\x12\"", "\"\n\"", "[", "{", "\"", "{\"a\"}"}) check(!rpc::Json::Parse(text));
    for (const std::string text : {std::string("\"\xc0\x80\""), std::string("\"\xed\xa0\x80\""), std::string("\"\xf4\x90\x80\x80\""), std::string("\"\xe2\x82\"")}) check(!rpc::Json::Parse(text));
    check(!rpc::Json::Parse("[[[[0]]]]", 3));
    check(!rpc::Json::Parse("[0,1,2,3]", 64, 4));
    check(rpc::Quote("a\n\"\\") == "\"a\\u000a\\\"\\\\\"");
    ConnectionProfile p; p.credential.reference = "/explicit/fixture-cookie"; check(ValidConnectionProfile(p));
    for (const auto* host : {"", "host/path", "user@host", "host?query", "host#fragment", "host\r\nheader", "http://host"}) { auto q = p; q.host = host; check(!ValidConnectionProfile(q)); }
    p.proxy = ProxyKind::Socks5RemoteDns; p.proxy_host = "127.0.0.1"; check(ValidConnectionProfile(p));
    std::cout << "JSON/profile checks: " << checks << " passed\n";
}
