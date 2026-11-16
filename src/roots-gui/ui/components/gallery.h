// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "ui/components/controls.h"
#include "ui/bootstrap.h"
namespace roots::gui::ui::components {
struct Gallery {
    Edit amount{"0.01000000"},fee{"1.25"},label{"Public fixture"};
    Selection selection;Dialog dialog;
    std::vector<Row> rows{{101,"First public observation",Money::Satoshis(1000000)},
                          {202,"Public fixture",Money::Satoshis(2000000)},
                          {303,"Public observation",Money::Satoshis(3000000),false,"Observation incomplete"}};
    std::array<char,128> filter{};
    std::optional<std::string> external_filter;TextPosition filter_cursor;
    bool enabled{true};
    AmountPrivacy privacy;
    bool semantic_hide_toggle{};
    bool reduced_motion{},show_summary{true},review_fixture{};
    PresentationFrame presentation;
    ViewPolish polish;
    float privacy_feedback{};
    bool reveal_rows{}; // One-shot presentation intent, e.g. a jump-to-observations command.
    FreshnessStatus status{Freshness::Offline,false,false,"Public fixture commands only; no wallet service.","2026-11-15 12:00 UTC"};
};
struct GalleryFacts {
    ItemFacts copy,amount,fee,label,primary,secondary,dialog,amount_copy;
    std::array<float,4> privacy_toggle{},motion_toggle{};
    bool review_visible{};
    std::vector<RowFacts> rows;
};
GalleryFacts DrawGallery(Gallery&,Commands&,const imkit::Theme&,const BootstrapAppearance&);
}
