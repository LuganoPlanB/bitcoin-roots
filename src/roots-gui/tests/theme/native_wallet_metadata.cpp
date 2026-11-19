// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include "app/history_workspace.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#undef None
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
namespace ui_shell=ui::shell;
namespace {
int assertions{};
void Check(bool ok,const char* detail)
{
    ++assertions;
    if(!ok)throw std::runtime_error(detail);
}
struct Requests final: app::Requests {
    app::RecoverySession& recovery;
    unsigned forwarded{};
    explicit Requests(app::RecoverySession& session):recovery(session){}
    app::RecoverySession* Recovery() override {return &recovery;}
    app::Receipt Submit(const app::Request&) override {++forwarded;return {false,"No daemon in this journey."};}
};
struct RawConnection {
    sqlite3* db{};
    ~RawConnection(){if(db)sqlite3_close(db);}
    void Begin(const std::filesystem::path& path){
        Check(sqlite3_open_v2(path.c_str(),&db,SQLITE_OPEN_READWRITE,nullptr)==SQLITE_OK,"separate writer opened");
        Check(sqlite3_exec(db,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)==SQLITE_OK,"second writer held before Apply");
    }
    void End(){Check(sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr)==SQLITE_OK,"held writer released");}
};
struct Observer final: platform::NativeObserver {
    ui_shell::State state;
    ui_shell::Facts facts;
    Requests requests;
    RawConnection contention;
    std::filesystem::path path;
    std::string pre_rename_snapshot;
    Display* display;
    void* xtst;
    int (*motion)(Display*,int,int,int,unsigned long){};
    int (*button)(Display*,unsigned int,Bool,unsigned long){};
    int stage{};
    bool settled{};
    const std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    Observer(app::RecoverySession& recovery,std::filesystem::path p,Display* d,void* x):
        requests(recovery),path(std::move(p)),display(d),xtst(x)
    {
        motion=reinterpret_cast<decltype(motion)>(dlsym(xtst,"XTestFakeMotionEvent"));
        button=reinterpret_cast<decltype(button)>(dlsym(xtst,"XTestFakeButtonEvent"));
        Check(motion && button,"owned native pointer provider");
    }
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override
    {
        if(std::chrono::steady_clock::now()-start>std::chrono::seconds(15))
            throw std::runtime_error("native wallet metadata deadline at stage "+std::to_string(stage));
    }
    bool Press(std::string_view name)
    {
        const auto packet=state.presentation.exchange.Published();
        if(!packet || !packet->valid)return false;
        namespace ax=ui::components::ax;
        const auto item=std::ranges::find_if(packet->nodes,[&](const auto& node){
            return node.name==name && !node.state.disabled && ax::Supports(node.actions,ax::SemanticAction::Press);
        });
        if(item==packet->nodes.end())return false;
        return state.presentation.exchange.QueueNext(packet->generation,item->id,ax::SemanticAction::Press).second
            ==ui::components::ActionResult::Queued;
    }
    void OpenTabMenu(platform::NativeHost& host,bool down)
    {
        int x{},y{};
        glfwGetWindowPos(host.Window(),&x,&y);
        const auto& rect=facts.tabs.at(1).rect;
        Check(motion(display,-1,x+int((rect[0]+rect[2])/2),y+int((rect[1]+rect[3])/2),0)
            && button(display,3,down,0),"native right click on exact wallet tab");
        XSync(display,False);
    }
    storage::Metadata Local(uint64_t id)
    {
        const auto value=requests.recovery.HistoryMetadata(app::HistoryMetadataScope(*state.documents.Find(id)->binding));
        Check(bool(value),"read acknowledged local metadata");
        return *value.value;
    }
    void ReadPreRenameSnapshot()
    {
        auto store=storage::MetadataStore::Open(path);
        Check(bool(store),"open independent reader for the pre-rename snapshot");
        const auto record=(*store.value)->LoadRecovery();
        Check(record && record.value->enabled,"load the original public recovery payload");
        const auto snapshot=app::DecodePublicSession(record.value->public_documents);
        const auto* original=snapshot?snapshot->documents.Find(1):nullptr;
        Check(original && original->name==original->original_name && original->icon==0,
            "the pre-rename public payload has the original presentation");
        pre_rename_snapshot=record.value->public_documents;
    }
    void CheckCustomRestart()
    {
        // Model a restart between the wallet-metadata commit and the public
        // checkpoint, while letting the live fixture continue independently.
        const auto copy=path.parent_path()/"pre-rename-checkpoint.sqlite";
        Check(!std::filesystem::exists(path.string()+"-journal") && !std::filesystem::exists(path.string()+"-wal")
            && std::filesystem::copy_file(path,copy),"clone the settled database after rename");
        {
            auto store=storage::MetadataStore::Open(copy);
            Check(bool(store),"open the cloned post-rename store");
            auto record=(*store.value)->LoadRecovery();
            Check(record && record.value->enabled,"load the recovery revision in the clone");
            record.value->public_documents=pre_rename_snapshot;
            Check(bool((*store.value)->SaveRecovery(*record.value)),"restore the older payload at the cloned revision");
        }
        app::RecoverySession restarted(copy);
        app::DocumentRegistry restored;
        Check(restarted.Start() && restarted.Restore(restored),"restore the stale checkpoint through a fresh session");
        const auto* wallet=restored.Find(1);
        Check(wallet && wallet->name=="Local display name" && wallet->icon==2
            && wallet->sections[1].note.draft()=="Retain this public draft",
            "persisted custom name and icon supersede the stale presentation and retain its draft");
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override
    {
        if(frame.rendered_frames<5)return;
        auto& wallet=*state.documents.Find(1);
        switch(stage){
        case 0:
            ReadPreRenameSnapshot();
            ui_shell::BeginRename(state,1);
            state.actions.name.SetDraft("Local display name");
            state.actions.icon=2;
            contention.Begin(path);
            break;
        case 1: if(!Press("Apply Name / Icon"))return; break;
        case 2:
            if(state.actions.error.empty())return;
            Check(wallet.name==wallet.original_name && Local(1).display_name.empty()
                && state.actions.name.draft()=="Local display name","failed save retains the acknowledged wallet and pending edit");
            contention.End();
            break;
        case 3: if(!Press("Apply Name / Icon"))return; break;
        case 4:
            if(state.actions.kind!=ui_shell::DocumentDialog::None)return;
            Check(wallet.name=="Local display name" && wallet.icon==2 && Local(1).display_icon==2
                && Local(1).display_name==wallet.name,"rename and icon persist before view acknowledgement");
            CheckCustomRestart();
            OpenTabMenu(host,true);
            break;
        case 5: OpenTabMenu(host,false); break;
        case 6: if(!Press("Reset Name / Icon"))return; break;
        case 7:
            Check(wallet.name==wallet.original_name && wallet.icon==0 && Local(1).display_icon==0,"native tab reset durably restores default");
            ui_shell::BeginDelete(state,1);
            break;
        case 8: if(!Press("Cancel"))return; break;
        case 9:
            if(state.actions.kind!=ui_shell::DocumentDialog::None)return;
            Check(Local(1).labels.size()==1 && Local(1).freezes.size()==1,"Cancel leaves scoped metadata intact");
            ui_shell::BeginDelete(state,1);
            break;
        case 10: if(!Press("Delete Local Metadata"))return; break;
        case 11:
            if(state.actions.kind!=ui_shell::DocumentDialog::None)return;
            Check(Local(1).display_name.empty() && Local(1).labels.empty() && Local(1).layouts.empty()
                && Local(1).widths.empty() && Local(1).freezes.empty() && Local(1).drafts.size()==1,"confirmed deletion clears only scoped metadata");
            Check(Local(2).labels.size()==1 && state.documents.Documents().size()==3
                && wallet.sections[1].note.draft()=="Retain this public draft","other wallets, every tab and public draft remain intact");
            Check(requests.forwarded==0,"no command was forwarded to any daemon interface");
            host.RequestClose();
            break;
        default: return;
        }
        ++stage;
    }
    void Teardown(const platform::NativeTeardown& teardown) override
    {
        settled=teardown.renderer_cleared && teardown.context_cleared && teardown.input_cleared;
    }
};
}

