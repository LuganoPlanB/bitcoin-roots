// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/capabilities.h"
#include <iostream>
using namespace roots::gui;
int main(int argc, char** argv)
{
    if (argc != 5) return 2;
    ServerCandidate candidate;
    candidate.connection.port = uint16_t(std::stoi(argv[1]));
    candidate.connection.credential.reference = argv[2]; candidate.expected_genesis = argv[3];
    if (std::string_view(argv[4]) != "node") candidate.wallet = argv[4];
    rpc::CookieCredentials credentials; rpc::Transport transport(credentials);
    const auto result = ProbeCapabilities(transport, candidate, {});
    std::cout << "{\"error\":" << rpc::Quote(rpc::ErrorName(result.error)) << ",\"authenticated\":" << (result.authenticated ? "true" : "false")
        << ",\"apply\":" << (result.CanApply() ? "true" : "false") << ",\"mismatch\":" << (result.network_mismatch ? "true" : "false")
        << ",\"ibd\":" << (result.initial_download ? "true" : "false") << ",\"pruned\":" << (result.pruned ? "true" : "false")
        << ",\"wallet\":" << rpc::Quote(WalletAvailabilityName(result.wallet)) << ",\"version\":" << result.version
        << ",\"public_descriptors\":" << rpc::Quote(SupportName(result.Supports("listdescriptors", "private")))
        << ",\"sign_argument\":" << rpc::Quote(SupportName(result.Supports("walletprocesspsbt", "sign")))
        << ",\"fund_exact_inputs\":" << rpc::Quote(SupportName(result.Supports("walletcreatefundedpsbt", "options.add_inputs")))
        << ",\"invented_argument\":" << rpc::Quote(SupportName(result.Supports("walletcreatefundedpsbt", "options.exclude_inputs"))) << "}\n";
}
