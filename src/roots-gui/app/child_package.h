// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "adapters/protocol/offline_draft.h"
#include "adapters/rpc/json.h"

#include <functional>

namespace roots::gui::app {
inline constexpr size_t MAX_CHILD_PACKAGE_TRANSACTIONS=16; // Parent included.
inline constexpr size_t MAX_CHILD_PACKAGE_DEPTH=8;
struct PackageTransaction {
    Hash256 txid,wtxid;
    Money base_fee,modified_fee;
    size_t weight{},vsize{},policy_vsize{};
    std::vector<Hash256> parents;
    bool operator==(const PackageTransaction&) const=default;
};
struct PackageSnapshot {
    std::vector<PackageTransaction> ancestors; // Parent and deduplicated unconfirmed ancestors.
    Money base_fee{Money::Satoshis(0)};
    size_t vsize{};
    std::vector<Hash256> cluster;
    size_t cluster_weight{},cluster_count_limit{},cluster_vsize_limit{};
    Money relay_floor{Money::Satoshis(0)},mempool_floor{Money::Satoshis(0)}; // sat/kvB
    bool operator==(const PackageSnapshot&) const=default;
};
struct ChildPackageIntent {
    std::shared_ptr<const protocol::TransactionDocument> parent;
    uint32_t output{};
    protocol::OfflineChange owned_destination;
    PackageSnapshot package;
    uint32_t target_per_kvb{};
    size_t estimated_vsize{};
    bool operator==(const ChildPackageIntent&) const=default;
};

// The caller supplies a bounded read-only node RPC port and its verified tip.
// A partial graph or inconsistent monetary/policy evidence is refused.
using PackageRead=std::function<rpc::Json(std::string,std::string,const char*)>;
PackageSnapshot ReadChildPackage(const PackageRead&,const protocol::TransactionDocument&,uint32_t,
    protocol::DisplayNetwork,std::string_view tip,std::stop_token);
struct ChildFee {
    Money fee;
    size_t estimated_vsize{};
};
ChildFee EstimateChildFee(const PackageSnapshot&,uint32_t target_per_kvb,std::string_view script,Money available);
}
