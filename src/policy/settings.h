// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POLICY_SETTINGS_H
#define BITCOIN_POLICY_SETTINGS_H

/** Default for -maxscriptsize. */
static constexpr unsigned int DEFAULT_SCRIPT_SIZE_POLICY_LIMIT{1650};
/** Default for -bytespersigop. */
static constexpr unsigned int DEFAULT_BYTES_PER_SIGOP{20};
/** Default for -bytespersigopstrict. */
static constexpr unsigned int DEFAULT_BYTES_PER_SIGOP_STRICT{20};
/** Default for -datacarriercost (multiplied by WITNESS_SCALE_FACTOR). */
static constexpr unsigned int DEFAULT_WEIGHT_PER_DATA_BYTE{4};

extern unsigned int g_script_size_policy_limit;
extern unsigned int nBytesPerSigOp;
extern unsigned int nBytesPerSigOpStrict;
extern unsigned int g_weight_per_data_byte;

#endif // BITCOIN_POLICY_SETTINGS_H
