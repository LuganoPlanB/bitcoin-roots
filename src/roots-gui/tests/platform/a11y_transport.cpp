// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
// Synthetic bounded-packet transport tests, never a native UI/Orca substitute.
#include "platform/linux/accessibility.h"
#include "ui/components/semantic_packet.h"
#include <array>
#include <algorithm>
#include <filesystem>
#include <cstdio>
#include <chrono>
#include <fstream>
#include <thread>
#include <stdexcept>
using namespace roots::gui;namespace c=ui::components;namespace ax=c::ax;
int main(int argc,char** argv){if(argc!=3)return 2;try{
    const std::filesystem::path out(argv[1]);std::filesystem::create_directories(out);const std::string test(argv[2]);
    c::SemanticExchange exchange;exchange.BindOwner();platform::LinuxAccessibility bridge(exchange);
    bool foreign_rejected{};std::thread foreign([&]{try{bridge.Pump();}catch(const std::logic_error&){foreign_rejected=true;}});foreign.join();
    if(test=="startup-cancel"){const bool pending=bridge.Status().callbacks>0;const bool closed=bridge.Close();const auto status=bridge.Status();const bool clean=pending && closed && !status.callbacks && !status.objects && !status.pending;std::ofstream(out/"result.json")<<"{\"closed\":"<<(clean?"true":"false")<<",\"pendingAtCancellation\":"<<(pending?"true":"false")<<",\"callbacks\":"<<status.callbacks<<"}\n";return clean?0:1;}
    const auto start=std::chrono::steady_clock::now();uint64_t generation{},command{},ticks{};
    const auto publish=[&](const std::string& mode){
        std::array<ax::SemanticNode,6> nodes{};const char* names[]{"Explicit transport fixture","Public Unicode field","Inspect fixture","Frozen fixture","Observation amount","Background progress"};
        for(size_t i=0;i<nodes.size();++i){nodes[i].id=101+i;nodes[i].parent=i?101:0;nodes[i].name=names[i];nodes[i].minimum={float(i*20),float(i*30)};nodes[i].maximum={float(i*20+100),float(i*30+25)};}
        nodes[1].role=ax::SemanticRole::TextField;nodes[1].value="Öffentlich##2026###";nodes[1].actions=ax::SemanticAction::SetValue|ax::SemanticAction::Focus;
        nodes[2].role=nodes[3].role=ax::SemanticRole::Button;nodes[2].actions=nodes[3].actions=ax::SemanticAction::Press|ax::SemanticAction::Focus;nodes[3].state.disabled=true;
        nodes[4].role=ax::SemanticRole::Text;nodes[4].value=mode=="hidden"?"Hidden":"0.01000000";nodes[4].state.readOnly=true;
        nodes[5].role=ax::SemanticRole::Progress;nodes[5].numericValue=mode=="progress"?500:425;nodes[5].maximumValue=1000;nodes[5].state.busy=true;nodes[5].state.readOnly=true;
        if(mode=="status" || mode=="status-error"){nodes[4].role=ax::SemanticRole::Status;nodes[4].name="Connection";nodes[4].value=mode=="status"?"Connection: Stale / last success 12:00 UTC":"Connection: Error / fixture offline";}
        const std::string bad_utf8(1,char(0xff)),huge(65537,'X');if(mode=="utf8")nodes[1].name=bad_utf8;if(mode=="text-overflow")nodes[4].value=huge;if(mode=="coordinates")nodes[2].maximum.x=1e30F;
        if(mode=="node-overflow"){std::vector<ax::SemanticNode> many(257);exchange.Publish({many,++generation,false},"fixture");}
        else {if(mode=="graph")nodes[2].parent=999;if(mode=="sort")std::reverse(nodes.begin(),nodes.end());exchange.Publish({nodes,++generation,false},mode=="scope"?"other fixture":"fixture");}
    };
    publish("normal");bool closed{};unsigned long long processed{};
    for(;;){++ticks;bridge.Pump();bridge.Update(10,20,1200,820,true);auto status=bridge.Status();
        unsigned long long seq{};std::string mode;std::ifstream input(out/"command");const bool complete=bool(input>>seq>>mode);
        if(complete && seq>command){command=seq;processed=seq;if(mode=="close"){closed=bridge.Close();exchange.Close();break;}publish(mode);bridge.Update(10,20,1200,820,true);status=bridge.Status();}
        std::ofstream f(out/"state.pending");f<<"{\"ticks\":"<<ticks<<",\"command\":"<<processed<<",\"registered\":"<<(status.registered?"true":"false")<<",\"bus\":\""<<status.bus_name<<"\",\"objects\":"<<status.objects<<",\"pending\":"<<status.pending<<",\"callbacks\":"<<status.callbacks<<",\"error\":"<<(!status.error.empty()?"true":"false")<<",\"overflow\":"<<(exchange.Published()->overflow?"true":"false")<<"}\n";f.close();std::filesystem::rename(out/"state.pending",out/"state.json");
        if(test=="startup-cancel" || (test=="missing-bus" && status.callbacks==0 && !status.error.empty())){closed=bridge.Close();exchange.Close();break;}
        if(std::chrono::steady_clock::now()-start>std::chrono::seconds(20))throw std::runtime_error("AT-SPI transport watchdog");std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    const auto status=bridge.Status();const bool clean=closed && status.closed && status.settled && !status.objects && !status.pending && !status.callbacks && foreign_rejected;
    std::ofstream(out/"result.json")<<"{\"closed\":"<<(clean?"true":"false")<<",\"wrongThreadRejected\":"<<(foreign_rejected?"true":"false")<<",\"objects\":"<<status.objects<<",\"pending\":"<<status.pending<<",\"callbacks\":"<<status.callbacks<<",\"hostEffects\":0}\n";return clean?0:1;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
