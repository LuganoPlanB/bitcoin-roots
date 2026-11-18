// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cerrno>
#include <sys/socket.h>
#include <unistd.h>

using namespace roots::gui;
namespace {
namespace shell=ui::shell;
namespace c=ui::components;
using Clock=std::chrono::steady_clock;

struct Requests final:app::Requests {
    mutable TransactionDocuments local;
    app::RecoverySession* recovery{};
    TransactionDocuments* LocalDocuments() const override{return &local;}
    app::RecoverySession* Recovery() override{return recovery;}
    app::Receipt Submit(const app::Request&) override{throw std::runtime_error("unexpected external command");}
};

struct Observer final:platform::NativeObserver {
    Observer(std::filesystem::path directory,std::filesystem::path paths)
        : out(std::move(directory)), files(std::move(paths)), display(XOpenDisplay(nullptr)), xtst(dlopen("libXtst.so.6",RTLD_NOW))
    {
        Check(display && xtst,"owned native input");
        motion=reinterpret_cast<decltype(motion)>(dlsym(xtst,"XTestFakeMotionEvent"));
        button=reinterpret_cast<decltype(button)>(dlsym(xtst,"XTestFakeButtonEvent"));
        key=reinterpret_cast<decltype(key)>(dlsym(xtst,"XTestFakeKeyEvent"));
        Check(motion && button && key,"owned native input entry points");
    }
    ~Observer() override{if(display)XCloseDisplay(display);if(xtst)dlclose(xtst);}
    Requests requests;
    shell::State state;
    shell::Facts facts;
    std::filesystem::path out,files;
    Display* display;
    void* xtst;
    int(*motion)(Display*,int,int,int,unsigned long){};
    int(*button)(Display*,unsigned int,Bool,unsigned long){};
    int(*key)(Display*,unsigned int,Bool,unsigned long){};
    const Clock::time_point started{Clock::now()};
    uint64_t binary_id{},hex_id{};
    bool lift{}, release{}, cleaned{};
    KeySym held{};
    int stage{},delay{},checks{},frames{};


