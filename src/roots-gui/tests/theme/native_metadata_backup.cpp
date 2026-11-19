// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "app/history_workspace.h"
#include "platform/shell_composition.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sodium.h>
#include <sys/stat.h>

using namespace roots::gui;
namespace a=app;
namespace c=ui::components;
namespace p=platform;
namespace sh=ui::shell;
namespace fs=std::filesystem;
namespace {
struct Host final:a::Requests {
    explicit Host(a::RecoverySession& owner):recovery(owner){}
    a::RecoverySession& recovery;
    a::RecoverySession* Recovery() override { return &recovery; }
    a::Receipt Submit(const a::Request&) override { throw std::runtime_error("metadata recovery unexpectedly used a host command"); }
};

enum class Step { OpenFile, OpenRecovery, SaveRecoveryView, OpenBackup, BackupPath, BackupPassword, Confirm,
                  WriteBackup, Saved, LeaveBackup, OpenRestore, RestorePath, RestorePassword, Inspect, Preview, Restored,
                  CloseRecovery, Complete };

struct Observer final:p::NativeObserver {
    Observer(a::RecoverySession& recovery,fs::path out,fs::path root,std::string pass,a::WalletBinding wallet)
        : host(recovery),output(std::move(out)),archive(root/"owned-wallet.rmeta"),password(std::move(pass)),
          wallet_scope(a::HistoryMetadataScope(wallet))
    {
        id=state.documents.OpenWallet("Vault local display",std::move(wallet),true);
        state.documents.Active()->sections[1].note.SetDraft("Independent workspace draft");
    }
    ~Observer(){sodium_memzero(password.data(),password.size());}

    sh::State state;
    sh::Facts facts;
    Host host;
    fs::path output,archive;
    std::string password;
    storage::Scope wallet_scope;
    uint64_t id{};
    Step step{Step::OpenFile};
    const std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    int checks{},captures{},settled_frame{},capture_step{-1};
    bool cleaned{},done{};

