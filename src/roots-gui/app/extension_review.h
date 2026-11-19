// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include "app/signing_review.h"
#include "services/extensions.h"

namespace roots::gui::app {
// Called on the owning thread with the exact destination document and the
// currently selected construction scope. The slot stores unsigned material
// only. No review or approval is created here or restored from the provider.
inline bool StageUnsignedConstruction(uint64_t document, const FundingScope& current, int64_t now_ms,
    PreparedConstruction proposal, std::optional<PreparedConstruction>& slot, SigningReviews& reviews)
{
    if (!document || !proposal.Current(current,now_ms)) return false;
    if (!slot || slot->Provenance().scope!=current || slot->Provenance().strategy!=proposal.Provenance().strategy
        || slot->Provenance().observation!=proposal.Provenance().observation
        || slot->Document()->source!=proposal.Document()->source || slot->Proof()!=proposal.Proof())
        reviews.Revoke(document,"The unsigned construction strategy or draft changed. Review the current draft again.");
    slot=std::move(proposal);
    return true;
}
} // namespace roots::gui::app
