// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/rpc/json.h"
#include "platform/shell_composition.h"
#include "tests/engine/recovery_fixture.h"
#include "tests/wallet_tools_fixture.h"

#include <GLFW/glfw3.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <sstream>
#include <atomic>
#include <dlfcn.h>
#include <fstream>
#include <iostream>

namespace roots::gui {
namespace {
namespace a = app;
namespace s = ui::shell;
namespace c = ui::components;
namespace p = protocol;
namespace fx = test::wallet_tools;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

void Require(bool good, std::string_view message)
{
    if (!good) throw std::runtime_error(std::string(message));
}

int64_t Ns(Clock::time_point begin, Clock::time_point end = Clock::now())
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
}

Hash256 IndexedHash(size_t n)
{
    std::array<char, 65> hex{};
    std::snprintf(hex.data(), hex.size(), "%064zx", n + 1);
    return *Hash256::Parse(hex.data());
}

int Admission(const char* port,const char* cookie)
{
    using J=rpc::Json;
    const auto owner=std::this_thread::get_id();
    std::exception_ptr error;
    std::ostringstream output;
    {
        std::jthread worker([&]{
            try {
                Require(std::this_thread::get_id()!=owner,"admission on a worker thread");
                const auto start=Clock::now();
                ServerCandidate profile;
                profile.connection.port=uint16_t(std::stoi(port));
                profile.connection.credential.reference=cookie;
                profile.expected_genesis=fx::REGTEST;
                profile.wallet="large-wallet";
                const RequestContext context{*BackendSessionId::Make("large-wallet"),
                    {*Hash256::Parse(profile.expected_genesis)},*WalletId::Make("large-wallet"),
                    *DocumentId::Make("wallet-document-1"),{1},{1}};
                rpc::CookieCredentials credentials;
                rpc::Transport transport(credentials);
                const auto tip=fx::Hash('b').hex();
                J rows{J::Kind::Array};
                rows.items.reserve(100000);
                for (size_t n=0;n<100000;++n) {
                    J item{J::Kind::Object};
                    item.members.emplace_back("txid",J{J::Kind::String,IndexedHash(n).hex()});
                    rows.items.push_back(std::move(item));
                }
                J delta{J::Kind::Object};
                delta.members.emplace_back("transactions",std::move(rows));
                delta.members.emplace_back("removed",J{J::Kind::Array});
                delta.members.emplace_back("lastblock",J{J::Kind::String,tip});
                HistoryBalances balances;
                balances.trusted=Money::Satoshis(100000000);
                const auto oversized=FetchHistory(transport,profile,context,delta,balances,tip,true,false,false,true,
                    {},{},Clock::now()+8s);
                Require(!oversized.batch && oversized.error==rpc::Error::Limit,"100k decoded history entries hit real admission bound before RPC");
                delta.members.front().second.items.erase(delta.members.front().second.items.begin()+1,
                    delta.members.front().second.items.end());
                const auto result=FetchHistory(transport,profile,context,delta,balances,tip,true,false,false,true,
                    {},{},Clock::now()+12s);
                Require(result.batch && result.batch->limited && !result.batch->inventory_available
                    && result.batch->coins.size()==8192 && result.batch->transactions.size()==9,
                    "real one-extra-row UTXO cap retains dated subset only");
                output<<"{\"historyDecodedSourceRows\":100000,\"historyOversizedAdmitted\":0,"
                    << "\"historyFixtureAdmitted\":9,\"inventoryResponseRows\":8193,\"inventoryAdmitted\":8192,"
                    << "\"inventoryCurrent\":false,\"workerThread\":true,\"durationNs\":"<<Ns(start)<<"}\n";
            } catch (...) {error=std::current_exception();}
        });
    }
    if (error) std::rethrow_exception(error);
    std::cout<<output.str();
    return 0;
}

struct NativeKeys {
    Display* display{};
    void* xtst{};
    int (*key)(Display*, unsigned, Bool, unsigned long){};
    int (*move)(Display*, int, int, int, unsigned long){};
    int (*button)(Display*, unsigned, Bool, unsigned long){};
    NativeKeys()
    {
        const auto* owned=std::getenv("ROOTS_GUI_OWNED_TEST_DISPLAY");
        const auto* actual=std::getenv("DISPLAY");
        Require(owned && actual && std::string_view(owned)==actual, "owned X11 display");
        display=XOpenDisplay(nullptr);
        xtst=dlopen("libXtst.so.6",RTLD_NOW);
        Require(display && xtst,"owned XTest");
        key=reinterpret_cast<decltype(key)>(dlsym(xtst,"XTestFakeKeyEvent"));
        move=reinterpret_cast<decltype(move)>(dlsym(xtst,"XTestFakeMotionEvent"));
        button=reinterpret_cast<decltype(button)>(dlsym(xtst,"XTestFakeButtonEvent"));
        Require(key && move && button,"native input calls");
    }
    ~NativeKeys() { if (display) XCloseDisplay(display); if (xtst) dlclose(xtst); }
    void Privacy()
    {
        for (auto sym : {XK_Control_L, XK_Shift_L, XK_h}) Require(key(display,XKeysymToKeycode(display,sym),True,0),"native key down");
        for (auto sym : {XK_h, XK_Shift_L, XK_Control_L}) Require(key(display,XKeysymToKeycode(display,sym),False,0),"native key up");
        XFlush(display);
    }
    void Click(platform::NativeHost& host,const c::ItemFacts& item,bool down)
    {
        int x{},y{};
        glfwGetWindowPos(host.Window(),&x,&y);
        Require(move(display,-1,x+int((item.rect[0]+item.rect[2])/2),y+int((item.rect[1]+item.rect[3])/2),0),"native click target");
        Require(button(display,1,down,0),"native button event");
        XFlush(display);
    }
    void Scroll(platform::NativeHost& host, const c::ItemFacts& item)
    {
        int x{},y{};
        glfwGetWindowPos(host.Window(),&x,&y);
        Require(move(display,-1,x+int((item.rect[0]+item.rect[2])/2),y+int((item.rect[1]+item.rect[3])/2),0),"native scroll target");
        for (int n=0;n<6;++n) Require(button(display,5,True,0) && button(display,5,False,0),"native wheel down");
        XFlush(display);
    }
};

struct Source {
    static constexpr size_t Count = 100000;
    std::vector<HistoryTransaction> transactions;
    std::vector<HistoryCoin> coins;
    Source()
    {
        transactions.reserve(Count);
        coins.reserve(Count);
        for (size_t n=0;n<Count;++n) {
            const auto hash=IndexedHash(n);
            HistoryTransaction tx{hash};
            tx.state=HistoryState::Confirmed;
            tx.time=1700000000+int64_t(n);
            tx.confirmations=2;
            tx.wallet_delta=Money::Satoshis(int64_t(n)+1);
            tx.details.push_back({0,"receive","",n%2 ? "Alternating observation" : "Dense public observation"});
            transactions.push_back(std::move(tx));
            coins.push_back({{hash,0},Money::Satoshis(int64_t(n)+1),2,true,true,false,"0014","",""});
        }
    }
};

struct Requests final : a::Requests {
    test::RecoveryFixtureDirectory scratch;
    a::RecoverySession recovery{scratch.Metadata()};
    Source source;
    TransactionDocuments local;
    std::atomic<unsigned> active{}, entered{}, exited{}, cancelled{}, maximum{};
    std::atomic<bool> hold{}, waiting{}, release{}, fail{};
    const std::thread::id owner=std::this_thread::get_id();
    std::atomic<bool> owner_fault{};
    RefreshService refresh{[this](const RefreshRequest& request,std::stop_token stop) {
        if (std::this_thread::get_id()==owner) owner_fault=true;
        auto count=++active;
        maximum=std::max(maximum.load(),count);
        ++entered;
        const auto& ident=request.identity;
        RefreshReply reply{ident,request.generation,request.sequence};
        if (hold) {
            waiting=true;
            const auto limit=Clock::now()+3s;
            while (!release && Clock::now()<limit) std::this_thread::sleep_for(1ms);
            waiting=false;
        }
        if (stop.stop_requested()) ++cancelled;
        if (fail) reply.errors.fill(rpc::Error::Connection);
        else {
            HistoryBatch batch{ident.context,*ident.profile.wallet,fx::Hash('b').hex()};
            const auto changes=std::min(request.history_limits.transactions,source.transactions.size());
            const auto unspent=std::min(request.history_limits.coins,source.coins.size());
            batch.transactions.assign(source.transactions.begin(),source.transactions.begin()+changes);
            batch.coins.assign(source.coins.begin(),source.coins.begin()+unspent);
            batch.balances.trusted=Money::Satoshis(int64_t(Source::Count)*int64_t(Source::Count+1)/2);
            batch.stable=batch.full_query=batch.limited=true;
            batch.height=900;
            reply.node=NodeObservation{batch.height,batch.lastblock};
            reply.wallet=WalletObservation{batch.balances.trusted};
            if (request.fetch_history) reply.history=HistoryDelta{{},{},batch.lastblock,std::move(batch)};
            reply.observed_unix_ms=fx::WHEN+entered.load();
        }
        --active;
        ++exited;
        return reply;
    }};
    Requests() { Require(recovery.Start(), "private metadata store"); }
    a::Receipt Submit(const a::Request&) override { return {false,"Fixture admits no command effects."}; }
    a::HostObservations Observe() const override { return {}; }
    RefreshService* Refresh() override { return &refresh; }
    a::RecoverySession* Recovery() override { return &recovery; }
    TransactionDocuments* LocalDocuments() const override { return const_cast<TransactionDocuments*>(&local); }
};

struct Sample {
    std::string_view phase;
    int sequence, phase_frame;
    int64_t draw, render, frame_interval;
    size_t history_order, coin_order, history_widgets, coin_widgets, batch_widgets, psbt_widgets;
    s::FrameWork work;
};

struct Observer final : platform::NativeObserver {
    Requests requests;
    NativeKeys keys;
    s::State state;
    s::Facts facts;
    fs::path output, fixtures;
    BackendCapabilities caps;
    uint64_t wallet{};
    std::optional<RefreshIdentity> identity;
    Clock::time_point began=Clock::now(), interval{};
    int64_t draw_ns{}, previous_interval{};
    int stage{}, frames{}, cycles{}, reopen_step{};
    bool selected_history{}, overflow_checked{}, stale_checked{}, psbt_worker_seen{};
    uint64_t retired_generation{};
    c::ax::StableId retired_node{};
    bool capture{}, last_capture{}, previous_capture{}, settled{}, drawn_dark{}, hidden_capture{}, dataset_captured{};
    std::vector<Sample> samples;
    std::vector<std::pair<std::string,int64_t>> latencies;
    std::optional<Clock::time_point> input;
    std::ostringstream resources;
    std::string renderer,edited_field,original_amount;
    int64_t clock_pair_ns{};

