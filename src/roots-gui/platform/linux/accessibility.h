// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <memory>
#include <string>
namespace roots::gui::ui::components {class SemanticExchange;}
namespace roots::gui::platform {
struct AccessibilityStatus {
    bool connected{}, registered{}, closed{}, settled{true};
    unsigned objects{}, pending{}, callbacks{};
    std::string bus_name, error;
};
// One native owner pumps a private GIO context. D-Bus callbacks only read owned
// packets and queue typed actions; they never access ImGui or document state.
class LinuxAccessibility {
public:
    explicit LinuxAccessibility(ui::components::SemanticExchange&);
    ~LinuxAccessibility();
    LinuxAccessibility(const LinuxAccessibility&)=delete;
    LinuxAccessibility& operator=(const LinuxAccessibility&)=delete;
    void Pump(); // Bounded ready work, never a blocking main loop.
    void Update(int screen_x,int screen_y,int width,int height,bool active);
    bool Close();
    AccessibilityStatus Status() const;
private:
    struct Impl;
    std::shared_ptr<Impl> m_impl;
};
}
