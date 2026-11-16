// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "application/preferences.h"
#include <cstdio>
#include <stdexcept>
using namespace roots::gui;
int main()
{
    struct Fake final : PreferenceRequests {
        int saves{}, cancels{}; bool accept{true};
        bool Save(const PublicPreferences&) override { ++saves; return accept; }
        void Cancel() override { ++cancels; }
    } fake;
    int executed{}, failed{};
    auto check = [&](bool ok) { ++executed; failed += !ok; };
    PublicPreferences draft, saved;
    for (int value : {-1,0,61}) { draft.refresh_seconds=value;check(!SavePreferences(draft,fake)); }
    check(fake.saves==0);
    draft.refresh_seconds=60;check(SavePreferences(draft,fake));check(fake.saves==1);
    fake.accept=false;draft.refresh_seconds=1;check(!SavePreferences(draft,fake));check(fake.saves==2);
    CancelPreferences(draft,saved,fake);check(draft==saved);check(fake.cancels==1);check(fake.saves==2);
    std::printf("executed=%d passed=%d failed=%d skipped=0\n",executed,executed-failed,failed);
    return failed ? 1 : 0;
}
