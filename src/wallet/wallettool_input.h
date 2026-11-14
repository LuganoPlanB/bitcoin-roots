// Copyright (c) 2026 The Bitcoin Core developers
// Copyright (C) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_WALLETTOOL_INPUT_H
#define BITCOIN_WALLET_WALLETTOOL_INPUT_H

#include <support/allocators/secure.h>

#include <memory>
#include <stdexcept>
#include <utility>

namespace wallet::WalletTool {

namespace detail {
enum class PassphraseEnd { BYTE_LINE, NATIVE_ENTER, END_OF_FILE };

/** The actual adapters share this byte policy. Native Enter contributes no
 * byte, unlike an LF in a byte stream whose immediately preceding CR is part
 * of CRLF. Never remove a data byte when finalizing a native key event.
 */
class PassphraseLine
{
    SecureString m_bytes;
    static void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

public:
    // Force allocation even for empty/short input: no secret in string SSO.
    PassphraseLine() { m_bytes.reserve(4097); }
    bool Push(char byte)
    {
        if (byte == '\n') return true;
        Require(m_bytes.size() < 4097, "Legacy passphrase exceeds 4096-byte limit");
        m_bytes.push_back(byte);
        Require(m_bytes.size() <= 4096 || byte == '\r', "Legacy passphrase exceeds 4096-byte limit");
        return false;
    }
    SecureString Finish(PassphraseEnd end)
    {
        Require(end != PassphraseEnd::END_OF_FILE || !m_bytes.empty(), "Missing legacy passphrase input");
        if (end == PassphraseEnd::BYTE_LINE && !m_bytes.empty() && m_bytes.back() == '\r') {
            memory_cleanse(m_bytes.data() + m_bytes.size() - 1, 1);
            m_bytes.pop_back();
        }
        Require(m_bytes.size() <= 4096, "Legacy passphrase exceeds 4096-byte limit");
        return std::move(m_bytes);
    }
};
} // namespace detail

/** Command-scoped cooperative interruption, installed before dumpwallet parsing.
 * No wallet/node lifecycle and no asynchronous terminal/secret operations.
 */
class LegacyWalletToolSignals
{
    struct Impl;
    std::unique_ptr<Impl> m_impl;

public:
    LegacyWalletToolSignals();
    ~LegacyWalletToolSignals();
    LegacyWalletToolSignals(const LegacyWalletToolSignals&) = delete;
    LegacyWalletToolSignals& operator=(const LegacyWalletToolSignals&) = delete;
};

bool LegacyWalletToolInterrupted() noexcept;
void CheckLegacyWalletToolInterrupted();

/** One length-aware secure byte string, at most 4096 bytes. Stdin is read
 * directly (no stdio read-ahead), and never waits for extra bytes after LF.
 * Terminal state is saved/restored exactly, including during exceptions.
 */
SecureString ReadLegacyWalletToolPassphrase(bool use_stdin);

} // namespace wallet::WalletTool

#endif // BITCOIN_WALLET_WALLETTOOL_INPUT_H
