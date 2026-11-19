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
    uint64_t pinned_id{},unsigned_id{},signed_id{};
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
        throw std::runtime_error("native offline draft deadline at stage "+std::to_string(stage));
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
        const auto hit=std::ranges::find_if(packet->nodes,[&](const auto& node) {
            return node.name==label && c::ax::Supports(node.actions,c::ax::SemanticAction::Press);
        });
        Check(hit!=packet->nodes.end(),"visible native semantic press action");
        Check(state.presentation.exchange.QueueNext(packet->generation,hit->id,c::ax::SemanticAction::Press,{}).second
              ==c::ActionResult::Queued,"native semantic press queued");
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override
    {
        if (lift) {Check(button(display,1,False,0),"native release");lift=false;XSync(display,False);return;}
        if (release) {Key(held,false);Key(XK_Control_L,false);release=false;XSync(display,False);return;}
        if (delay) {--delay;return;}
        if (frame.rendered_frames<5) return;
        const auto* doc=state.documents.Active();
        const auto& modal=facts.local_document;
        const auto& buttons=facts.transaction_document;
        const auto click=[&](const char* name){Click(host,modal.at(name));};
        const auto local=[&](const char* name){Click(host,buttons.at(name));};
        const auto packet=state.presentation.exchange.Published();
        const auto semantic=[&](std::string_view word) {
            return packet && packet->valid && std::ranges::any_of(packet->nodes,[&](const auto& node) {
                return node.name.find(word)!=std::string::npos || node.value.find(word)!=std::string::npos;
            });
        };
        switch (stage) {
        case 0:
            if (!doc || !doc->coin_draft || !buttons.contains("offline-builder")) return;
            pinned_id=doc->id;
            local("offline-builder");
            break;
        case 1: {
            if (!semantic("Recipient address")) return;
            std::ifstream address(files/"recipient.txt");
            std::string value;
            address>>value;
            Check(!value.empty(),"fixture recipient supplied");
            Set("Recipient address",value);
            break;
        }
        case 2:
            if (!semantic("Recipient amount (BTC)"))return;
            Set("Recipient amount (BTC)","0.00123456");
            break;
        case 3:
            if (!semantic("Fee (BTC)"))return;
            Set("Fee (BTC)","0.00001200");
            break;
        case 4:
            if (!semantic("Use cached change output"))return;
            Press("Use cached change output");
            break;
        case 5:
            if (!buttons.contains("make-offline-draft") || !semantic("Live spendability unknown"))return;
            Check(state.offline_builders.at(pinned_id).use_change,"explicit cached change choice");
            Capture(frame,"offline-builder");
            if (buttons.at("make-offline-draft").visible) local("make-offline-draft");
            else Press("Build unsigned PSBT");
            break;
        case 6:
            if (!doc || !doc->imported || !semantic("PSBT v0"))return;
            unsigned_id=doc->id;
            Check(doc->imported->parsed->transaction->fee==Money::Satoshis(1200)
                  && doc->imported->parsed->transaction->outputs.size()==2,"known fee and exact change");
            Control(XK_s);
            break;
        case 7:
            if (!modal.contains("save-original") || !semantic("Local file path"))return;
            Set("Local file path",(files/"unsigned.psbt").string());
            break;
        case 8:
            if (!modal.contains("save-original") || modal.at("save-original").disabled)return;
            click("save-original");
            break;
        case 9:
            if (state.local_document.dialog.open || state.local_document.close_pending)return;
            (void)std::ofstream(files/"unsigned-ready");
            break;
        case 10:
            if (!buttons.contains("record-review"))return;
            Press("Record local review");
            break;
        case 11:
            if (!semantic("Local review recorded") || !semantic("Unsigned transaction commitment"))return;
            Check(state.imported_views.at(unsigned_id).proved_change_output==1,"reviewed independent wallet change");
            break;
        case 12:
            if (!std::filesystem::exists(files/"signed-ready") || !buttons.contains("merge"))return;
            local("merge");
            break;
        case 13:
            if (!modal.contains("inspect-file") || !semantic("Local file path"))return;
            Set("Local file path",(files/"wrong-signature.psbt").string());
            break;
        case 14:
            if (!modal.contains("inspect-file") || modal.at("inspect-file").disabled)return;
            click("inspect-file");
            break;
        case 15:
            if (!semantic("Signature does not validate"))return;
            Check(doc && doc->id==unsigned_id,"wrong external signature preserves working document");
            Check(bool(state.imported_views.at(unsigned_id).reviewed),"bad signature alone did not replace reviewed transaction");
            Set("Local file path",(files/"signed.psbt").string());
            break;
        case 16:
            if (!modal.contains("inspect-file") || modal.at("inspect-file").disabled)return;
            click("inspect-file");
            break;
        case 17:
            if (!doc || doc->id==unsigned_id || !doc->imported || !semantic("input policies verified"))return;
            signed_id=doc->id;
            Check(doc->imported->parsed->wire.find("offline-proprietary-example")!=std::string::npos,
                "opaque external map survives merged working PSBT");
            Check(semantic("Local review recorded") && state.imported_views.at(signed_id).proved_change_output==1,
                "equivalent signed PSBT retains reviewed identity and independent change");
            Capture(frame,"offline-signed");
            local("merge");
            break;
        case 18:
            if (!modal.contains("inspect-file") || !semantic("Local file path"))return;
            Set("Local file path",(files/"wrong-transaction.psbt").string());
            break;
        case 19:
            if (!modal.contains("inspect-file") || modal.at("inspect-file").disabled)return;
            click("inspect-file");
            break;
        case 20:
            if (!semantic("Output 1 amount or order changed."))return;
            Check(doc && doc->id==signed_id && !state.imported_views.at(signed_id).reviewed
                  && state.imported_views.at(unsigned_id).reviewed,"review revocation is tied to the exact target tab");
            click("cancel");
            break;
        case 21: {
            if (!semantic("Review revoked. Output 1 amount or order changed."))return;
            const auto& revoked=state.imported_views.at(signed_id);
            Check(!revoked.reviewed && !revoked.proved_change_output && buttons.contains("record-review")
                  && !buttons.at("record-review").disabled,
                  "revocation removes review authority while retaining public transaction identity and fresh review");
            Capture(frame,"offline-review-revoked");
            Press("Finalize locally");
            break;
        }
        case 22:
            if (!doc || !doc->imported || !semantic("Raw transaction ID"))return;
            Check(doc->id!=signed_id && doc->imported->parsed->transaction->witness &&
                  state.documents.Find(pinned_id) && state.documents.Find(unsigned_id) &&
                  state.documents.Find(signed_id),"finalized raw result preserves every original tab");
            Capture(frame,"offline-finalized");
            Control(XK_s);
            break;
        case 23:
            if (!modal.contains("save-original") || !semantic("Local file path"))return;
            Set("Local file path",(files/"final.tx").string());
            break;
        case 24:
            if (!modal.contains("save-original") || modal.at("save-original").disabled)return;
            click("save-original");
            break;
        case 25:
            if (state.local_document.dialog.open || state.local_document.close_pending || requests.local.Busy())return;
            Check(std::filesystem::file_size(files/"final.tx")>0,"final bytes saved");
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
}

int main(int argc,char** argv)
{
    if (argc!=4)return 2;
    try {
        const auto* display=std::getenv("DISPLAY");
        const auto* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if (!display || !owned || std::string_view(display)!=owned)throw std::runtime_error("owned display required");
        std::filesystem::create_directories(argv[1]);
        Observer observer(argv[1],argv[3]);
        for (const int family:{AF_INET,AF_INET6}) {
            const int fd=::socket(family,SOCK_STREAM,0);
            const bool denied=fd<0 && errno==EPERM;
            if (fd>=0) ::close(fd);
            observer.Check(denied,"network sockets denied in the offline GUI");
        }
        app::RecoverySession recovery(observer.files/"metadata.sqlite");
        observer.Check(recovery.Start() && recovery.Pending() && recovery.Restore(observer.state.documents),
            "explicit restore of previously observed public wallet snapshot");
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
        observer.Check(observer.stage==26 && observer.cleaned,"complete native offline draft journey");
        std::ofstream report(observer.out/"result.json");
        report<<"{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"checks\":"<<observer.checks
            <<",\"frames\":"<<observer.frames<<",\"teardown\":true,\"rpcPorts\":0}\n";
        return report?0:1;
    } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
