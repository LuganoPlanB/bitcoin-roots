// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/foundation.h"
#include "platform/native_host.h"
#include "ui/shell/workspace.h"
#include "platform/shell_composition.h"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <imgui_impl_glfw.h>
#if !defined(__APPLE__)
#include <imgui_impl_opengl3.h>
#endif
#include <cstdio>
#include <cstdlib>
namespace roots::gui::platform {
ui::shell::Facts DrawShellFrame(NativeHost& host,ui::shell::State& workspace,app::Requests& requests,const imkit::Theme& theme,const ui::BootstrapAppearance& appearance)
{
    for(auto& drop:host.TakeDroppedPaths()) {
        const auto& area=workspace.native_drop_target;
        if(workspace.actions.kind==ui::shell::DocumentDialog::None && workspace.documents.Documents().empty() && area.w>0 && drop.x>=area.x && drop.x<area.x+area.w && drop.y>=area.y && drop.y<area.y+area.h) {
            if(drop.paths.size()==1)ui::shell::BeginImport(workspace,drop.paths.front());
            else workspace.notice="Import one public file at a time; no files were read.";
        }
    }
    // The native facade lives through DrawWorkspace's complete-frame Seal.
    // Native callbacks retain only operation IDs/data, never this facade.
    struct NativeRequests final:app::Requests {
        NativeHost& host;ui::shell::State& state;app::Requests& fallback;
        NativeRequests(NativeHost& h,ui::shell::State& s,app::Requests& f):host(h),state(s),fallback(f){}
        bool CopyRequest(const app::Request& request) const {return request.component && (std::holds_alternative<ui::components::CopyIdentifier>(*request.component) || std::holds_alternative<ui::components::CopyAmount>(*request.component));}
        app::Availability CanSubmit(const app::Request& request) const override {
            if(!CopyRequest(request))return fallback.CanSubmit(request);
            const bool money=std::holds_alternative<ui::components::CopyAmount>(*request.component);
            const auto* doc=state.documents.Find(request.document);
            const bool available=host.InputState().clipboard && (!request.document || (doc && state.documents.ActiveId()==request.document && doc->observation!=app::ObservationState::Locked)) && (!money || (!state.privacy.hidden && !state.semantic_hide_toggle));
            return {available,available?"":"Native clipboard is unavailable for this view/value."};
        }
        app::Receipt Submit(const app::Request& request) override {
            if(!CopyRequest(request))return fallback.Submit(request);
            if(!CanSubmit(request).available)return {false,"Copy unavailable; clipboard unchanged."};
            const auto* identifier=std::get_if<ui::components::CopyIdentifier>(&*request.component);
            const bool copied=identifier?host.Input().Copy(identifier->value):host.Input().Copy(std::get<ui::components::CopyAmount>(*request.component).value.btc(),true);
            return {copied,copied?"Copied to the native clipboard.":host.InputState().notice};
        }
        app::HostObservations Observe() const override{return fallback.Observe();}
    } native(host,workspace,requests);
    auto facts=ui::shell::DrawWorkspace(workspace,native,theme,appearance);
    const auto native_notice=host.InputState().notice;if(!native_notice.empty())workspace.notice=native_notice;
    if(facts.appearance_requested>=0)host.SetAppearance(facts.appearance_requested);
    return facts;
}
std::string BuildInfo()
{
    auto* volatile platform_entry = &ImGui_ImplGlfw_InitForOther;
#if defined(__APPLE__)
    const bool renderer_linked = false; // Metal native host remains follow-on work.
#else
    auto* volatile renderer_entry = &ImGui_ImplOpenGL3_Init;
    const bool renderer_linked = renderer_entry != nullptr;
#endif
    return "\"glfw\":\"" + std::string(glfwGetVersionString()) + "\",\"backend_symbols_linked\":" +
           (platform_entry && renderer_linked ? "true" : "false") + ",\"native_initialized\":false";
}
int Run()
{
    NativeOptions options;
    if (const auto* config=std::getenv("XDG_CONFIG_HOME"); config && std::filesystem::path(config).is_absolute())
        options.appearance_file=std::filesystem::path(config)/"roots-gui/appearance";
    else if (const auto* home=std::getenv("HOME"); home && std::filesystem::path(home).is_absolute())
        options.appearance_file=std::filesystem::path(home)/".config/roots-gui/appearance";
    struct ShellRequests final:app::Requests {
        NativeHost* host{};ui::components::SemanticExchange* semantics{};
        app::Availability CanSubmit(const app::Request& request) const override {return {request.command==app::Command::Quit,app::MissingDependency(request.command)};}
        app::Receipt Submit(const app::Request& request) override {
            if(request.command==app::Command::Quit){semantics->Close();host->RequestClose();return {true,{}};}
            return {false,app::MissingDependency(request.command)};
        }
    } requests;
    ui::shell::State workspace;
    struct SemanticLifetime {ui::components::SemanticExchange& exchange;~SemanticLifetime(){exchange.Close();}} lifetime{workspace.presentation.exchange};
    requests.semantics=&workspace.presentation.exchange;
    options.accessibility=&workspace.presentation.exchange;
    NativeHost host(options,nullptr,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance) {
        DrawShellFrame(*requests.host,workspace,requests,theme,appearance);
    });
    requests.host=&host;
    return host.Run();
}
}