    Observer(fs::path out,fs::path data) : output(std::move(out)),fixtures(std::move(data))
    {
        caps.candidate.connection.credential.reference="/no-credentials-fixture";
        caps.candidate.wallet="large-wallet";
        caps.candidate.expected_genesis=fx::REGTEST;
        caps.genesis=fx::REGTEST;
        caps.wallet=WalletAvailability::Available;
        caps.authenticated=caps.identity_verified=true;
        caps.descriptors=true;
        caps.methods["gettransaction"]={true,true,true,false,{"txid","verbose"}};
        const auto binding=a::VerifiedWalletBinding(caps);
        Require(bool(binding),"verified public fixture identity");
        wallet=state.documents.OpenWallet("Large synthetic wallet",*binding,true);
        state.documents.Find(wallet)->history.enabled=true;
        identity=a::WalletRefreshIdentity(caps,1,*state.documents.Find(wallet),requests.recovery.Gate().Epoch());
        Require(identity && requests.refresh.Bind(*identity,caps,false,RefreshMonotonicMs()),"bind scoped refresh worker");
        samples.reserve(1800);
        for (int sample=0;sample<20000;++sample) {
            const auto start=Clock::now();
            clock_pair_ns+=Ns(start);
        }
        Resource("wallet-before-native");
    }
    void Resource(std::string_view mark)
    {
        std::ifstream status("/proc/self/status");
        size_t rss{};
        for (std::string line;std::getline(status,line);) if (line.starts_with("VmRSS:")) rss=std::stoull(line.substr(6));
        const auto count=[](const char* path){return std::distance(fs::directory_iterator(path),fs::directory_iterator());};
        resources<<mark<<','<<Ns(began)<<','<<rss<<','<<count("/proc/self/fd")-1<<','<<count("/proc/self/task")
            <<','<<requests.active.load()<<','<<requests.refresh.Busy()<<','<<requests.local.Busy()
            <<','<<state.histories.size()<<','<<state.coins.size()<<','<<state.imported_views.size()<<'\n';
    }
    void Draw(platform::NativeHost& host,const imkit::Theme& theme,const ui::BootstrapAppearance& appearance)
    {
        const auto start=Clock::now();
        requests.refresh.Tick(RefreshMonotonicMs());
        facts=platform::DrawShellFrame(host,state,requests,theme,appearance);
        draw_ns=Ns(start);
        drawn_dark=appearance.dark;
    }
    bool CaptureFramebuffer() const override { return capture; }
    void Idle(platform::NativeHost&,const platform::NativeFrame&) override
    {
        const auto now=Clock::now();
        previous_interval=interval==Clock::time_point{} ? 0 : Ns(interval,now);
        previous_capture=last_capture;
        interval=now;
        if (now-began>45s) {
            std::ofstream(output/"deadline.txt")<<stage<<' '<<frames<<'\n'<<state.presentation.Snapshot();
            throw std::runtime_error("large wallet deadline stage "+std::to_string(stage)+" frame "+std::to_string(frames));
        }
    }
    bool Action(std::string_view name,std::string value={})
    {
        const auto packet=state.presentation.exchange.Published();
        if (!packet || !packet->valid) return false;
        const auto action=value.empty() ? c::ax::SemanticAction::Press : c::ax::SemanticAction::SetValue;
        for (const auto& node:packet->nodes) {
            if (node.name==name && !node.state.disabled && c::ax::Supports(node.actions,action)) {
                return state.presentation.exchange.QueueNext(packet->generation,node.id,action,std::move(value)).second==c::ActionResult::Queued;
            }
        }
        return false;
    }
    void Record(std::string_view phase,const platform::NativeFrame& frame,Clock::time_point end)
    {
        if (capture || !previous_interval) return;
        samples.push_back({phase,frame.rendered_frames,frames,draw_ns,Ns(interval,end),previous_capture?0:previous_interval,
            facts.history_order.size(),facts.coin_order.size(),facts.history.size(),facts.coin.size(),
            facts.batch.size(),facts.transaction_document.size(),facts.work});
    }
    void Snapshot(const platform::NativeFrame& frame, std::string_view name)
    {
        const auto packet=state.presentation.exchange.Published();
        Require(capture && packet && packet->valid && !packet->overflow && frame.font_texture && frame.decorated && frame.gl_error==0,
                "full X11 pixels and current bounded semantics");
        Require(frame.width==1200 && frame.height==820 && frame.effective_scale==1 && !drawn_dark,"reference native profile");
        const auto path=output/std::string(name);
        std::ofstream ppm(path.string()+".ppm",std::ios::binary);
        ppm<<"P6\n"<<frame.framebuffer_width<<' '<<frame.framebuffer_height<<"\n255\n";
        for(int y=frame.framebuffer_height-1;y>=0;--y) for(int x=0;x<frame.framebuffer_width;++x)
            ppm.write(reinterpret_cast<const char*>(frame.rgba.data()+(size_t(y)*frame.framebuffer_width+x)*4),3);
        Require(bool(ppm),"retained framebuffer");
        std::ofstream(path.string()+".semantics.txt")<<state.presentation.Snapshot();
        std::ofstream(path.string()+".native.json")<<"{\"width\":"<<frame.width<<",\"height\":"<<frame.height
            <<",\"scale\":"<<frame.effective_scale<<",\"frame\":"<<frame.rendered_frames<<",\"semanticGeneration\":"<<packet->generation<<"}\n";
        capture=false;
    }
    bool PickHistoryRow(c::ax::SemanticAction action)
    {
        const auto packet=state.presentation.exchange.Published();
        if (!packet || !packet->valid) return false;
        const auto found=std::ranges::find_if(packet->nodes,[&](const auto& node){
            return node.role==c::ax::SemanticRole::Row && node.name=="Transaction" && node.showing
                && !node.state.disabled && c::ax::Supports(node.actions,action);
        });
        return found!=packet->nodes.end()
            && state.presentation.exchange.QueueNext(packet->generation,found->id,action).second==c::ActionResult::Queued;
    }
    void CheckActionBackpressure()
    {
        const auto packet=state.presentation.exchange.Published();
        Require(packet && packet->valid,"current dense packet before overflow probe");
        const auto focus=std::ranges::find_if(packet->nodes,[](const auto& node){
            return node.name=="Transactions" && node.role==c::ax::SemanticRole::Radio
                && c::ax::Supports(node.actions,c::ax::SemanticAction::Focus);
        });
        Require(focus!=packet->nodes.end(),"bounded focus queue target");
        for (size_t i=0;i<c::SemanticExchange::QueueCapacity;++i)
            Require(state.presentation.exchange.QueueNext(packet->generation,focus->id,c::ax::SemanticAction::Focus).second
                ==c::ActionResult::Queued,"exact allowed focus queue capacity");
        Require(state.presentation.exchange.QueueNext(packet->generation,focus->id,c::ax::SemanticAction::Focus).second
            ==c::ActionResult::Overflow,"one-extra focus request fails closed");
        overflow_checked=true;
    }
    void Frame(platform::NativeHost& host,const platform::NativeFrame& frame) override
    {
        const auto end=Clock::now();
        last_capture=capture;
        const auto view=requests.refresh.View();
        ++frames;
        if (frame.rendered_frames<5) return;
        switch(stage) {
        case 0:
            Record("history-admission",frame,end);
            if (view.busy || !view.ledger || !view.ledger->Observed()) return;
            if (view.scan_budget<4096) {
                Require(requests.refresh.ScanEarlier(*identity,RefreshMonotonicMs()),"real repeated explicit scan while partial");
                return;
            }
            Require(view.ledger->Rows().size()==4096 && view.ledger->Coins().size()==8192 && view.ledger->Limited() && !view.ledger->Complete()
                && !view.ledger->InventoryAvailable(),"100k source uses bounded incomplete GUI ledger");
            Resource("wallet-admitted");
            break;
        case 1:
            Record("history",frame,end);
            if (frames==80) capture=true;
            else if (frames==81) Snapshot(frame,"history");
            else if (frames==90) {
                const auto row=std::ranges::find_if(facts.history,[](const auto& item){return item.first.starts_with("row-") && item.second.rect[1]>300 && item.second.rect[3]<750;});
                Require(row!=facts.history.end() && facts.history_order.size()==4096,"actual clipped transaction rows");
                keys.Scroll(host,row->second);
                capture=true;
            } else if (frames==91) Snapshot(frame,"history-scroll");
            else if (frames==97) Require(PickHistoryRow(c::ax::SemanticAction::Focus),"focus clipped history row");
            else if (frames==102) Require(PickHistoryRow(c::ax::SemanticAction::Select),"select clipped history row");
            else if (frames==106) {
                selected_history=!state.documents.Active()->history.selected.empty();
                Require(selected_history,"exact selection retained after scroll");
                capture=true;
            } else if (frames==107) Snapshot(frame,"history-focus-selection");
            else if (frames==112) Require(Action("Date (UTC)"),"sort history through published action");
            else if (frames==123) CheckActionBackpressure();
            else if (frames==140) Require(Action("Filter transactions","Alternating"),"filter history through published action");
            else if (frames>=185) {
                Require(facts.history_order.size()==2048,"exact filter narrowed 4096 admitted rows");
                Require(state.documents.Navigate(a::Section::UTXOs),"activate actual UTXO section");
                break;
            }
            return;
        case 2:
            Record("utxos",frame,end);
            if (frames==35) capture=true;
            else if (frames==36) Snapshot(frame,"utxos");
            else if (frames==50) Require(Action("Sort date"),"sort UTXOs through published action");
            else if (frames>=64 && frames<114) {
                if (input && state.privacy.hidden!=hidden_capture) {
                    latencies.emplace_back("native-privacy",Ns(*input,end));
                    input.reset();
                    if (state.privacy.hidden && !dataset_captured) {dataset_captured=true;capture=true;}
                } else if (capture) {
                    Require(state.presentation.Snapshot().find("0.00000001")==std::string::npos,"hidden UTXO amount not published");
                    Snapshot(frame,"utxos-hidden");
                } else if (!input && frames%3==0) {
                    hidden_capture=state.privacy.hidden;
                    input=Clock::now();
                    keys.Privacy();
                }
            } else if (frames>=145) {
                Require(latencies.size()>=8 && facts.coin_order.size()==8192,"native latency samples and actual clipped 8192 coin inventory");
                if (state.privacy.hidden) { keys.Privacy(); return; }
                input.reset();
                Require(state.documents.Navigate(a::Section::Send),"activate actual Send section");
                break;
            }
            return;
        case 3:
            if (!state.batch) { if (!Action("Send to Many")) return; else break; }
            return;
        case 4:
            Record("batch-admission",frame,end);
            if (!state.batch || !facts.batch.contains("files")) return;
            if (!state.batch->file_panel) { Require(Action("CSV files"),"open real batch file panel"); return; }
            if (!state.batch_read && state.batch->work.rows.size()!=1000) {
                if (state.batch->path[0]==0) {
                    Action("Local CSV path",(fixtures/"payments.csv").string());
                    return;
                }
                Require(state.batch->status.empty() || state.batch->status.starts_with("Reading CSV"),
                    "CSV worker rejected fixture: "+state.batch->status);
                Action("Load CSV");
                return;
            }
            if (state.batch_read) return;
            Require(state.batch->work.rows.size()==1000,"1000 actual batch rows");
            Resource("batch-1000");
            break;
        case 5:
            Record("batch",frame,end);
            if (frames==40) capture=true;
            else if (frames==41) Snapshot(frame,"batch");
            else if (frames==65) Require(Action("Later rows"),"actual page selection");
            else if (frames==85) {
                Require(state.batch && state.batch->checked && state.batch->checked->ready && !state.batch->checked->errors,
                    "all 1000 unique payment rows validate before edit");
                const auto packet=state.presentation.exchange.Published();
                Require(packet && packet->valid,"current 1000 row batch page");
                const auto amount=std::ranges::find_if(packet->nodes,[](const auto& node){
                    return node.role==c::ax::SemanticRole::TextField && node.name.ends_with(" amount") &&
                        c::ax::Supports(node.actions,c::ax::SemanticAction::SetValue);
                });
                Require(amount!=packet->nodes.end(),"paged amount input");
                edited_field=amount->name;
                original_amount=amount->value;
                Require(Action(edited_field,"-1"),"invalid edit through real batch field");
                input=Clock::now();
            } else if (frames==90) {
                Require(state.batch && state.batch->checked && state.batch->checked->errors==1 && !state.batch->checked->ready,
                    "exact validation error after edit");
                capture=true;
            } else if (frames==91) Snapshot(frame,"batch-error");
            else if (frames==100) Require(Action(edited_field,original_amount),"repair exact amount through same paged field");
            else if (frames>=135) {
                Require(state.batch && state.batch->page==1 && state.batch->checked && state.batch->checked->ready,
                    "batch page and exact validated edit retained");
                if (!Action("Apply batch")) return;
                input=Clock::now();
                break;
            }
            if (input && state.batch && state.batch->checked && state.batch->checked->errors==1) {
                latencies.emplace_back("batch-edit-feedback",Ns(*input,end));
                input.reset();
            }
            return;
        case 6:
            if (state.batch) return;
            Require(state.documents.Active()->recipients.rows.size()==1000,"applied a local 1000 recipient draft without funding");
            if (input) {
                latencies.emplace_back("batch-apply-feedback",Ns(*input,end));
                input.reset();
            }
            Record("send-applied",frame,end);
            if (frames<120) return;
            if (!state.local_document.dialog.open && !requests.local.Busy())
                Require(s::BeginLocalDocument(state,requests,s::LocalDocumentMode::File,(fixtures/"large.psbt").string()),"open bounded local PSBT dialog");
            break;
        case 7:
            if (!Action("Inspect file")) return;
            break;
        case 8:
            Record("psbt-open",frame,end);
            if (requests.local.Busy() && !psbt_worker_seen) {
                psbt_worker_seen=true;
                Resource("psbt-worker");
            }
            if (!state.documents.Active()->imported || requests.local.Busy() || state.local_document.dialog.open) return;
            Require(state.documents.Active()->imported->parsed->wire.size()>48000,"actual large parsed PSBT document");
            Resource("large-psbt-open");
            break;
        case 9:
            Record("psbt-overview",frame,end);
            if (frames==38) capture=true;
            else if (frames==39) Snapshot(frame,"psbt-overview");
            else if (frames>=145) {
                Require(s::ToggleTransactionHex(state,requests),"actual bounded workbench hex");
                break;
            }
            return;
        case 10:
            Record("psbt-hex",frame,end);
            if (frames==38) capture=true;
            else if (frames==39) Snapshot(frame,"psbt-hex");
            else if (frames==145) {
                const auto& next=facts.transaction_document.at("bytes-next");
                Require(next.visible && !next.disabled,"actual byte page button visible to native input");
                input=Clock::now();
                keys.Click(host,next,true);
            } else if (frames==146) {
                keys.Click(host,facts.transaction_document.at("bytes-next"),false);
                break;
            }
            return;
        case 11: {
            Record("psbt-reopen",frame,end);
            const auto* doc=state.documents.Active();
            if (reopen_step==1) {
                if (Action("Inspect file")) reopen_step=2;
                return;
            }
            if (!doc || !doc->imported || requests.local.Busy() || state.local_document.dialog.open) return;
            const auto id=doc->id;
            if (!cycles && reopen_step==0) {
                if (state.imported_views.at(id).bytes_page!=1) return;
                if (input) {
                    latencies.emplace_back("native-hex-page",Ns(*input,end));
                    input.reset();
                }
            }
            if (frames<5) return;
            Require(state.imported_views.contains(id) && state.imported_views.size()==1,"one live workbench view after repeated close/open");
            const auto old_packet=state.presentation.exchange.Published();
            if (!old_packet || !old_packet->valid || old_packet->nodes.empty()) return;
            if (reopen_step==2) {
                Require(doc->imported->parsed->wire.size()>48000,"worker reopened exact full PSBT");
                ++cycles;
                Resource("psbt-reopened");
            }
            Require(state.documents.Close(std::array{id}) && state.documents.Activate(wallet),"close exact workbench view");
            if (cycles<12) {
                Require(s::BeginLocalDocument(state,requests,s::LocalDocumentMode::File,(fixtures/"large.psbt").string()),
                    "reopen through owned worker file path");
                reopen_step=1;
                frames=0;
                return;
            }
            const auto packet=state.presentation.exchange.Published();
            Require(packet && packet->valid,"pre-transition packet");
            Require(packet->scope!=s::SemanticScope(state),"view owner changes immediately on close");
            retired_generation=old_packet->generation;
            retired_node=old_packet->nodes.front().id;
            requests.hold=true;
            requests.refresh.SetOffline(false,RefreshMonotonicMs());
            Require(requests.refresh.RequestNow(RefreshMonotonicMs()),"start owned slow refresh");
            Resource("documents-closed-worker-starting");
            break;
        }
        case 12:
            Record("refresh-lifetime",frame,end);
            if (!stale_checked) {
                const auto current=state.presentation.exchange.Published();
                if (!current || !current->valid || current->generation<=retired_generation) return;
                const auto stale=state.presentation.exchange.QueueNext(retired_generation,retired_node,c::ax::SemanticAction::Focus);
                Require(stale.second==c::ActionResult::Stale,"closed document packet refuses stale action");
                stale_checked=true;
            }
            if (!requests.waiting) return;
            Resource("refresh-running");
            requests.refresh.SetOffline(true,RefreshMonotonicMs());
            requests.refresh.Unbind();
            requests.release=true;
            break;
        case 13:
            if (requests.refresh.Busy()) return;
            Require(!requests.refresh.View().identity && requests.cancelled==1,"retired timed worker cannot publish after unbind");
            requests.hold=false;
            requests.refresh.SetOffline(false,RefreshMonotonicMs());
            Require(requests.refresh.Bind(*identity,caps,false,RefreshMonotonicMs()),"rebind exact fixture identity");
            break;
        case 14:
            if (view.busy || !view.identity || !view.node || view.freshness[0]!=Freshness::Live) return;
            requests.fail=true;
            Require(requests.refresh.RequestNow(RefreshMonotonicMs()),"start failing read-only refresh");
            break;
        case 15:
            if (view.busy || view.errors[0]!=rpc::Error::Connection) return;
            Require(view.ledger && view.ledger->Rows().size()==4096,"failed refresh retains dated bounded rows");
            capture=true;
            break;
        case 16:
            Snapshot(frame,"refresh-error");
            requests.refresh.PrivacyLock();
            Require(state.documents.Close(std::array{wallet})==1,"close wallet after privacy fence");
            break;
        case 17:
            if (frames<22) return;
            Require(!requests.refresh.View().ledger && !requests.refresh.Busy() && !requests.local.Busy()
                && requests.active==0 && requests.entered==requests.exited && requests.maximum==1 && !requests.owner_fault
                && state.histories.empty() && state.coins.empty() && state.imported_views.empty() && !state.batch,
                "owned work and document projections settled after close and privacy retirement");
            Resource("before-native-teardown");
            host.RequestClose();
            ++stage;
            return;
        default: return;
        }
        frames=0;
        ++stage;
    }
    void Teardown(const platform::NativeTeardown& status) override
    {
        settled=status.renderer_cleared && status.platform_cleared && status.textures_released && status.imgui_cleared &&
            status.context_cleared && status.accessibility_cleared && status.input_cleared;
        Resource("after-native-teardown");
    }
    void Report()
    {
        std::ofstream raw(output/"frames.csv");
        raw<<"phase,frame,phase_frame,ui_ns,idle_to_render_ns,previous_frame_interval_ns,history_order,coin_order,history_widgets,coin_widgets,batch_widgets,psbt_widgets,"
            <<"history_indexed,history_flattened,history_drawn,history_chart_indexed,history_chart_drawn,coins_adopted,coins_indexed,coins_drawn,"
            <<"coins_summarized,coin_chart_indexed,batch_validated,batch_drawn,send_counted,psbt_overview_items,psbt_hex_bytes\n";
        for(const auto& x:samples) raw<<x.phase<<','<<x.sequence<<','<<x.phase_frame<<','<<x.draw<<','<<x.render<<','<<x.frame_interval<<','
            <<x.history_order<<','<<x.coin_order<<','<<x.history_widgets<<','<<x.coin_widgets<<','<<x.batch_widgets<<','<<x.psbt_widgets
            <<','<<x.work.history_indexed<<','<<x.work.history_flattened<<','<<x.work.history_drawn<<','<<x.work.history_chart_indexed
            <<','<<x.work.history_chart_drawn<<','<<x.work.coins_adopted<<','<<x.work.coins_indexed<<','<<x.work.coins_drawn
            <<','<<x.work.coins_summarized<<','<<x.work.coin_chart_indexed<<','<<x.work.batch_validated<<','<<x.work.batch_drawn
            <<','<<x.work.send_counted<<','<<x.work.psbt_overview_items<<','<<x.work.psbt_hex_bytes<<'\n';
        std::ofstream feedback(output/"feedback.csv");
        feedback<<"event,nanoseconds\n";
        for (const auto& [name,timing]:latencies) feedback<<name<<','<<timing<<'\n';
        std::ofstream(output/"resources.csv")<<"mark,since_start_ns,rss_kib,open_fds,tasks,fetch_workers,refresh_busy,document_busy,history_views,coin_views,imported_views\n"<<resources.str();
        std::ofstream(output/"result.json")<<"{\"stage\":"<<stage<<",\"teardown\":"<<(settled?"true":"false")
            <<",\"historySource\":"<<requests.source.transactions.size()<<",\"utxoSource\":"<<requests.source.coins.size()
            <<",\"cycles\":"<<cycles<<",\"refreshEntered\":"<<requests.entered<<",\"refreshExited\":"<<requests.exited
            <<",\"refreshCancelled\":"<<requests.cancelled<<",\"queueOverflowChecked\":"<<(overflow_checked?"true":"false")
            <<",\"historySelectionChecked\":"<<(selected_history?"true":"false")
            <<",\"closeScopeRetired\":"<<(stale_checked?"true":"false")
            <<",\"psbtWorkerSeen\":"<<(psbt_worker_seen?"true":"false")<<",\"clockPairMeanNs\":"<<clock_pair_ns/20000
            <<",\"renderer\":"<<rpc::Quote(renderer)<<"}\n";
    }
};
}
} // namespace roots::gui

int main(int argc,char** argv)
{
    using namespace roots::gui;
    if (argc!=4) return 2;
    try {
        if (std::string_view(argv[1])=="--admission") return Admission(argv[2],argv[3]);
        Observer observer(argv[1],argv[3]);
        try {
            platform::NativeOptions options{1200,820,false,1,argv[2]};
            options.accessibility=&observer.state.presentation.exchange;
            platform::NativeHost* current{};
            {
                platform::NativeHost host(options,&observer,[&](const auto& theme,const auto& appearance) {
                    observer.Draw(*current,theme,appearance);
                });
                current=&host;
                observer.renderer=host.Renderer();
                Require(host.Run()==0,"native host return");
            }
            observer.Report();
            Require(observer.settled && observer.stage==18,"completed native lifetime journey");
        } catch (...) {
            observer.Report();
            throw;
        }
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
    return 0;
}