int main(int argc,char** argv)
{
    if(argc!=4)return 2;
    const std::filesystem::path output=argv[1], path=argv[3];
    std::filesystem::create_directories(output);
    try{
        const auto* display_name=std::getenv("DISPLAY");
        const auto* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        Check(display_name && owned && std::string_view(display_name)==owned,"owned X11 fixture required");
        auto* display=XOpenDisplay(nullptr);
        auto* xtst=dlopen("libXtst.so.6",RTLD_NOW);
        struct ReleaseDisplay{Display* display;void* xtst;~ReleaseDisplay(){if(display)XCloseDisplay(display);if(xtst)dlclose(xtst);}} release{display,xtst};
        Check(display && xtst,"owned X11 provider ready");
        auto documents=app::PublicFixtureDocuments();
        const auto wallet_one=app::HistoryMetadataScope(*documents.Find(1)->binding);
        const auto wallet_two=app::HistoryMetadataScope(*documents.Find(2)->binding);
        const auto hash=*Hash256::Parse(std::string(64,'7'));
        {
            auto store=storage::MetadataStore::Open(path);
            Check(bool(store),"seed independent SQLite metadata");
            storage::Metadata metadata;
            metadata.labels.push_back({{storage::TargetKind::Address,"fixture-address"},"saved label"});
            metadata.freezes.push_back({storage::TargetKind::Address,"fixture-address"});
            metadata.layouts.push_back({"coins","value",false});
            metadata.widths.push_back({"coins","value",100});
            metadata.drafts.push_back({"public-draft",{1},"saved draft",Money::Satoshis(7)});
            Check(bool((*store.value)->Save(wallet_one,metadata)) && bool((*store.value)->Save(wallet_two,metadata)),
                "two independently scoped stores");
            Check(bool((*store.value)->RecordBroadcastAttempt({wallet_one.network,hash,hash,hash,1})),
                "seed separate broadcast exclusion record");
        }
        {
            app::RecoverySession recovery(path);
            Check(recovery.Start(),"production recovery opened");
            Observer observer(recovery,path,display,xtst);
            observer.state.documents=std::move(documents);
            observer.state.documents.Find(1)->sections[1].note.SetDraft("Retain this public draft");
            Check(recovery.Enable(observer.state.documents),"opt in to persistent workspace recovery");
            platform::NativeOptions options{960,640,false,1.5F,argv[2]};
            options.accessibility=&observer.state.presentation.exchange;
            platform::NativeHost* current{};
            {
                platform::NativeHost host(options,&observer,[&](const auto& theme,const auto& appearance){
                    observer.facts=platform::DrawShellFrame(*current,observer.state,observer.requests,theme,appearance);
                });
                current=&host;
                host.Run();
            }
            Check(observer.stage==12 && observer.settled,"production adapter journey and teardown completed");
        }
        app::RecoverySession restarted(path);
        app::DocumentRegistry restored;
        Check(restarted.Start() && restarted.Restore(restored),"restore after reopening the SQLite workspace");
        const auto got=restarted.HistoryMetadata(wallet_one);
        const auto other=restarted.HistoryMetadata(wallet_two);
        const auto attempt=restarted.HasBroadcastAttempt(wallet_one.network,hash);
        Check(got && got.value->display_name.empty() && got.value->display_icon==0 && got.value->labels.empty()
            && other && other.value->labels.size()==1 && attempt && *attempt.value,"restart retains exact deletion, other wallet and journal");
        Check(restored.Find(1)->name==restored.Find(1)->original_name && restored.Find(1)->icon==0
            && restored.Find(1)->sections[1].note.draft()=="Retain this public draft","saved public draft and default presentation reopen together");
        std::ofstream(output/"result.json")<<"{\"selected\":1,\"executed\":1,\"passed\":1,\"failed\":0,\"skipped\":0,\"assertions\":"<<assertions<<"}\n";
        return 0;
    }catch(const std::exception& e){
        std::cerr<<e.what()<<'\n';
        return 1;
    }
}
