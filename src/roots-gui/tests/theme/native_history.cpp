// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/shell_composition.h"
#include "app/history_workspace.h"
#include "app/connection_observations.h"
#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <dlfcn.h>
#include <fstream>
#include <iostream>
#include <atomic>
#include <algorithm>
using namespace roots::gui;namespace sh=ui::shell;namespace a=app;
namespace {
struct Requests final:a::Requests {std::atomic<bool> states{};a::RecoverySession* recovery{};rpc::CookieCredentials credentials;rpc::Transport transport{credentials};RefreshService refresh{[this](const auto& r,std::stop_token s){auto request=r;if(states)request.history_limits.transactions=std::max(size_t(1),r.history_limits.transactions/256);return FetchRefresh(transport,request,s);}};ConnectionService connection{[this](const auto& c,std::stop_token s){return ProbeCapabilities(transport,c,s);},[this](bool b){refresh.SetOffline(b,RefreshMonotonicMs());}};ConnectionService* Connection() override{return &connection;}RefreshService* Refresh() override{return &refresh;}a::RecoverySession* Recovery() override{return recovery;}a::HostObservations Observe() const override{return a::ConnectionObservations(connection,refresh);}a::Availability CanSubmit(const a::Request&) const override{return {false,"No daemon mutation port"};}a::Receipt Submit(const a::Request&) override{throw std::runtime_error("unexpected host mutation");}};
struct Observer final:platform::NativeObserver {
    Requests requests;sh::State state;sh::Facts facts;std::filesystem::path out;ServerCandidate empty;int64_t expected{};Display* display{};void* xtst{};int(*motion)(Display*,int,int,int,unsigned long){};int(*button)(Display*,unsigned int,Bool,unsigned long){};int(*key)(Display*,unsigned int,Bool,unsigned long){};int checks{},base{},empty_stage{};bool states{},done{},cleaned{};int state_stage{},click_frame{},scroll_reveal_frames{};int64_t stale_date{};std::string selected,capacity_selected;std::vector<uint64_t> capacity_fillers;std::map<uint64_t,std::string> capacity_notes;const std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
    void Check(bool b,const char* label){++checks;if(!b)throw std::runtime_error(label);}
    bool CaptureFramebuffer() const override{return true;}
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override{if(std::chrono::steady_clock::now()-started>std::chrono::seconds(25))throw std::runtime_error("native history deadline");}
    void Key(KeySym sym,bool down){Check(key(display,XKeysymToKeycode(display,sym),down,0),"native history key");XSync(display,False);}
    void Click(platform::NativeHost& host,const std::string& name,bool down){const auto& item=facts.history.at(name);const auto& r=item.rect;Check(item.visible && !item.disabled && r[0]>=0 && r[1]>=0 && r[2]<=960 && r[3]<=640,("native history action reachable at 150 percent: "+name).c_str());int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);Check(motion(display,-1,x+int((r[0]+r[2])/2),y+int((r[1]+r[3])/2),0),"native history pointer");Check(button(display,1,down,0),"native history click");XSync(display,False);}
    bool RevealLabel(platform::NativeHost& host,const std::string& name)
    {
        const auto item=facts.history.find(name);
        if(item==facts.history.end())return false;
        for(const auto& grid:state.presentation.Tree().nodes){
            if(grid.name!="Wallet transactions" || grid.role!=ui::components::ax::SemanticRole::Grid)continue;
            const auto& r=item->second.rect;
            if(item->second.visible && r[0]>=grid.minimum.x && r[2]<=grid.maximum.x
                    && r[1]>=grid.minimum.y && r[3]<=grid.maximum.y)return true;
            int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);
            if(facts.work.history_drawn && (++scroll_reveal_frames%6!=1))return false;
            if(scroll_reveal_frames%60==1){
                std::ofstream geometry(out/"selected-label-reveal.txt",std::ios::app);
                geometry<<r[0]<<' '<<r[1]<<' '<<r[2]<<' '<<r[3]<<" clip "
                        <<grid.minimum.x<<' '<<grid.minimum.y<<' '<<grid.maximum.x<<' '<<grid.maximum.y
                        <<" showing "<<item->second.visible<<'\n';
            }
            const unsigned wheel=r[1]<grid.minimum.y?4:5;
            Check(motion(display,-1,x+int((grid.minimum.x+grid.maximum.x)/2),
                         y+int((grid.minimum.y+grid.maximum.y)/2),0)
                    && button(display,wheel,True,0) && button(display,wheel,False,0),
                  "native history table scroll to the selected label");
            XSync(display,False);
            return false;
        }
        return false;
    }
    void Capture(const platform::NativeFrame& f,const char* name){std::ofstream file(out/(std::string(name)+".ppm"),std::ios::binary);file<<"P6\n"<<f.framebuffer_width<<' '<<f.framebuffer_height<<"\n255\n";for(int y=f.framebuffer_height-1;y>=0;--y)for(int x=0;x<f.framebuffer_width;++x)file.write(reinterpret_cast<const char*>(f.rgba.data()+(size_t(y)*f.framebuffer_width+x)*4),3);Check(bool(file),"native history capture retained");Check(state.presentation.Tree().Validate() && !state.presentation.Tree().overflow,"history accessibility tree valid and bounded");std::ofstream(out/(std::string(name)+".semantics.txt"))<<state.presentation.Snapshot();}
    bool VisibleRow(const char* stage){
        std::ofstream geometry(out/(std::string(stage)+"-row-geometry.txt"));
        // A SpanAllColumns selectable exceeds its first column's clip rect.
        // Test its vertical bounds against the actual owned GRID viewport,
        // not ItemFacts.visible (which intentionally uses that column clip).
        for(const auto& grid:state.presentation.Tree().nodes)if(grid.name=="Wallet transactions" && grid.role==ui::components::ax::SemanticRole::Grid){geometry<<"grid "<<grid.minimum.x<<' '<<grid.minimum.y<<' '<<grid.maximum.x<<' '<<grid.maximum.y<<'\n';bool visible=false;for(const auto& [name,item]:facts.history)if(name.starts_with("row-")){geometry<<"row "<<item.rect[0]<<' '<<item.rect[1]<<' '<<item.rect[2]<<' '<<item.rect[3]<<" columnVisible="<<item.visible<<'\n';visible|=item.rect[1]>=grid.minimum.y && item.rect[3]<=grid.maximum.y && item.rect[2]>grid.minimum.x && item.rect[0]<grid.maximum.x;}return visible;}return false;
    }
    void StateFrame(platform::NativeHost& host,const platform::NativeFrame& f){
        auto* doc=state.documents.Find(1);const auto view=doc?a::HistoryForDocument(*doc,requests.refresh,{}):std::nullopt;if(!view || !view->ledger->Observed())return;
        if(state_stage==0){if(!view->ledger->Limited() || view->busy)return;Check(f.width==960 && f.height==640 && f.effective_scale==1.5F && !f.gl_error,"actual native state journey 960x640/150 percent");Check(view->ledger->Rows().size()==1 && !view->ledger->Complete(),"actual bounded first parent is partial");Check(state.presentation.Snapshot().find("Partial scan")!=std::string::npos,"native partial reason visible and accessible");Check(VisibleRow("partial"),"partial history retains a visible native transaction row");doc->history.selected=view->ledger->Rows().begin()->first;Capture(f,"history-partial-scan");stale_date=view->ledger->Observed();Click(host,"scan-earlier",true);click_frame=int(f.rendered_frames);state_stage=1;return;}
        if(state_stage==1 && int(f.rendered_frames)>click_frame){Click(host,"scan-earlier",false);state_stage=2;return;}
        if(state_stage==2){if(view->busy || view->scan_budget<512 || view->ledger->Observed()==stale_date)return;Check(view->ledger->Rows().size()==2 && view->ledger->Limited(),"explicit continuation makes bounded actual RPC progress");Check(!requests.refresh.AcknowledgeHistory(*view->identity,requests.refresh.PendingHistorySequence(),requests.refresh.View().history->lastblock),"partial cursor cannot be acknowledged");Click(host,"scan-earlier",true);click_frame=int(f.rendered_frames);state_stage=3;return;}
        if(state_stage==3 && int(f.rendered_frames)>click_frame){Click(host,"scan-earlier",false);int x{},y{};glfwGetWindowPos(host.Window(),&x,&y);Check(motion(display,-1,x+930,y+80,0),"move pointer clear of native state provenance");XSync(display,False);state_stage=4;return;}
        if(state_stage==4){if(view->busy || !view->ledger->Complete())return;Check(view->ledger->Rows().size()==3 && view->ledger->Totals().total->sats()==expected && view->chain_current,"larger explicit budget reconciles actual daemon ledger");Check(facts.history.at("scan-earlier").disabled,"completed history has no invented earlier balance");Capture(f,"history-scan-complete");stale_date=view->ledger->Observed();std::ofstream(out/"disconnect")<<"owned proxy disconnect listsinceblock only\n";Check(requests.refresh.RequestNow(RefreshMonotonicMs()),"explicit refresh before owned transport disconnect");state_stage=5;return;}
        if(state_stage==5){if(view->busy || view->freshness[2]!=Freshness::Stale)return;Check(view->ledger->Observed()==stale_date && view->ledger->Rows().size()==3 && !view->chain_current,"actual disconnect retains dated rows, no old current success");Check(state.presentation.Snapshot().find("Refresh failed")!=std::string::npos && state.presentation.Snapshot().find("Current chain state Unknown")!=std::string::npos,"native stale provenance and chain uncertainty visible");Check(VisibleRow("stale"),"stale history retains a visible native transaction row");Capture(f,"history-stale");std::filesystem::remove(out/"disconnect");Check(requests.refresh.RequestNow(RefreshMonotonicMs()),"deliberate reconnect refresh");state_stage=6;return;}
        if(state_stage==6){if(view->busy || !view->chain_current)return;Check(view->ledger->Complete() && view->ledger->Rows().size()==3,"reconnect recovers current verified state without duplicate rows");Capture(f,"history-reconnected");done=true;host.RequestClose();}
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& f) override {
        if(states){StateFrame(host,f);return;}
        auto* doc=state.documents.Find(1);auto view=doc?a::HistoryForDocument(*doc,requests.refresh,{}):std::nullopt;
        if(!base){if(!view || !view->ledger->Complete() || view->ledger->Rows().size()<3 || f.rendered_frames<4)return;Check(state.documents.Documents().size()==1 && doc->history.enabled,"actual production authenticated registration");Check(view->ledger->Totals().total && view->ledger->Totals().total->sats()==expected,"production ledger agrees with actual daemon wallet");for(const auto& [id,tx]:view->ledger->Rows())Check(tx.decoded_available && !tx.io.empty(),"actual proven verbose I/O normalization");Check(f.decorated && f.font_texture && !f.gl_error && f.width==960 && f.height==640 && f.effective_scale==1.5F,"actual native 960x640 150 percent renderer");Capture(f,"transactions-populated");base=int(f.rendered_frames);return;}
        const int n=int(f.rendered_frames)-base;
        if(n==1 || n==2)Click(host,"chart-next",n==1);
        if(n==5){selected=doc->history.selected;Check(!selected.empty(),"chart selects stable transaction");Capture(f,"history-chart");Click(host,"expand-"+selected,true);}if(n==6)Click(host,"expand-"+selected,false);
        if(n==10){Check(doc->history.expanded.contains(selected),"native expansion persists");if(!RevealLabel(host,"label-"+selected)){base=int(f.rendered_frames)-9;return;}Click(host,"label-"+selected,true);}if(n==11)Click(host,"label-"+selected,false);
        if(n==12){Key(XK_Control_L,true);Key(XK_a,true);}if(n==13){Key(XK_a,false);Key(XK_Control_L,false);}
        constexpr std::string_view text="native";if(n>=14 && n<26){const auto at=(n-14)/2;Key(KeySym(text[size_t(at)]),(n-14)%2==0);}if(n==27 || n==28)Click(host,"filter",n==27);
        if(n==32){const auto& draft=doc->history.drafts.at(selected);auto stored=requests.recovery->HistoryMetadata(a::HistoryMetadataScope(*doc->binding));Check(stored && stored.value->labels.size()==1 && stored.value->labels[0].text==text && draft.saved==text && draft.draft==text,"native focus loss without Enter acknowledges scoped metadata");Capture(f,"label-focus-saved");}
        if(n==34){auto loaded=requests.recovery->HistoryMetadata(a::HistoryMetadataScope(*doc->binding));Check(bool(requests.recovery->SaveHistoryLabel(a::HistoryMetadataScope(*doc->binding),*Hash256::Parse(selected),"Other instance",loaded.value->revision)),"controlled competing label revision");Click(host,"label-"+selected,true);}if(n==35)Click(host,"label-"+selected,false);
        if(n==36){Key(XK_Control_L,true);Key(XK_a,true);}if(n==37){Key(XK_a,false);Key(XK_Control_L,false);}
        constexpr std::string_view retained="retain";if(n>=38 && n<50){const auto at=(n-38)/2;Key(KeySym(retained[size_t(at)]),(n-38)%2==0);}if(n==51 || n==52)Click(host,"filter",n==51);
        if(n==56){const auto& draft=doc->history.drafts.at(selected);auto stored=requests.recovery->HistoryMetadata(a::HistoryMetadataScope(*doc->binding));Capture(f,"label-conflict");Check(draft.draft==retained && !draft.error.empty() && draft.saved==text && stored && stored.value->labels[0].text=="Other instance","native failed focus-loss acknowledgement retains draft and previous saved label");}
        if(n==58)Key(XK_Escape,true);if(n==59)Key(XK_Escape,false);
        if(n==60 || n==68){Key(XK_Control_L,true);Key(XK_Shift_L,true);Key(XK_h,true);}if(n==61 || n==69){Key(XK_h,false);Key(XK_Shift_L,false);Key(XK_Control_L,false);}
        if(n==65){Check(state.privacy.hidden && state.presentation.ChartSamples().empty() && facts.history.at("csv").disabled && facts.history.at("copy-value").disabled,"native amount privacy disables chart/export/copy");Check(state.presentation.Snapshot().find("retain")==std::string::npos && state.presentation.Snapshot().find("Other instance")==std::string::npos,"native hidden financial free text absent from accessibility");Capture(f,"history-hidden");}
        if(n==73 || n==74)Click(host,"open",n==73);
        if(n==78){Check(state.documents.Documents().size()==2 && state.documents.Active()->observed_transaction->txid.hex()==selected,"native open focuses read-only transaction reference");Capture(f,"history-transaction-document");Click(host,"show-wallet",true);}if(n==79)Click(host,"show-wallet",false);
        if(n==84)Check(state.documents.ActiveId()==1 && doc->history.selected==selected,"native transaction return preserves selection");
        if(n==86){while(state.documents.Documents().size()<32){const auto id=state.documents.OpenTransaction("Capacity draft "+std::to_string(state.documents.Documents().size()));state.documents.Find(id)->transaction.note.SetDraft("Retained capacity work "+std::to_string(id));capacity_fillers.push_back(id);}state.documents.Activate(1);for(const auto& item:state.documents.Documents())capacity_notes[item.id]=std::string(item.Current().note.draft());}
        if(n==88 || n==89)Click(host,"open",n==88);
        if(n==92){Check(state.documents.Documents().size()==32 && state.documents.Active()->observed_transaction && state.documents.Active()->observed_transaction->txid.hex()==selected,"native existing reference focuses at capacity");Click(host,"show-wallet",true);}if(n==93)Click(host,"show-wallet",false);
        if(n==96 || n==97)Click(host,"chart-next",n==96);
        if(n==100){capacity_selected=state.documents.Find(1)->history.selected;Check(capacity_selected!=selected,"native new reference selected at capacity");Click(host,"open",true);}if(n==101)Click(host,"open",false);
        if(n==104){Check(state.documents.Documents().size()==32 && state.documents.ActiveId()==1 && state.documents.Find(1)->history.selected==capacity_selected,"native new reference limit retains workspace and selection");for(const auto& item:state.documents.Documents())Check(capacity_notes.at(item.id)==item.Current().note.draft(),"native capacity failure retains each public draft");Check(state.presentation.Snapshot().find("Close a tab")!=std::string::npos,"native capacity failure shows actionable accessible reason");Capture(f,"document-limit");}
        if(n==108){Check(state.documents.Close(capacity_fillers) && state.documents.Documents().size()==2,"native fixture closes only its temporary view drafts");}
        if(n==112){Check(requests.connection.SetDraft(empty) && requests.connection.Test(),"explicit independent empty wallet connection test");empty_stage=1;}
        if(empty_stage==1 && !requests.connection.Busy() && requests.connection.CanApply()){Check(requests.connection.Apply(),"explicit existing empty wallet profile apply");empty_stage=2;}
        if(empty_stage==2 && state.documents.ActiveId()!=1 && state.documents.Active()->kind==a::DocumentKind::Wallet){const auto empty_view=a::HistoryForDocument(*state.documents.Active(),requests.refresh,{});if(empty_view && empty_view->ledger->Observed() && empty_view->ledger->Rows().empty()){Check(state.documents.Documents().size()==3 && state.documents.Find(1)->history.selected==capacity_selected,"production wallets remain independent across profile switch");Capture(f,"transactions-empty");done=true;host.RequestClose();}}
    }
    void Teardown(const platform::NativeTeardown& t) override{cleaned=t.renderer_cleared && t.platform_cleared && t.textures_released && t.imgui_cleared && t.context_cleared;Check(cleaned,"native resources settled");}
};
}
int main(int argc,char** argv){if(argc!=10 && argc!=11)return 2;Observer o;o.states=argc==11;o.requests.states=o.states;o.out=argv[1];std::filesystem::create_directories(o.out);int result=1;try{const char* display=getenv("DISPLAY"),*owned=getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");if(!display || !owned || std::string(display)!=owned)throw std::runtime_error("owned display required");a::RecoverySession recovery(argv[3]);o.requests.recovery=&recovery;recovery.Start();ServerCandidate candidate;candidate.connection.port=uint16_t(std::stoi(argv[4]));candidate.connection.credential.reference=argv[5];candidate.expected_genesis=argv[6];candidate.wallet=argv[7];o.empty=candidate;o.empty.wallet=argv[8];o.expected=std::stoll(argv[9]);auto& c=o.requests.connection;o.Check(c.SetDraft(candidate) && c.Test(),"production connection test starts");const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(c.Busy() && std::chrono::steady_clock::now()<deadline){c.Poll();std::this_thread::yield();}o.Check(c.Apply(),"authenticated actual wallet profile applied");o.display=XOpenDisplay(nullptr);o.xtst=dlopen("libXtst.so.6",RTLD_NOW);if(!o.display || !o.xtst)throw std::runtime_error("native input unavailable");o.motion=reinterpret_cast<decltype(o.motion)>(dlsym(o.xtst,"XTestFakeMotionEvent"));o.button=reinterpret_cast<decltype(o.button)>(dlsym(o.xtst,"XTestFakeButtonEvent"));o.key=reinterpret_cast<decltype(o.key)>(dlsym(o.xtst,"XTestFakeKeyEvent"));if(!o.motion || !o.button || !o.key)throw std::runtime_error("XTest unavailable");platform::NativeHost* current{};{platform::NativeOptions options{960,640,false,1.5F,argv[2]};options.fixed_delta_seconds=1.F/60;platform::NativeHost host(options,&o,[&](const imkit::Theme& theme,const ui::BootstrapAppearance& appearance){o.facts=platform::DrawShellFrame(*current,o.state,o.requests,theme,appearance);});current=&host;host.Run();}o.Check(o.done && o.cleaned && !c.Busy() && !o.requests.refresh.Busy(),"history native journey completed and workers settled");result=0;}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';}if(o.display)XCloseDisplay(o.display);if(o.xtst)dlclose(o.xtst);std::ofstream(o.out/"result.json")<<"{\"selected\":1,\"executed\":1,\"passed\":"<<!result<<",\"failed\":"<<result<<",\"skipped\":0,\"checks\":"<<o.checks<<",\"teardown\":"<<o.cleaned<<"}\n";return result;}
