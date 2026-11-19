// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "tests/wallet_tools_fixture.h"
#include "ui/theme/theme.h"
#include <imgui_test_engine/imgui_te_context.h>
#include <imgui_test_engine/imgui_te_engine.h>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <unistd.h>

using namespace roots::gui;
namespace sh=ui::shell;
namespace a=app;
namespace c=ui::components;
namespace fx=test::wallet_tools;
namespace {
struct Files final:c::NativeInputPort {
    std::string path;
    bool held{},active{},cancel{};
    unsigned choices{};
    void BeginView(std::string_view) override{}
    void Privacy(bool) override{}
    void EnterEditor(uint32_t,bool) override{}
    void LeaveEditor() override{}
    std::optional<std::string> Paste(uint32_t,std::string_view,int,int,int,size_t) override{return {};}
    bool FileChooserAvailable() const override{return true;}
    uint64_t ChooseFile(std::string_view,bool save) override{active=save;return save?++choices:0;}
    c::FileChoice PollFile(uint64_t id) override{
        if(!active || id!=choices)return {c::NativeOutcome::Stale,{},{}};
        if(held)return {c::NativeOutcome::Pending,{},{}};
        active=false;return {cancel?c::NativeOutcome::Cancelled:c::NativeOutcome::Chosen,path,{}};
    }
};
struct Fixture final:a::Requests {
    std::unique_ptr<fx::Wallets> wallets;
    std::unique_ptr<RefreshService> refresh;
    std::vector<a::WalletBalanceSummary> balance_rows;
    a::WalletSearchResult address_results,output_results,formula_results;
    bool fail_wallet_refresh{};
    sh::State state;
    sh::Facts facts;
    Files files;
    std::filesystem::path path;
    a::HostObservations observations;
    unsigned dispatches{};
    bool initialized{};
    ~Fixture(){if(!path.empty())std::filesystem::remove_all(path);}
    RefreshService* Refresh() override{return refresh.get();}
    AddressService* Addresses() override{return &wallets->addresses;}
    a::HostObservations Observe() const override{return observations;}
    a::Receipt Submit(const a::Request&) override{++dispatches;return {false,"fixture has no mutation port"};}
    void Seed(){
        wallets=std::make_unique<fx::Wallets>();
        refresh=std::make_unique<RefreshService>([this](const auto& request,std::stop_token){return wallets->Fetch(request);});
        wallets->Seed(state,*refresh);
        address_results=a::SearchWallets(state.documents,*refresh,&wallets->addresses,{},{"address label",a::SearchType::Address,0});
        output_results=a::SearchWallets(state.documents,*refresh,&wallets->addresses,{},{"output label",a::SearchType::Output,0});
        formula_results=a::SearchWallets(state.documents,*refresh,&wallets->addresses,{},{"formula-sheet",a::SearchType::Transaction,0});
        observations.connection={a::ServiceState::Ready,fx::RAW_DETAIL};
        observations.activity={a::ServiceState::Loading,fx::RAW_DETAIL};
        observations.tasks={{1,fx::RAW_DETAIL,fx::RAW_DETAIL,a::ServiceState::Loading,425}};
        observations.log={fx::RAW_DETAIL};
        const auto* scratch=std::getenv("TMPDIR");
        if(!scratch)throw std::runtime_error("owned TMPDIR required");
        auto folder=(std::filesystem::path(scratch)/"wallet-tools-test-XXXXXX").string();
        if(!::mkdtemp(folder.data()))throw std::runtime_error("isolated export fixture directory");
        path=folder;files.path=(path/"explicit.csv").string();
        initialized=true;
    }
    std::string Export() const {
        std::ifstream file(files.path);
        return {std::istreambuf_iterator<char>(file),{}};
    }
};
ImGuiTest* Test(ImGuiTestEngine* engine,const char* name)
{
    auto* test=IM_REGISTER_TEST(engine,"roots.wallet_tools",name);
    test->SetVarsDataType<Fixture>();
    test->GuiFunc=[](ImGuiTestContext* ctx){
        auto& fixture=ctx->GetVars<Fixture>();
        if(!fixture.initialized)fixture.Seed();
        if(fixture.fail_wallet_refresh){
            fixture.wallets->fail=true;
            if(!fixture.refresh->RequestNow(1))throw std::runtime_error("test refresh must have current wallet");
            fx::Settle(*fixture.refresh,1);
            fixture.fail_wallet_refresh=false;
        }
        fixture.balance_rows=a::WalletBalances(fixture.state.documents,*fixture.refresh,{});
        c::NativeInputScope input(&fixture.files);
        const auto theme=ui::MakeBootstrapTheme({});
        ui::theme::Scope style(theme);
        fixture.facts=sh::DrawWorkspace(fixture.state,fixture,theme,{},{1280,800});
    };
    return test;
}
}