    void Check(bool condition,const char* why)
    {
        if(!condition)throw std::runtime_error("stage "+std::to_string(stage)+": "+why);
        ++checks;
    }
    bool CaptureFramebuffer() const override{return true;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override
    {
        if(Clock::now()-started<std::chrono::seconds(42))return;
        std::ofstream(out/"deadline.txt")<<stage<<' '<<state.local_document.error<<'\n'<<state.presentation.Snapshot();
        throw std::runtime_error("native document deadline at stage "+std::to_string(stage));
    }
    void Capture(const platform::NativeFrame& f,const char* tag)
    {
        Check(f.width==960 && f.height==640 && f.effective_scale==1.5F && f.decorated && f.font_texture && !f.gl_error,"owned compact native geometry");
        const auto publication=state.presentation.exchange.Published();
        Check(publication && publication->valid && state.presentation.Tree().Validate() && !state.presentation.Tree().overflow,"current bounded semantic packet");
        std::ofstream image(out/(std::string(tag)+".ppm"),std::ios::binary);
        image<<"P6\n"<<f.framebuffer_width<<' '<<f.framebuffer_height<<"\n255\n";
        for(int y=f.framebuffer_height-1;y>=0;--y)for(int x=0;x<f.framebuffer_width;++x)
            image.write(reinterpret_cast<const char*>(f.rgba.data()+(size_t(y)*f.framebuffer_width+x)*4),3);
        Check(bool(image),"framebuffer written");
        std::ofstream semantics(out/(std::string(tag)+".semantics.txt"));
        semantics<<state.presentation.Snapshot();
        Check(bool(semantics),"semantics written");
        ++frames;
    }
    void Click(platform::NativeHost& host,const c::ItemFacts& item)
    {
        const auto& r=item.rect;
        if (!item.visible || item.disabled || r[0]<0 || r[1]<0 || r[2]>960 || r[3]>640) {
            std::ofstream diagnostic(out/"reachability.txt");
            for (const auto& [name,fact]:facts.local_document) {
                diagnostic << name << ' ' << fact.visible << ' ' << fact.disabled;
                for (const auto value:fact.rect) diagnostic << ' ' << value;
                diagnostic << '\n';
            }
            diagnostic << state.presentation.Snapshot();
            Check(false,"reachable native control");
        }
        ++checks;
        int x{},y{};
        glfwGetWindowPos(host.Window(),&x,&y);
        Check(motion(display,-1,x+int((r[0]+r[2])/2),y+int((r[1]+r[3])/2),0) && button(display,1,True,0),"native press");
        lift=true;
        XSync(display,False);
    }
    void Key(KeySym symbol,bool down){Check(key(display,XKeysymToKeycode(display,symbol),down,0),"owned key input");}
    void Control(KeySym symbol)
    {
        held=symbol;Key(XK_Control_L,true);Key(symbol,true);release=true;XSync(display,False);
    }
    void Set(std::string_view label,std::string value)
    {
        const auto packet=state.presentation.exchange.Published();
        const auto found=std::ranges::find_if(packet->nodes,[&](const auto& item){
            return item.name==label && c::ax::Supports(item.actions,c::ax::SemanticAction::SetValue);
        });
        Check(found!=packet->nodes.end(),"actual document field supports accessible editing");
        Check(state.presentation.exchange.QueueNext(packet->generation,found->id,c::ax::SemanticAction::SetValue,std::move(value)).second
            ==c::ActionResult::Queued,"actual document accessibility input");
    }
    std::string Read(const std::filesystem::path& name) const
    {
        std::ifstream stream(files/name,std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(stream),{});
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override
    {
        if (lift) {Check(button(display,1,False,0),"native release");lift=false;XSync(display,False);return;}
        if (release) {Key(held,false);Key(XK_Control_L,false);release=false;XSync(display,False);return;}
        if (delay) {--delay;return;}
        if (frame.rendered_frames<5) return;
        const auto* doc=state.documents.Active();
        const auto& panel=state.local_document;
        const auto& modal=facts.local_document;
        const auto click=[&](const char* name){Click(host,modal.at(name));};
        const auto output=state.presentation.exchange.Published();
        const auto semantic=[&](std::string_view word) {
            return output && output->valid && std::ranges::any_of(output->nodes,[&](const auto& item) {
                return item.name.find(word)!=std::string::npos || item.value.find(word)!=std::string::npos;
            });
        };
        switch (stage) {
        case 0:
            Control(XK_f);
            break;
        case 1:
            if (!modal.contains("inspect-file") || !semantic("Local file path")) return;
            Set("Local file path",(files/"binary-with-text-extension.txt").string());
            break;
        case 2:
            if (!modal.contains("inspect-file") || modal.at("inspect-file").disabled) return;
            click("inspect-file");
            break;
        case 3:
            if (!doc || !doc->imported || !semantic("PSBT v2") || panel.close_pending) return;
            binary_id=doc->id;
            Check(doc->imported->parsed->source==Read("binary-with-text-extension.txt") &&
                doc->imported->parsed->format==protocol::DocumentFormat::PsbtBinary,"sniff actual binary PSBT and keep exact original");
            Check(semantic("Wallet ownership: Unknown") && semantic("Fee: Unknown")
                  && doc->imported->network==protocol::DisplayNetwork::Unknown,"imported metadata grants no provenance");
            Capture(frame,"offline-psbt");
            Control(XK_s);
            break;
        case 4:
            if (!modal.contains("save-original") || !semantic("Local file path")) return;
            Set("Local file path",(files/"existing.psbt").string());
            break;
        case 5:
            if (!modal.contains("save-original") || modal.at("save-original").disabled) return;
            click("save-original");
            break;
        case 6:
            if (!semantic("Existing files are retained")) return;
            Check(Read("existing.psbt")=="sentinel","lossless save refuses to overwrite");
            Set("Local file path",(files/"saved.psbt").string());
            break;
        case 7:
            if (!modal.contains("save-original") || modal.at("save-original").disabled) return;
            click("save-original");
            break;
        case 8:
            if (panel.dialog.open || panel.close_pending) return;
            Check(Read("saved.psbt")==Read("binary-with-text-extension.txt"),"exact original PSBT binary save");
            Control(XK_t);
            break;
        case 9:
            if (!modal.contains("inspect-clipboard") || !semantic("Display network")) return;
            Set("Display network","Regtest");
            break;
        case 10:
            if (panel.network!=protocol::DisplayNetwork::Regtest || !modal.contains("inspect-clipboard")) return;
            Check(host.Input().Copy(Read("raw.txt")),"owned raw transaction hex clipboard");
            click("inspect-clipboard");
            break;
        case 11:
            if (!doc || doc->id==binary_id || !doc->imported || panel.close_pending || !semantic("bcrt1")) return;
            hex_id=doc->id;
            Check(doc->imported->parsed->source==Read("raw.txt") && doc->imported->parsed->transaction->outputs[0].value.sats()==1,
                "raw text whitespace, exact value and network address");
            Click(host,facts.transaction_document.at("representation"));
            break;
        case 12:
            if (!doc || doc->id!=hex_id || !semantic("Wire hex")) return;
            Check(doc->imported->representation==app::DocumentRepresentation::WireHex,"selected active representation recorded");
            Capture(frame,"offline-raw");
            Control(XK_t);
            break;
        case 13:
            if (!modal.contains("inspect-clipboard")) return;
            Check(host.Input().Copy(Read("future.txt")),"owned future-version fixture");
            click("inspect-clipboard");
            break;
        case 14:
            if (!semantic("Unsupported PSBT version 3")) return;
            Check(state.documents.Documents().size()==2 && doc && doc->id==hex_id,"unsupported version keeps existing workspace");
            Capture(frame,"offline-invalid");
            click("cancel");
            break;
        case 15:
            if (panel.dialog.open || panel.close_pending || requests.local.Busy()) return;
            Check(requests.recovery->Enable(state.documents),"explicit local public recovery");
            host.RequestClose();
            break;
        default:return;
        }
        ++stage;
        delay=3;
    }
    void Teardown(const platform::NativeTeardown& result) override
    {
        cleaned=result.renderer_cleared && result.platform_cleared && result.textures_released && result.imgui_cleared
            && result.context_cleared && result.accessibility_cleared && result.input_cleared;
        Check(cleaned && !requests.local.Busy(),"native and local parser resources settled");
    }
};

int Restore(const std::filesystem::path& path)
{
    app::RecoverySession recovery(path/"metadata.sqlite");
    app::DocumentRegistry registry;
    if (!recovery.Start() || !recovery.Pending() || !recovery.Restore(registry) || registry.Documents().size()!=2) return 1;
    const auto& first=registry.Documents()[0];
    const auto& second=registry.Documents()[1];
    if (!first.imported || !second.imported || first.binding || second.binding
        || first.observation!=app::ObservationState::Offline || second.observation!=app::ObservationState::Offline
        || first.imported->parsed->psbt_version!=2 || second.imported->representation!=app::DocumentRepresentation::WireHex
        || second.imported->network!=protocol::DisplayNetwork::Regtest) return 1;
    for (const auto& [document,name]:{std::pair{&first,"binary-with-text-extension.txt"},std::pair{&second,"raw.txt"}}) {
        std::ifstream file(path/name,std::ios::binary);
        const std::string content(std::istreambuf_iterator<char>(file),{});
        if (document->imported->parsed->source!=content) return 1;
    }
    std::cout<<"{\"exactPublicDocumentsRestored\":2,\"savedActiveRepresentation\":true,\"rpcPorts\":0}\n";
    return 0;
}
}

int main(int argc,char** argv)
{
    if (argc==3 && std::string_view(argv[1])=="--restore") return Restore(argv[2]);
    if (argc!=4) return 2;
    try {
        const auto* display=std::getenv("DISPLAY");
        const auto* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if (!display || !owned || std::string_view(display)!=owned) throw std::runtime_error("owned display required");
        std::filesystem::create_directories(argv[1]);
        Observer observer(argv[1],argv[3]);
        for (const int family:{AF_INET,AF_INET6}) {
            const int fd=::socket(family,SOCK_STREAM,0);
            const bool prohibited=fd<0 && errno==EPERM;
            if (fd>=0) ::close(fd);
            observer.Check(prohibited,"socket filter denies IPv4 and IPv6 in the native process");
        }
        app::RecoverySession recovery(observer.files/"metadata.sqlite");
        observer.Check(recovery.Start() && !recovery.Pending(),"fresh native local workspace");
        observer.requests.recovery=&recovery;
        platform::NativeOptions options{960,640,false,1.5F,argv[2]};
        options.fixed_delta_seconds=1.F/60;
        options.accessibility=&observer.state.presentation.exchange;
        platform::NativeHost* active{};
        {
            platform::NativeHost host(options,&observer,[&](const auto& theme,const auto& appearance){
                observer.facts=platform::DrawShellFrame(*active,observer.state,observer.requests,theme,appearance);
            });
            active=&host;
            host.Run();
        }
        observer.Check(observer.stage==16 && observer.cleaned,"complete offline native document journey");
        std::ofstream out(observer.out/"result.json");
        out<<"{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":"<<observer.checks
            <<",\"frames\":"<<observer.frames<<",\"teardown\":true,\"rpcPorts\":0}\n";
        return out?0:1;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
