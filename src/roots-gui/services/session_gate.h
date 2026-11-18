// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "domain/model.h"
#include <limits>
#include <stop_token>
#include <thread>
namespace roots::gui {
enum class ProtectedOperation { Signing,AddressLoad,LabelSave,Refresh };
struct ProtectedTicket {ProtectedOperation operation;RequestContext context;std::stop_token cancellation;};
// Owner-thread admission for every future sensitive async use case. Cancellation
// stops local waiting/adoption; it cannot undo accepted daemon work.
class SessionGate {
    const std::thread::id owner{std::this_thread::get_id()};
    LockEpoch epoch;bool locked{},exhausted{};std::stop_source source;
public:
    LockEpoch Epoch() const{return epoch;}
    bool Locked() const{return locked;}
    std::optional<ProtectedTicket> Begin(ProtectedOperation kind,RequestContext context) const {
        if(std::this_thread::get_id()!=owner || locked || exhausted)return {};context.lock_epoch=epoch;return ProtectedTicket{kind,std::move(context),source.get_token()};
    }
    bool Accept(const ProtectedTicket& ticket,const RequestContext& current,ProtectedOperation expected) const {
        return std::this_thread::get_id()==owner && !locked && !exhausted && ticket.operation==expected && !ticket.cancellation.stop_requested() && ticket.context.lock_epoch==epoch && current.lock_epoch==epoch && ticket.context==current;
    }
    template<class F> bool Adopt(const ProtectedTicket& ticket,const RequestContext& current,ProtectedOperation expected,F&& apply){return Accept(ticket,current,expected) && apply();}
    bool Lock(){if(std::this_thread::get_id()!=owner)return false;if(locked)return !exhausted;locked=true;source.request_stop();if(epoch.value==std::numeric_limits<uint64_t>::max()){exhausted=true;return false;}++epoch.value;return true;}
    bool Unlock(){if(std::this_thread::get_id()!=owner || !locked || exhausted)return false;source=std::stop_source();locked=false;return true;}
};
}
