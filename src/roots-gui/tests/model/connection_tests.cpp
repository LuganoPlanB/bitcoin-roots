// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "services/capabilities.h"
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <stdexcept>
using namespace roots::gui;
int main()
{
    int checks{};
    const auto check = [&](bool ok) { if (!ok) throw std::runtime_error("connection contract " + std::to_string(checks + 1)); ++checks; };
    ServerCandidate value; value.connection.credential.reference = "/explicit/test-cookie";
    value.expected_genesis = std::string(64, '0'); value.wallet = "one";
    std::atomic<int> calls{}; std::atomic<bool> held{false}, release{true};
    std::mutex mutex; std::condition_variable_any cv;
    int offline_changes{}; bool offline_callback{};
    ConnectionService service([&](const ServerCandidate& candidate, std::stop_token stop) {
        ++calls;
        if (!release) { held = true; std::unique_lock lock(mutex); cv.wait_for(lock, stop, std::chrono::seconds(2), [&] { return release.load(); }); }
        BackendCapabilities result; result.candidate = candidate; result.authenticated = result.identity_verified = true;
        result.genesis = candidate.expected_genesis; result.wallet = WalletAvailability::Available;
        if (candidate.connection.host == "failing.invalid") result.error = rpc::Error::Authentication;
        return result;
    }, [&](bool offline) { ++offline_changes; offline_callback=offline; });
    const auto settle = [&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (service.Busy() && std::chrono::steady_clock::now() < deadline) { service.Poll(); std::this_thread::yield(); }
        check(!service.Busy());
    };
    check(service.SetDraft(value)); check(!service.Apply()); check(service.Test()); check(!service.Test()); settle();
    check(service.CanApply()); check(!service.Active()); check(service.Apply()); check(service.Session() == 1 && service.Active()->candidate == value); check(!service.Apply());
    auto replacement = value; replacement.connection.host = "failing.invalid";
    check(service.SetDraft(replacement)); check(!service.CanApply()); check(service.Test()); settle();
    check(service.Tested()->authenticated && service.Tested()->error == rpc::Error::Authentication);
    check(!service.Apply() && service.Active()->candidate == value && service.Session() == 1);
    check(service.SetDraft(value)); release = false; held = false; check(service.Test());
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!held && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    check(held); auto other = value; other.wallet = "two"; check(service.SetDraft(other)); release = true; cv.notify_all(); settle();
    check(!service.Tested() && !service.CanApply() && service.Active()->candidate == value);
    check(service.Test()); settle(); check(service.Apply()); check(service.Active()->candidate.wallet == "two" && service.Session() == 2);
    check(service.SetDraft(value)); check(service.Test()); service.Cancel(); settle(); check(!service.CanApply() && service.Active()->candidate.wallet == "two");
    auto wrong = value; wrong.expected_genesis = "not-a-hash"; check(service.SetDraft(wrong)); check(!service.Test());
    check(service.SetDraft(value)); check(service.Test()); settle(); check(service.CanApply());
    check(service.SetOffline(true) && offline_callback && offline_changes==1);
    check(!service.Test() && !service.Apply() && !service.CanApply() && service.Active()->candidate.wallet=="two");
    check(service.SetOffline(false) && !offline_callback && offline_changes==2 && !service.CanApply());
    check(service.Test()); settle(); check(service.Apply());
    bool foreign{}; std::thread other_owner([&] { foreign = service.Test() || service.Apply() || service.SetDraft(value) || service.SetOffline(true); }); other_owner.join(); check(!foreign && !service.Offline());
    check(calls == 7); std::cout << "Connection service checks: " << checks << " passed; owned worker settled\n";
}
