// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <gio/gio.h>
using namespace roots::gui;
namespace a=app;namespace c=ui::components;namespace sh=ui::shell;
namespace {
std::string Quote(std::string_view text){std::string out="\"";constexpr char hex[]="0123456789abcdef";for(unsigned char ch:text){if(ch=='"' || ch=='\\'){out+='\\';out+=ch;}else if(ch<32){out+="\\u00";out+=hex[ch>>4];out+=hex[ch&15];}else out+=ch;}return out+'"';}
struct Fixture final:a::Requests,platform::NativeObserver {
    sh::State state;sh::Facts facts;platform::NativeHost* host{};std::filesystem::path out;std::vector<a::Request> requests;
    bool input_fixture{},reject{},cleaned{};unsigned refresh{};a::HostObservations observations;
    const std::chrono::seconds static_limit{195};
    const std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    a::Availability CanSubmit(const a::Request& r) const override {const bool available=r.command==a::Command::OpenWallet || r.command==a::Command::Refresh || r.command==a::Command::Component || r.command==a::Command::LockCurrent || r.command==a::Command::Quit || r.command==a::Command::DocumentMetadata;return {available,available?"Explicit effect-free accessibility fixture.":a::MissingDependency(r.command)};}
    a::HostObservations Observe() const override{return observations;}
    a::Receipt Submit(const a::Request& r) override {
        requests.push_back(r);if(reject)return {false,"Fixture acknowledgement rejected; draft retained."};
        if(r.command==a::Command::OpenWallet)state.documents=a::PublicFixtureDocuments();
        if(r.command==a::Command::LockCurrent)if(auto* d=state.documents.Find(r.document))d->observation=a::ObservationState::Locked;
        if(r.command==a::Command::Refresh){++refresh;observations.connection={a::ServiceState::Stale,"Fixture refresh stale; last success 12:00 UTC."};observations.backend_height=123456;}
        if(r.command==a::Command::Quit)host->RequestClose();
        return {true,"Explicit in-memory fixture acknowledged; no files or daemon were accessed."};
    }
    void Idle(platform::NativeHost& host,const platform::NativeFrame&) override {
        if(std::chrono::steady_clock::now()-start>static_limit)throw std::runtime_error("accessibility fixture watchdog");
        // Test-executable-only control of the native port. No product hook or
        // production environment flag admits these explicit fixture requests.
        if(!input_fixture || !std::filesystem::exists(out/"native-request"))return;
        std::ifstream stream(out/"native-request");std::string sequence,operation;std::getline(stream,sequence);std::getline(stream,operation);stream.close();std::filesystem::rename(out/"native-request",out/("native-request-"+sequence));bool accepted{};
        if(operation=="copy-sensitive")accepted=host.Input().Copy("0.12345678",true);
        if(operation=="copy-public")accepted=host.Input().Copy("Public fixture identifier",false);
        if(operation=="wrong-thread"){std::thread worker([&]{try{host.Input().Status();}catch(const std::logic_error&){accepted=true;}});worker.join();}
        if(operation=="motion-on" || operation=="motion-off"){auto* settings=g_settings_new("org.gnome.desktop.interface");accepted=g_settings_set_boolean(settings,"enable-animations",operation=="motion-on");g_object_unref(settings);}
        std::ofstream(out/("native-receipt-"+sequence+".json"))<<"{\"accepted\":"<<(accepted?"true":"false")<<"}\n";
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override {
        const auto status=host.AccessibilityState();
        if(ImGui::IsKeyPressed(ImGuiKey_Enter,false) || ImGui::IsKeyPressed(ImGuiKey_Space,false)){std::ofstream trace(out/"native-activation.jsonl",std::ios::app);trace<<"{\"enter\":"<<(ImGui::IsKeyPressed(ImGuiKey_Enter,false)?"true":"false")<<",\"space\":"<<(ImGui::IsKeyPressed(ImGuiKey_Space,false)?"true":"false")<<",\"nav\":"<<(ImGui::GetIO().NavVisible?"true":"false")<<",\"active\":"<<(ImGui::IsAnyItemActive()?"true":"false")<<",\"requests\":"<<requests.size()<<",\"focusedRows\":[";bool comma=false;for(const auto& row:facts.rows)if(row.focused){if(comma)trace<<",";trace<<row.id;comma=true;}trace<<"]}\n";}
        std::ofstream f(out/"state.pending");f<<"{\"frames\":"<<frame.rendered_frames<<",\"registered\":"<<(status.registered?"true":"false")<<",\"bus\":"<<Quote(status.bus_name)<<",\"error\":"<<Quote(status.error)<<",\"documents\":"<<state.documents.Documents().size()<<",\"active\":"<<state.documents.ActiveId()<<",\"requests\":"<<requests.size()<<",\"refresh\":"<<refresh<<",\"hidden\":"<<(state.privacy.hidden?"true":"false")<<",\"locked\":"<<(state.documents.Active() && state.documents.Active()->observation==a::ObservationState::Locked?"true":"false");
        if(auto* d=state.documents.Active())f<<",\"note\":"<<Quote(d->Current().note.draft())<<",\"saved\":"<<Quote(d->Current().note.saved)<<",\"dirty\":"<<(d->Current().note.dirty()?"true":"false");
        const auto input=host.InputState();
        f<<",\"input\":{\"clipboard\":"<<(input.clipboard?"true":"false")<<",\"chooser\":"<<(input.file_dialog?"true":"false")<<",\"active\":"<<(host.Input().NativeDialogActive()?"true":"false")<<",\"callbacks\":"<<input.callbacks<<",\"pastes\":"<<input.pastes<<",\"cancelled\":"<<input.cancelled<<",\"reads\":"<<input.clipboard_reads<<",\"writes\":"<<input.clipboard_writes<<",\"notice\":"<<Quote(input.notice)<<",\"pid\":"<<Quote(input.dialog_process)<<",\"scale\":"<<input.system_scale.value_or(0)<<",\"reduced\":"<<(input.reduced_motion.value_or(true)?"true":"false")<<"},\"importPath\":"<<Quote(state.actions.source.data())<<",\"importError\":"<<Quote(state.actions.error)<<",\"width\":"<<frame.width<<",\"height\":"<<frame.height<<",\"scale\":"<<frame.effective_scale;
        f<<"}\n";f.close();std::filesystem::rename(out/"state.pending",out/"state.json");
    }
    void Teardown(const platform::NativeTeardown& t) override {cleaned=t.input_cleared && t.accessibility_cleared && t.renderer_cleared && t.platform_cleared && t.textures_released && t.imgui_cleared && t.context_cleared;std::ofstream(out/"teardown.json")<<"{\"cleaned\":"<<(cleaned?"true":"false")<<",\"accessibilityClosedBeforeContext\":"<<(t.accessibility_cleared?"true":"false")<<",\"requests\":"<<requests.size()<<"}\n";}
};
}
int main(int argc,char** argv){if(argc!=3 && argc!=4)return 2;try{
    const char* display=std::getenv("DISPLAY"),*owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");if(!display || !owned || std::string(display)!=owned)throw std::runtime_error("owned authenticated display required");
    Fixture f;f.input_fixture=argc==4 && std::string_view(argv[3])=="input";f.out=argv[1];std::filesystem::create_directories(f.out);
    {platform::NativeOptions options;if(argc==4 && std::string_view(argv[3])=="input"){options.width=960;options.height=640;options.font_scale=1.5F;}options.font_directory=argv[2];options.accessibility=&f.state.presentation.exchange;
     platform::NativeHost host(options,&f,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){f.facts=platform::DrawShellFrame(*f.host,f.state,f,theme,appearance);});f.host=&host;host.Run();}
    return f.cleaned?0:1;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
