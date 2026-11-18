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
    ~Observer() override{if(clipboard_window)XDestroyWindow(display,clipboard_window);if(display)XCloseDisplay(display);if(xtst)dlclose(xtst);}
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
    Window clipboard_window{};
    Atom clipboard_property{},clipboard_utf8{};
    uint64_t clipboard_before{};
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
    void Press(std::string_view label)
    {
        const auto packet=state.presentation.exchange.Published();
        const auto found=std::ranges::find_if(packet->nodes,[&](const auto& item){
            return item.name==label && !item.state.disabled && c::ax::Supports(item.actions,c::ax::SemanticAction::Press);
        });
        Check(found!=packet->nodes.end(),"current accessible document control");
        Check(state.presentation.exchange.QueueNext(packet->generation,found->id,c::ax::SemanticAction::Press).second
            ==c::ActionResult::Queued,"actual document accessibility activation");
    }
    void ReadClipboard()
    {
        clipboard_window=XCreateSimpleWindow(display,DefaultRootWindow(display),0,0,1,1,0,0,0);
        clipboard_property=XInternAtom(display,"ROOTS_WORKBENCH_TEST_BYTES",False);
        clipboard_utf8=XInternAtom(display,"UTF8_STRING",False);
        XConvertSelection(display,XInternAtom(display,"CLIPBOARD",False),clipboard_utf8,clipboard_property,clipboard_window,CurrentTime);
        XFlush(display);
    }
    std::optional<std::string> Clipboard()
    {
        XEvent event;
        if (!XCheckTypedWindowEvent(display,clipboard_window,SelectionNotify,&event)) return {};
        Check(event.xselection.property==clipboard_property,"application owns and delivers clipboard bytes");
        Atom type{};
        int format{};
        unsigned long count{},after{};
        unsigned char* value{};
        Check(XGetWindowProperty(display,clipboard_window,clipboard_property,0,32,True,clipboard_utf8,
            &type,&format,&count,&after,&value)==Success && type==clipboard_utf8 && format==8 && !after && value,
            "native clipboard UTF-8 payload retrieved");
        std::string result(reinterpret_cast<char*>(value),count);
        XFree(value);
        XDestroyWindow(display,clipboard_window);
        clipboard_window=0;
        return result;
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
        if (stage>=11 && (!output || !output->valid)) return;
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
            if (!doc || doc->id==binary_id || !doc->imported || panel.close_pending || !semantic("Raw witness transaction ID")) return;
            hex_id=doc->id;
            Check(doc->imported->parsed->source==Read("raw.txt") && doc->imported->parsed->transaction->outputs[0].value.sats()==1,
                "exact raw text whitespace, witness and value");
            Check(semantic("c694f12193d204e4bbf0308a28e5b291c0242189299c6910c2beaba0f0383cec")
                && semantic("0e593fee5e6b7b912591f6afdd04035ce627f2b50c99c7504921ea15111efec3"),
                "independently calculated txid and wtxid identify the supplied raw transaction");
            Capture(frame,"workbench-overview");
            Click(host,facts.transaction_document.at("input-0"));
            break;
        case 12:
            if (!semantic("Input detail")) return;
            Check(semantic("Previous amount and script: Unknown") && semantic("Spending history: Unknown")
                && semantic("Witness item 1"),"exact raw witness and honest unknown previous data");
            Capture(frame,"workbench-input");
            Press("Transaction outputs");
            break;
        case 13:
            if (!facts.transaction_document.contains("output-0")) return;
            Press("Output 1");
            break;
        case 14:
            if (!semantic("Output detail")) return;
            Check(semantic("bcrt1") && semantic("0.00000001 BTC") && semantic("P2WPKH")
                && semantic("Spending history: Unknown"),"raw amount, script classification, address encoding and unknown history");
            Capture(frame,"workbench-output");
            Press("Output amount");
            break;
        case 15:
            if (!doc || doc->id!=hex_id || !semantic("Wire hex · supplied raw transaction bytes")) return;
            Check(doc->imported->representation==app::DocumentRepresentation::WireHex,"selected active representation recorded");
            Check(semantic("Offset 49 · 8 bytes") && semantic("0100000000000000"),"exact output number range is selected in witnessed wire");
            Capture(frame,"workbench-hex");
            clipboard_before=host.InputState().clipboard_writes;
            Press("Copy selected bytes as hex");
            break;
        case 16:
            if (host.InputState().clipboard_writes==clipboard_before) return;
            ReadClipboard();
            break;
        case 17: {
            const auto value=Clipboard();
            if (!value) return;
            Check(*value=="0100000000000000","received real exact selected hexadecimal X11 clipboard content");
            std::ofstream(out/"copied-hex.txt")<<*value<<'\n';
            Press("Save selected bytes");
            break;
        }
        case 18:
            if (!modal.contains("save-inspection") || !semantic("Local file path")) return;
            Check(semantic("Offset 49 · 8 bytes"),"file dialog confirms frozen representation range");
            Set("Local file path",(files/"wire-selection.bin").string());
            break;
        case 19:
            if (!modal.contains("save-inspection") || modal.at("save-inspection").disabled) return;
            click("save-inspection");
            break;
        case 20:
            if (panel.dialog.open || panel.close_pending) return;
            Check(Read("wire-selection.bin")==std::string("\x01\0\0\0\0\0\0\0",8),"atomic selected-byte export kept exact binary bytes");
            Control(XK_t);
            break;
        case 21:
            if (!modal.contains("inspect-clipboard")) return;
            Check(host.Input().Copy(Read("future.txt")),"owned future-version fixture");
            click("inspect-clipboard");
            break;
        case 22:
            if (!semantic("Unsupported PSBT version 3")) return;
            Check(state.documents.Documents().size()==2 && doc && doc->id==hex_id,"unsupported version keeps existing workspace");
            Capture(frame,"offline-invalid");
            click("cancel");
            break;
        case 23:
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
        observer.Check(observer.stage==24 && observer.frames==6 && observer.cleaned,"complete offline native document journey");
        std::ofstream out(observer.out/"result.json");
        out<<"{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":"<<observer.checks
            <<",\"frames\":"<<observer.frames<<",\"teardown\":true,\"rpcPorts\":0}\n";
        return out?0:1;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
