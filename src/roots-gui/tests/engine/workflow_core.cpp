// Adapted core-only sections of ImKit workflow.cpp at 986d8d286a74fb7b2f724b9ef5d008af139aa887.
// Copyright (c) ImKit authors; MIT, see imkit/LICENSE in third_party/test-inputs.tar.gz.
// Editor canvas/tiles and unrelated application workflows are deliberately excluded.
#include <imkit/imkit.h>
#include <array>
#include <cstdio>
#include <cstdlib>
namespace { int executed{}, failures{};
void Check(bool value, const char* what) { ++executed; if (!value) { ++failures; std::fprintf(stderr,"FAIL %s\n",what); } }
void* Allocate(std::size_t n,void*) { return std::malloc(n); }
void Free(void* p,void*) { std::free(p); }
}
int main() { using namespace imkit;
    std::array<FeedbackView,5> notices{{{1,"old","",FeedbackKind::Info,0,0},{2,"expired","",FeedbackKind::Error,4,9},{1,"new","",FeedbackKind::Success,0,1},{3,"priority","",FeedbackKind::Warning,0,3},{4,"equal","",FeedbackKind::Info,0,1}}};
    std::array<std::size_t,4> order{};
    auto count=SelectNotifications(notices,5,order,3);Check(count==3&&order[0]==3&&order[1]==2&&order[2]==4,"update expiry priority stable order");
    Check(SelectNotifications(notices,5,{},4)==0&&SelectNotifications({},5,order,4)==0,"empty notification scratch");
    RequestBuffer noRequests;Check(!noRequests.Push(1)&&noRequests.overflow,"request overflow");
    ImGui::SetAllocatorFunctions(Allocate,Free);
    for(int generation=0;generation<2;++generation){
        auto* context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.DisplaySize={1000,800};io.DeltaTime=1.f/60;io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
        auto theme=MakeTheme(ColorScheme::Dark,ContrastMode::HighContrast,Density::Comfortable);ApplyTheme(theme);
        std::array<accessibility::SemanticNode,256> nodes{};std::array<accessibility::ActionRequest,16> pending{};
        accessibility::ActionQueue queue(pending);accessibility::AccessibilityFrame semantics(nodes,&queue);
        ComponentOptions options{&theme,nullptr,&semantics};
        auto frame=[&](auto draw){io.AddFocusEvent(true);ImGui::NewFrame();semantics.Begin(generation);ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({900,740});ImGui::Begin("fixture",nullptr,ImGuiWindowFlags_NoSavedSettings);draw();ImGui::End();ImGui::Render();Check(semantics.Tree().Validate(),"valid semantic tree");};
        StableId workspaceRequest=0;
        bool toggleRequest=false,groupOpen=true,detailsOpen=false,detailsDrawn=false;
        auto drawWorkspace=[&]{
            const WorkspaceTab tabs[]={{101,"Edit","Edit items"},{102,"Connect","Connect items"},{103,"Review","Review items"}};
            workspaceRequest=WorkspaceTabs("workspace-tabs",tabs,101,nullptr,options);
            if(HierarchyGroupHeader("items","Items",1,&groupOpen,nullptr,IconId::Count,options))
                HierarchyRow("row",{7,"Item A","Selectable item",IconId::Count,1,true},nullptr,options);
            if(BeginInspectorCard("card","Settings","Related controls",nullptr,IconId::Count,options))
                toggleRequest=SettingToggleRow("enabled","Enabled","Use this item",false,false,"",options);
            EndInspectorCard();
            detailsDrawn=BeginCollapsibleInspectorCard("details","Details",&detailsOpen,nullptr,IconId::Count,options);
            if(detailsDrawn) {ImGui::TextUnformatted("Additional settings");EndCollapsibleInspectorCard();}
        };
        frame(drawWorkspace);
        Check(!detailsDrawn,"collapsed inspector omits detail content");
        detailsOpen=true;frame(drawWorkspace);
        Check(detailsDrawn,"host can reopen inspector detail content");
        accessibility::SemanticNode connect{},toggle{};
        for(const auto& node:semantics.Tree().nodes) {
            if(node.name=="Connect")connect=node;
            if(node.name=="Enabled")toggle=node;
        }
        Check(connect.id&&toggle.id,"workspace and setting controls expose semantic names");
        queue.Push({connect.id,accessibility::SemanticAction::Select});frame(drawWorkspace);
        Check(workspaceRequest==102,"workspace returns selection request without changing host selection");
        queue.Push({toggle.id,accessibility::SemanticAction::Toggle});frame(drawWorkspace);
        Check(toggleRequest,"setting row returns toggle request without changing host value");
        ImGui::DestroyContext(context);
    }
    std::printf("executed=%d passed=%d failed=%d skipped=0\n",executed,executed-failures,failures);
    return failures ? 1 : 0;
}