    void Check(bool ok,const char* message)
    {
        if(!ok)throw std::runtime_error("metadata native stage "+std::to_string(int(step))+": "+message);
        ++checks;
    }
    bool CaptureFramebuffer()const override{return true;}
    void Idle(p::NativeHost&,const p::NativeFrame&)override
    {
        if(std::chrono::steady_clock::now()-start>std::chrono::seconds(17)){
            auto snapshot=state.presentation.Snapshot();
            if(snapshot.find(password)==std::string::npos && snapshot.find(archive.string())==std::string::npos)
                std::ofstream(output/"native-timeout.txt")<<int(step)<<'\n'<<snapshot;
            throw std::runtime_error("metadata native deadline");
        }
    }
    bool Send(std::string_view name,c::ax::SemanticAction operation,std::string value={})
    {
        const auto packet=state.presentation.exchange.Published();
        if(!packet || !packet->valid || packet->overflow)return false;
        for(const auto& node:packet->nodes)if(node.name==name && !node.state.disabled){
            Check(state.presentation.exchange.QueueNext(packet->generation,node.id,operation,std::move(value)).second
                  ==c::ActionResult::Queued,"current native semantic action");
            return true;
        }
        return false;
    }
    bool Bounds(const p::NativeFrame& f)
    {
        if(capture_step!=int(step)){capture_step=int(step);settled_frame=f.rendered_frames+2;}
        auto packet=state.presentation.exchange.Published();
        return f.rendered_frames>=settled_frame && packet && packet->valid && !packet->overflow;
    }
    void Capture(const p::NativeFrame& frame,const char* name,std::initializer_list<std::string_view> values)
    {
        Check(frame.width==960 && frame.height==640 && frame.effective_scale==1.5F && frame.decorated
              && !frame.gl_error && !frame.rgba.empty(),"actual native framebuffer");
        const auto packet=state.presentation.exchange.Published();
        Check(packet && packet->valid && !packet->overflow,"bounded current native semantics");
        std::string snapshot;
        for(const auto& node:packet->nodes)snapshot+=node.name+" | "+node.value+" | "+node.description+'\n';
        Check(snapshot.find(password)==std::string::npos && snapshot.find(archive.string())==std::string::npos,
              "archive password and selected path are absent from semantic capture");
        for(const auto value:values)Check(snapshot.find(value)!=std::string::npos,"expected local recovery evidence");
        std::ofstream ppm(output/(std::string(name)+".ppm"),std::ios::binary);
        ppm<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for(int y=frame.framebuffer_height-1;y>=0;--y)
            for(int x=0;x<frame.framebuffer_width;++x)
                ppm.write(reinterpret_cast<const char*>(frame.rgba.data()+(size_t(y)*frame.framebuffer_width+x)*4),3);
        Check(bool(ppm),"native framebuffer retained");
        std::ofstream(output/(std::string(name)+"-semantics.txt"))<<snapshot;
        ++captures;
    }
    void Frame(p::NativeHost& window,const p::NativeFrame& f)override
    {
        using A=c::ax::SemanticAction;
        auto& panel=state.metadata_backup;
        if(f.rendered_frames<4)return;
        switch(step){
        case Step::OpenFile:
            if(Send("File",A::Press))step=Step::OpenRecovery;
            break;
        case Step::OpenRecovery:
            if(Send("Session Recovery",A::Press))step=Step::SaveRecoveryView;
            break;
        case Step::SaveRecoveryView:
            if(!state.recovery_dialog.open || !Bounds(f))break;
            Capture(f,"recovery-dialog",{"Wallet recovery","daemon wallet backup","signer recovery material","Restore local wallet metadata"});
            if(Send("Back up local wallet metadata",A::Press))step=Step::OpenBackup;
            break;
        case Step::OpenBackup:
            if(panel.mode!=sh::MetadataBackupMode::Export || !Bounds(f))break;
            Check(facts.document_actions.at("metadata-file").focused,"backup focuses archive path");
            step=Step::BackupPath;
            break;
        case Step::BackupPath:
            if(Send("Archive file",A::SetValue,archive.string()))step=Step::BackupPassword;
            break;
        case Step::BackupPassword:
            if(Send("Archive password",A::SetValue,password))step=Step::Confirm;
            break;
        case Step::Confirm:
            if(Send("Confirm password",A::SetValue,password))step=Step::WriteBackup;
            break;
        case Step::WriteBackup:
            if(Send("Save encrypted backup",A::Press))step=Step::Saved;
            break;
        case Step::Saved:
            if(panel.notice.empty())break;
            Check(panel.notice.starts_with("Encrypted metadata backup saved") && panel.password.front()==0
                && panel.confirmation.front()==0,"encrypted export acknowledged and inputs wiped");
            {
                struct stat info{};
                Check(stat(archive.c_str(),&info)==0 && (info.st_mode&0777)==0600,"mode 0600 encrypted backup");
                const auto before=host.recovery.HistoryMetadata(wallet_scope);
                Check(before && bool(host.recovery.SaveCoinFreezes(wallet_scope,{},before.value->revision)),"intervening local freeze change");
                Check(state.documents.Rename(id,"Current tab before restore"),"different current local display");
            }
            step=Step::LeaveBackup;
            break;
        case Step::LeaveBackup:
            if(Send("Back to Recovery",A::Press))step=Step::OpenRestore;
            break;
        case Step::OpenRestore:
            if(panel.mode==sh::MetadataBackupMode::None && Send("Restore local wallet metadata",A::Press))step=Step::RestorePath;
            break;
        case Step::RestorePath:
            if(Send("Archive file",A::SetValue,archive.string()))step=Step::RestorePassword;
            break;
        case Step::RestorePassword:
            if(Send("Archive password",A::SetValue,password))step=Step::Inspect;
            break;
        case Step::Inspect:
            if(Send("Inspect archive",A::Press))step=Step::Preview;
            break;
        case Step::Preview:
            if(!panel.inspected || !Bounds(f))break;
            Check(panel.password.front()==0 && state.documents.Active()->name=="Current tab before restore"
                && host.recovery.HistoryMetadata(wallet_scope).value->freezes.empty(),"inspect grants no metadata effect");
            Capture(f,"metadata-restore-preview",{"Wallet metadata restore","Vault local display","Labels 2 · Freezes 1","Apply inspected metadata"});
            if(Send("Apply inspected metadata",A::Press))step=Step::Restored;
            break;
        case Step::Restored:
            if(panel.restored_name.empty() || !Bounds(f))break;
            {
                const auto updated=host.recovery.HistoryMetadata(wallet_scope);
                Check(updated && updated.value->revision==3 && updated.value->display_name=="Vault local display"
                    && updated.value->labels.size()==2 && updated.value->freezes.size()==1
                    && state.documents.Active()->name=="Vault local display"
                    && state.documents.Active()->sections[1].note.draft()=="Independent workspace draft",
                    "committed exact metadata revision and preserved unrelated wallet draft");
            }
            Capture(f,"metadata-restore",{"Wallet metadata restore","Local wallet metadata restored","Vault local display","Labels 2 · Freezes 1"});
            if(Send("Back to Recovery",A::Press))step=Step::CloseRecovery;
            break;
        case Step::CloseRecovery:
            if(panel.mode==sh::MetadataBackupMode::None && Send("Close",A::Press))step=Step::Complete;
            break;
        case Step::Complete:
            if(state.recovery_dialog.open)break;
            Check(panel.mode==sh::MetadataBackupMode::None && panel.password.front()==0 && panel.path.front()==0,"Close retires archive inputs");
            done=true;window.RequestClose();
            break;
        }
    }
    void Teardown(const p::NativeTeardown& t)override
    {
        cleaned=t.renderer_cleared && t.platform_cleared && t.textures_released && t.imgui_cleared && t.context_cleared && t.input_cleared;
        Check(cleaned,"native teardown settled");
    }
};
}

