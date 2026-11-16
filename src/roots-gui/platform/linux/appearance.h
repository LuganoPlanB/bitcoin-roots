// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <optional>
namespace roots::gui::platform {
// Optional desktop integration. No desktop settings are changed. Unknown stays
// distinguishable from Light; the host uses Light as the documented fallback.
class SystemAppearance {
public:
    SystemAppearance();
    ~SystemAppearance();
    SystemAppearance(const SystemAppearance&) = delete;
    SystemAppearance& operator=(const SystemAppearance&) = delete;
    std::optional<bool> Dark();
private:
    struct Impl;
    Impl* m_impl{};
};
}
