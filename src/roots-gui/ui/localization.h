// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include "domain/model.h"

namespace roots::gui::ui::locale {

enum class Language { English, German, Pseudo };

Language Select(std::string_view setting);
Language Messages();
const char* LanguageTag();
std::string_view Translate(std::string_view message, Language language);
const char* Text(const char* message);
std::string Decimal(Money amount, Language language, bool trim = false);
std::string Btc(Money amount, bool trim = false);
std::string BothUnits(Money amount);
std::string Date(int64_t unix_seconds, bool full, Language language);
std::string LocalDate(int64_t unix_seconds, bool full = false);

} // namespace roots::gui::ui::locale
