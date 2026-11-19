// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "ui/components/controls.h"
#include "services/capabilities.h"
#include <map>

namespace roots::gui::ui {
enum class ServerSource { DirectRpc, PrivateIndex, PublicIndex };
struct ServerPanel {
    imkit::DialogState dialog;
    bool close_pending{}, initialized{}, tls{}, select_wallet{}, secure_store{};
    int proxy{};
    ServerSource source{ServerSource::DirectRpc};
    components::Edit host{"127.0.0.1"}, port{"8332"}, credential, wallet, ca, proxy_host{"127.0.0.1"}, proxy_port{"9050"};
    components::Edit genesis{"000000000019d6689c085ae165831e934ff763ae46a2a6c172b3f1b60a8ce26f"};
    std::string notice;
    uint64_t scope_revision{}, scope_session{};
    uint64_t cookie_dialog{}, cookie_revision{}, cookie_session{};
    ServerCandidate Candidate() const;
    void Load(const ServerCandidate&);
};
struct ServerFacts { bool visible{}; std::map<std::string, components::ItemFacts> items; };
void OpenServer(ServerPanel&, ConnectionService&);
ServerFacts DrawServer(ServerPanel&, ConnectionService&, components::PresentationFrame&, const imkit::Theme&, float scale,bool hidden=false);
}
