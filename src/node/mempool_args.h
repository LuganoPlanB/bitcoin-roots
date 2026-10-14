// Copyright (c) 2022-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_NODE_MEMPOOL_ARGS_H
#define BITCOIN_NODE_MEMPOOL_ARGS_H

#include <common/settings.h>
#include <util/result.h>

#include <cstdint>
#include <string_view>
#include <utility>

class ArgsManager;
class CChainParams;
struct bilingual_str;
namespace kernel {
struct MemPoolOptions;
};

[[nodiscard]] util::Result<std::pair<int32_t, int>> ParseDustDynamicOpt(std::string_view optstr, unsigned int max_fee_estimate_blocks);
/** Parse vbytes per carrier byte, rounding exact hundredths upward to integral weight. */
[[nodiscard]] util::Result<unsigned int> ParseDataCarrierCost(std::string_view value);
/** Validate effective Roots policy values without changing inherited Core option parsing. */
[[nodiscard]] util::Result<void> ValidateRootsPolicyOptions(const ArgsManager& argsman);
void ApplyPermitEphemeralOption(const common::SettingsValue& value, kernel::MemPoolOptions& mempool_opts);

/**
 * Overlay the options set in \p argsman on top of corresponding members in \p mempool_opts.
 * Returns an error if one was encountered.
 *
 * @param[in]  argsman The ArgsManager in which to check set options.
 * @param[in,out] mempool_opts The MemPoolOptions to modify according to \p argsman.
 */
[[nodiscard]] util::Result<void> ApplyArgsManOptions(const ArgsManager& argsman, const CChainParams& chainparams, kernel::MemPoolOptions& mempool_opts);


#endif // BITCOIN_NODE_MEMPOOL_ARGS_H