void RegisterWalletToolsTests(ImGuiTestEngine* engine)
{
    auto* test=Test(engine,"current_and_all_typed_search_preserve_exact_wallet_and_row_ids");
    test->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();ctx->Yield(2);
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiKey_S);ctx->Yield(3);
        IM_CHECK(s.facts.tools_visible && s.state.wallet_tools.tool==sh::WalletTool::CurrentSearch);
        auto results=[&]{return s.state.wallet_tools.results?&*s.state.wallet_tools.results:nullptr;};
        ctx->ItemInputValue(s.facts.tools.at("query").id,"shared COFFEE");
        ctx->ItemClick(s.facts.tools.at("Transaction").id);ctx->ItemClick(s.facts.tools.at("search").id);ctx->Yield(2);
        IM_CHECK(results() && results()->hits.size()==1);
        IM_CHECK_EQ(results()->hits.front().document,s.wallets->ids[1]);
        ctx->ItemClick(s.facts.tools.at("tool-close").id);ctx->Yield(2);
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiMod_Alt|ImGuiKey_S);ctx->Yield(3);
        ctx->ItemInputValue(s.facts.tools.at("query").id,"aaaaaaaa");
        ctx->ItemClick(s.facts.tools.at("search").id);ctx->Yield(2);
        IM_CHECK(results() && results()->hits.size()==2 && results()->hits[0].key==results()->hits[1].key);
        IM_CHECK(results()->hits[0].network!=results()->hits[1].network);
        ctx->ItemClick(s.facts.tools.at("hit-0").id);ctx->ItemClick(s.facts.tools.at("show").id);ctx->Yield(3);
        IM_CHECK_EQ(s.state.documents.ActiveId(),s.wallets->ids[0]);
        IM_CHECK_EQ(s.state.documents.Active()->history.selected,fx::Hash('a').hex());
        IM_CHECK_EQ(s.state.documents.Active()->section,a::Section::Transactions);
        const auto& addresses=s.address_results;
        IM_CHECK_EQ(addresses.hits.size(),2);
        IM_CHECK_EQ(addresses.hits[0].key,"address-key-wallet-a");
        const auto& outputs=s.output_results;
        IM_CHECK_EQ(outputs.hits.size(),2);
        IM_CHECK_EQ(outputs.hits[0].key,fx::Hash('1').hex()+":0");
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiKey_S);ctx->Yield(3);
        ctx->ItemClick(s.facts.tools.at("Address").id);ctx->ItemClick(s.facts.tools.at("search").id);ctx->Yield(2);
        IM_CHECK(results() && results()->hits.size()==1 && results()->hits.front().document==s.wallets->ids[0]);
        ctx->ItemClick(s.facts.tools.at("hit-0").id);ctx->ItemClick(s.facts.tools.at("show").id);ctx->Yield(3);
        IM_CHECK_EQ(s.state.documents.Active()->section,a::Section::Addresses);
        IM_CHECK_EQ(s.state.documents.Active()->addresses.selected,"address-key-wallet-a");
        IM_CHECK(s.state.retained_address_navigation && s.state.retained_address_navigation->document==s.wallets->ids[0]);
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiKey_S);ctx->Yield(3);
        ctx->ItemClick(s.facts.tools.at("Output").id);ctx->ItemClick(s.facts.tools.at("search").id);ctx->Yield(2);
        ctx->ItemClick(s.facts.tools.at("hit-0").id);ctx->ItemClick(s.facts.tools.at("show").id);ctx->Yield(3);
        IM_CHECK_EQ(s.state.documents.Active()->section,a::Section::UTXOs);
        IM_CHECK_EQ(s.state.documents.Active()->coins.focused,fx::Hash('1').hex()+":0");
        IM_CHECK_EQ(s.wallets->fetches.load(),3);IM_CHECK_EQ(s.wallets->address_fetches.load(),3);
        IM_CHECK_EQ(s.dispatches,0);
    };
    test=Test(engine,"lock_retires_visible_rows_and_held_search_export");
    test->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();ctx->Yield(2);
        ctx->KeyPress(ImGuiMod_Ctrl|ImGuiMod_Shift|ImGuiMod_Alt|ImGuiKey_S);ctx->Yield(3);
        ctx->ItemClick(s.facts.tools.at("search").id);ctx->Yield(2);
        IM_CHECK(s.state.wallet_tools.results);
        const auto csv=a::WalletSearchCsv(*s.state.wallet_tools.results,true);
        const auto& formula=s.formula_results;
        IM_CHECK(a::WalletSearchCsv(formula,true).find("\"'=formula-sheet\"")!=std::string::npos);
        IM_CHECK(a::WalletSearchCsv(formula,false).find(",Wallet,")==std::string::npos);
        IM_CHECK(csv.find("Locked-only label")==std::string::npos && csv.find("Locked wallet canary")==std::string::npos);
        s.files.held=true;ctx->ItemClick(s.facts.tools.at("search-export").id);ctx->Yield(2);
        IM_CHECK(s.state.wallet_tools.export_file.chooser && !s.state.wallet_tools.export_file.bytes.empty());
        s.state.documents.Find(s.wallets->ids[0])->observation=a::ObservationState::Locked;s.files.held=false;ctx->Yield(3);
        IM_CHECK(!s.state.wallet_tools.results && !s.state.wallet_tools.export_file.chooser && !std::filesystem::exists(s.files.path));
        IM_CHECK(s.state.presentation.Snapshot().find("Archived wallet")==std::string::npos);
        s.state.privacy.hidden=true;ctx->Yield(3);
        IM_CHECK(!s.facts.tools_visible && !s.state.wallet_tools.results);
        IM_CHECK(s.state.presentation.Snapshot().find("Shared coffee")==std::string::npos);
        IM_CHECK_EQ(s.dispatches,0);
    };
    test=Test(engine,"summary_keeps_distinct_networks_exact_stale_amounts_and_unknowns");
    test->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();ctx->Yield(2);
        sh::OpenWalletTool(s.state,s,sh::WalletTool::Summary);ctx->Yield(3);
        auto rows=s.balance_rows;
        IM_CHECK_EQ(rows.size(),2);
        IM_CHECK_EQ(rows[0].freshness,Freshness::Cached);
        IM_CHECK(rows[0].scanning && rows[0].categories[0]->sats()==100000001 && !rows[0].categories[3]);
        IM_CHECK_EQ(rows[1].freshness,Freshness::Live);
        IM_CHECK(rows[0].network!=rows[1].network && rows[1].categories[0]->sats()==200000003);
        const auto date=rows[1].observed;
        s.fail_wallet_refresh=true;ctx->Yield(2);
        rows=s.balance_rows;
        IM_CHECK_EQ(rows[1].freshness,Freshness::Stale);
        IM_CHECK(rows[1].observed==date && rows[1].categories[0]->sats()==200000003);
        const auto text=s.state.presentation.Snapshot();
        IM_CHECK(text.find("Stale after refresh failure")!=std::string::npos && text.find("2.00000003 BTC")!=std::string::npos);
        IM_CHECK(text.find("Locked wallet canary")==std::string::npos && text.find("3.00000004")==std::string::npos);
        s.state.privacy.hidden=true;ctx->Yield(3);
        IM_CHECK(s.state.presentation.ChartSamples().empty() && s.state.presentation.Snapshot().find("2.00000003")==std::string::npos);
    };
    test=Test(engine,"activity_export_is_typed_timestamped_bounded_and_exclusive");
    test->TestFunc=[](ImGuiTestContext* ctx){
        auto& s=ctx->GetVars<Fixture>();ctx->Yield(2);
        ctx->ItemClick(s.facts.activity_button.id);ctx->Yield(3);
        auto text=s.state.presentation.Snapshot();
        IM_CHECK(text.find("Operational log")!=std::string::npos && text.find("UTC")!=std::string::npos && text.find(fx::RAW_DETAIL)==std::string::npos);
        ctx->ItemClick(s.facts.tools.at("activity-export").id);ctx->Yield(3);
        const auto original=s.Export();
        IM_CHECK(original.find("Observed UTC,Activity,State,Progress per mille")!=std::string::npos);
        IM_CHECK(original.find("Background work,Loading,425")!=std::string::npos && original.find(fx::RAW_DETAIL)==std::string::npos);
        IM_CHECK_EQ(std::filesystem::status(s.files.path).permissions(),std::filesystem::perms::owner_read|std::filesystem::perms::owner_write);
        ctx->ItemClick(s.facts.tools.at("activity-export").id);ctx->Yield(3);
        IM_CHECK_EQ(s.Export(),original);IM_CHECK(s.state.activity_notice.find("existing files are retained")!=std::string::npos);
        s.files.path=(s.path/"changed.csv").string();s.files.held=true;
        ctx->ItemClick(s.facts.tools.at("activity-export").id);ctx->Yield(2);
        IM_CHECK(s.state.activity_export.chooser);
        s.observations.activity.state=s.observations.tasks.front().state=a::ServiceState::Error;s.files.held=false;ctx->Yield(3);
        IM_CHECK(!std::filesystem::exists(s.files.path) && s.state.activity_notice.find("changed")!=std::string::npos);
        for(unsigned n=0;n<80;++n){s.observations.tasks.front().progress_per_mille=n;ctx->Yield();}
        IM_CHECK_EQ(s.state.activity_log.Records().size(),64);
        IM_CHECK_EQ(s.dispatches,0);
    };
}
