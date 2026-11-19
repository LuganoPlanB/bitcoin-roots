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
    check(saved.explorer==ExplorerMode::Disabled);
    draft.explorer=ExplorerMode::Custom;
    draft.explorer_template="https://explorer.example/tx/{txid}";
    check(!ValidPreferences(draft));
    draft.explorer_genesis=std::string(64,'a');
    check(ValidPreferences(draft));
    for(const auto* rejected:{"http://explorer.example/tx/{txid}","https://name@explorer.example/tx/{txid}",
        "https://explorer.example/tx?secret=example/{txid}","https://explorer.example/../{txid}",
        "https://explorer.example/tx/{txid}#ref", "https://explorer.example/tx/{txid}/{txid}",
        "https://hidden.OnIoN/tx/{txid}", "https://127.0.0.1/tx/{txid}", "https://explorer.example/tx/%2f/{txid}"})
        check(!ValidExplorerTemplate(rejected));
    draft.explorer=ExplorerMode::Disabled;
    check(!ValidPreferences(draft));
    std::printf("executed=%d passed=%d failed=%d skipped=0\n",executed,executed-failed,failed);
    return failed ? 1 : 0;
}
