// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include "services/capabilities.h"

#include <atomic>
#include <memory>

namespace roots::gui {
enum class DeviceState {
    Idle, Scanning, Unconfigured, Unsupported, NoDevice, NoMatch, Multiple,
    Select, Ready, Confirm, Locked, Passphrase, Disconnected, Failure,
};
const char* DeviceStateText(DeviceState);
DeviceState SignerFailure(const rpc::Reply&);
bool CanDiscoverDevice(const BackendCapabilities&);

struct DeviceScope {
    ServerCandidate profile;
    std::string chain;
    uint64_t session{}, revision{};
    bool operator==(const DeviceScope&) const = default;
};
struct DevicePolicyBranch {
    std::string descriptor;
    bool internal{};
    uint32_t first{}, last{};
    bool operator==(const DevicePolicyBranch&) const = default;
};
struct DeviceEvidence {
    DeviceState state{DeviceState::Failure};
    std::string fingerprint, label;
    std::vector<DevicePolicyBranch> branches;
};

// A selection is minted only by the owner-thread service after explicit
// discovery and selection. Recovery, document and RPC replies cannot mint one.
class DeviceSelection {
    friend class DeviceSigners;
    DeviceSelection(DeviceScope scoped, DeviceEvidence proof) : scope(std::move(scoped)), evidence(std::move(proof)) {}
    const DeviceScope scope;
    const DeviceEvidence evidence;
    const std::chrono::steady_clock::time_point expires = std::chrono::steady_clock::now() + std::chrono::minutes(2);
    mutable std::atomic<DeviceState> state{DeviceState::Ready};
    mutable std::atomic_flag in_use = ATOMIC_FLAG_INIT;
public:
    class Operation {
        const DeviceSelection& selection;
        bool acquired;
    public:
        explicit Operation(const DeviceSelection&);
        ~Operation();
        Operation(const Operation&) = delete;
        Operation& operator=(const Operation&) = delete;
        explicit operator bool() const {return acquired;}
    };
    bool Live() const {return state.load() == DeviceState::Ready && std::chrono::steady_clock::now() < expires;}
    bool Busy() const {return in_use.test();}
    DeviceState RetiredState() const {
        const auto current=state.load();
        return current==DeviceState::Ready ? DeviceState::Idle : current;
    }
    void Retire(DeviceState = DeviceState::Idle) const;
    bool Current(const ConnectionService&) const;
    bool Matches(const ServerCandidate&, uint64_t session, uint64_t revision) const;
    DeviceState Recheck(rpc::Transport&, std::stop_token) const;
    bool Owns(std::string_view descriptor, std::string_view exact_script, bool receive_only,
        std::optional<uint32_t> index = {}) const;
};
using SelectedDevice = std::shared_ptr<const DeviceSelection>;

class DeviceSigners {
public:
    explicit DeviceSigners(rpc::Transport& transport) : transport(transport) {}
    ~DeviceSigners();
    bool Rescan(const ConnectionService&);
    bool Select(const ConnectionService&);
    void Poll(const ConnectionService*);
    void Cancel();
    bool Busy() const{return worker.joinable() || (selection && selection->Busy()) || (retiring && retiring->Busy());}
    DeviceState Status() const;
    std::string_view Fingerprint() const;
    std::string_view Label() const;
    SelectedDevice Current(const ConnectionService*) const;
private:
    const std::thread::id owner{std::this_thread::get_id()};
    rpc::Transport& transport;
    std::optional<DeviceScope> scope;
    std::chrono::steady_clock::time_point select_by{};
    DeviceEvidence report{DeviceState::Idle,{}, {},{}};
    SelectedDevice selection;
    SelectedDevice retiring;
    std::optional<DeviceEvidence> completion;
    std::mutex mutex;
    std::jthread worker;
};
}
