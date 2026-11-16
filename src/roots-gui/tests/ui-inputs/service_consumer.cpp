// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include <curl/curl.h>
#include <sqlite3.h>
#include <cstdio>
#include <cstring>

int main()
{
    int executed{0}, failed{0};
    const auto check = [&](bool condition, const char* description) {
        ++executed;
        if (!condition) { ++failed; std::fprintf(stderr, "FAIL %s\n", description); }
    };
    check(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK, "curl init");
    const auto* version = curl_version_info(CURLVERSION_NOW);
    check(version && version->version_num == 0x081700, "pinned curl 8.23.0");
    check(version && (version->features & CURL_VERSION_SSL) && version->ssl_version, "TLS compiled");
    bool http{false}, https{false}, unrelated{false};
    for (const char* const* p = version->protocols; *p; ++p) {
        http |= std::strcmp(*p, "http") == 0;
        https |= std::strcmp(*p, "https") == 0;
        unrelated |= std::strcmp(*p, "http") != 0 && std::strcmp(*p, "https") != 0;
    }
    check(http && https && !unrelated, "HTTP/HTTPS-only profile");
    auto* handle = curl_easy_init();
    check(handle != nullptr, "easy handle");
    if (handle) {
        check(curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L) == CURLE_OK, "peer verification option");
        check(curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L) == CURLE_OK, "hostname verification option");
        check(curl_easy_setopt(handle, CURLOPT_PROXYTYPE, CURLPROXY_SOCKS5_HOSTNAME) == CURLE_OK, "SOCKS proxy DNS option");
        check(curl_easy_setopt(handle, CURLOPT_HTTPAUTH, CURLAUTH_BASIC) == CURLE_OK, "RPC Basic auth option");
        check(curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 1000L) == CURLE_OK, "bounded connection timeout option");
        curl_easy_cleanup(handle);
    }
    // No perform: this pin/API test never sends credentials or requests.
    curl_global_cleanup();
    check(sqlite3_libversion_number() == 3054001, "pinned SQLite 3.54.1");
    check(sqlite3_compileoption_used("THREADSAFE=1") && sqlite3_compileoption_used("OMIT_LOAD_EXTENSION"), "SQLite profile");
    sqlite3* db{};
    check(sqlite3_open(":memory:", &db) == SQLITE_OK, "local in-memory metadata database");
    if (db) {
        check(sqlite3_exec(db, "CREATE TABLE metadata(amount INTEGER NOT NULL)", nullptr, nullptr, nullptr) == SQLITE_OK, "schema creation");
        sqlite3_stmt* statement{};
        const bool prepared = sqlite3_prepare_v2(db, "INSERT INTO metadata VALUES(?)", -1, &statement, nullptr) == SQLITE_OK;
        check(prepared, "prepared insertion");
        constexpr sqlite3_int64 amount = 9007199254740993LL;
        if (prepared) {
            check(sqlite3_bind_int64(statement, 1, amount) == SQLITE_OK && sqlite3_step(statement) == SQLITE_DONE, "exact int64 binding beyond double range");
            sqlite3_finalize(statement);
        }
        const bool selected = sqlite3_prepare_v2(db, "SELECT amount FROM metadata", -1, &statement, nullptr) == SQLITE_OK;
        check(selected, "prepared selection");
        if (selected) {
            check(sqlite3_step(statement) == SQLITE_ROW && sqlite3_column_type(statement, 0) == SQLITE_INTEGER && sqlite3_column_int64(statement, 0) == amount, "exact int64 roundtrip");
            sqlite3_finalize(statement);
        }
        check(sqlite3_close(db) == SQLITE_OK, "database ownership released");
    }
    std::printf("selected=%d executed=%d passed=%d failed=%d skipped=0; API/options only, no HTTP/TLS handshake or persistence claim\n", executed, executed, executed - failed, failed);
    return failed ? 1 : 0;
}
