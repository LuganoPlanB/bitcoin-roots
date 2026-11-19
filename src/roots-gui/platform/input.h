// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "ui/components/native_input.h"
#include <memory>
#include <optional>
struct GLFWwindow;
namespace roots::gui::platform {
struct InputStatus {
    bool clipboard{},file_dialog{},closed{},settled{true};
    unsigned callbacks{};
    std::optional<bool> reduced_motion;
    std::optional<float> system_scale;
    std::string notice,dialog_process;
    uint64_t clipboard_reads{},clipboard_writes{},pastes{},cancelled{};
};
class NativeInput final:public ui::components::NativeInputPort {
public:
    explicit NativeInput(GLFWwindow*);
    ~NativeInput();
    void Pump();
    void BeginFrame();
    void EndFrame();
    void BeginView(std::string_view) override;
    void Privacy(bool hidden) override;
    void PrivacyLock() override;
    bool Copy(std::string_view,bool sensitive=false);
    const char* ClipboardForPaste();
    void EnterEditor(uint32_t,bool) override;
    void EnterProtectedEditor(uint32_t) override;
    void LeaveEditor() override;
    std::optional<std::string> Paste(uint32_t,std::string_view,int,int,int,size_t) override;
    bool RequestInspection(uint32_t) override;
    bool InspectionPending(uint32_t) const override;
    bool FileChooserAvailable() const override;
    bool NativeDialogActive() const override;
    uint64_t ChooseFile(std::string_view,bool save=false) override;
    uint64_t ChooseRpcCookie() override;
    ui::components::FileChoice PollFile(uint64_t) override;
    InputStatus Status() const;
    bool Close();
private:
    struct Impl;std::shared_ptr<Impl> impl;
};
}
