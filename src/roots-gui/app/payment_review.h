// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include "app/signing_review.h"
#include "services/payment_protocols.h"

namespace roots::gui::app {
// Owning-thread handoff. The result contains only unsigned material. Existing
// review and approval are consumed even when the provider repeats an offer.
// A separate supported workflow must establish the fresh signing basis.
inline bool StageNegotiatedDraft(const ExtensionRegistry& registry, const NegotiationPermission& permission,
    const NegotiationOwner& current, int64_t now, NegotiatedDraft proposal,
    std::optional<NegotiatedDraft>& slot, SigningReviews& reviews, std::stop_token stop = {})
{
    if (stop.stop_requested() || !proposal.Current(registry,permission,current,now) || (slot && slot->Owner()!=current)) return false;
    reviews.Revoke(current.document,"The negotiated unsigned draft changed. Review the current draft again.");
    slot=std::move(proposal);
    return true;
}

// Call on permit/profile/session/owner retirement. Clear only this attempt's
// slot and review, including an approved ordinary draft if negotiation had
// already been deliberately authorized for that exact document.
inline void RetireNegotiatedDraft(const NegotiationOwner& retired, const NegotiationOwner& active,
    std::optional<NegotiatedDraft>& slot, SigningReviews& reviews)
{
    if (!retired.document || retired!=active || (slot && slot->Owner()!=retired)) return;
    slot.reset();
    reviews.Revoke(retired.document,"The negotiation context changed. Review the current draft again.");
}
} // namespace roots::gui::app
