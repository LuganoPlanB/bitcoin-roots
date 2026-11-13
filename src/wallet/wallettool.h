// Copyright (c) 2016-present The Bitcoin Core developers
// Copyright (C) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_WALLET_WALLETTOOL_H
#define BITCOIN_WALLET_WALLETTOOL_H

#include <string>

class ArgsManager;

namespace wallet {
namespace WalletTool {

/** Protect inherited report channels before dumpwallet startup can report.
 * Observes only raw nominated path identities, including rejected duplicates.
 * False means reporting could not be made safe; exit without a diagnostic.
 */
bool ProtectLegacyWalletToolReportChannels(int argc, char* argv[]) noexcept;

bool ExecuteWalletToolFunc(const ArgsManager& args, const std::string& command);

} // namespace WalletTool
} // namespace wallet

#endif // BITCOIN_WALLET_WALLETTOOL_H