int main(int argc,char** argv)
{
    if(argc!=4)return 2;
    try{
        auto* display=std::getenv("DISPLAY");
        auto* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        if(!display || !owned || std::string_view(display)!=owned)throw std::runtime_error("owned native display required");
        const fs::path root=argv[3];
        const auto path=root/"metadata.sqlite";
        const NetworkIdentity network{*Hash256::Parse("0f9188f13cb7b2c71f2a335e3a4fc328bf5beb436012afca590b1a11466e2206")};
        const a::WalletBinding wallet{*BackendSessionId::Make("metadata-fixture-origin"),*WalletId::Make("wallet-fixture"),network};
        {
            auto store=storage::MetadataStore::Open(path);
            storage::Metadata value;
            value.display_name="Vault local display";
            value.labels={{{storage::TargetKind::Address,"fixture-public-receiver"},"Receiver label"},
                          {{storage::TargetKind::Transaction,std::string(64,'a')},"Transaction label"}};
            value.freezes={{storage::TargetKind::Output,std::string(64,'b')+":0"}};
            if(!store || !(*store.value)->Save(a::HistoryMetadataScope(wallet),value))
                throw std::runtime_error("seed owned wallet metadata");
        }
        a::RecoverySession recovery(path);
        if(!recovery.Start())throw std::runtime_error("start owned metadata workspace");
        std::ifstream credential(root/"archive-password");
        std::string password;
        if(!std::getline(credential,password) || password.size()<12)throw std::runtime_error("owned archive password unavailable");
        Observer observer(recovery,argv[1],root,std::move(password),wallet);
        fs::create_directories(observer.output);
        p::NativeOptions options{960,640,false,1.5F,argv[2]};
        options.fixed_delta_seconds=1.F/60;
        options.accessibility=&observer.state.presentation.exchange;
        p::NativeHost* current{};
        {
            p::NativeHost window(options,&observer,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){
                observer.facts=p::DrawShellFrame(*current,observer.state,observer.host,theme,appearance);
            });
            current=&window;window.Run();
        }
        observer.Check(observer.done && observer.cleaned && observer.captures==3,"complete actual native recovery journey");
        std::ofstream(observer.output/"result.json")<<"{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"assertions\":"
            <<observer.checks<<",\"captures\":3,\"teardown\":true}\n";
        return 0;
    }catch(const std::exception& error){
        std::cerr<<error.what()<<'\n';return 1;
    }
}
