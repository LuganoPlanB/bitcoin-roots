// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/theme/tokens.h"
#include "ui/theme/theme.h"
#include "ui/theme/appearance.h"
#include <imgui_impl_null.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <cmath>
using namespace roots::gui::ui::theme;
namespace {
int checks{}, failures{};
void Check(bool ok, const char* id) { ++checks; if (!ok) { ++failures; std::fprintf(stderr,"FAIL %s\n",id); } }
bool Equal(ImVec4 a,ImVec4 b) { return a.x==b.x && a.y==b.y && a.z==b.z && a.w==b.w; }
}
int main(int argc,char** argv)
{
    if (argc!=2) return 2;
    const std::filesystem::path directory(argv[1]); std::filesystem::create_directories(directory);
    for (bool dark : {false,true}) {
        const auto t=Wallet(dark); const auto theme=Make(dark);
        Check(Equal(t.canvas,Rgb(dark?0x171717:0xFFFEFA)),"exact canvas");
        Check(Equal(t.interaction,Rgb(dark?0xE15364:0x4F97E9)),"interaction is separate from primary/error");
        Check(Equal(t.focus,Rgb(0x7468FF)),"exact focus");
        Check(Equal(t.error.foreground,Rgb(dark?0xFFB4BC:0xB4233A)),"exact error");
        Check(Equal(t.identity,Rgb(dark?0xF7931A:0xFFB604)),"identity");
        const ImVec4 actual[]{t.canvas,t.alternate,t.panel,t.strong_panel,t.text,t.interaction,t.primary,t.on_primary,
            t.primary_hover,t.primary_pressed,t.boundary,t.focus,t.link,t.success.foreground,t.success.background,
            t.warning.foreground,t.warning.background,t.error.foreground,t.error.background,t.info.foreground,t.info.background,
            t.utxo,t.script_structure,t.script_key,t.script_signature,t.identity};
        const unsigned light[]{0xFFFEFA,0xF3F9FF,0xFCFCFC,0xF3F8FD,0x030B20,0x4F97E9,0x082952,0xFFFEFA,
            0x103B6A,0x061F3F,0x74849A,0x7468FF,0x082952,0x1B6E45,0xE8F5ED,0x855C00,0xFFF4DA,0xB4233A,0xFCECEE,
            0x082952,0xF3F9FF,0x66768B,0x315F9C,0x5A46B8,0x1B6E45,0xFFB604};
        const unsigned night[]{0x171717,0x1A1717,0x211D1D,0x292424,0xFFFFFF,0xE15364,0xFFFFFF,0x171717,
            0xF2EEEE,0xE0DADA,0x8A7F7F,0x7468FF,0xB0D3F1,0x75D6A3,0x17362A,0xF5CD70,0x3A2F16,0xFFB4BC,0x3A2028,
            0xB0D3F1,0x1D2C3C,0xA99EA2,0xB0D3F1,0xBEA9FF,0x75D6A3,0xF7931A};
        for (std::size_t i=0;i<std::size(actual);++i) Check(Equal(actual[i],Rgb(dark?night[i]:light[i])),"every specified opaque wallet role exact");
        Check(Equal(t.secondary,Rgb(dark?0xFFFFFF:0x030B20,.72F)),"exact translucent secondary");
        Check(Equal(t.soft_accent,dark?Rgb(0xFFFFFF,.12F):Rgb(0xB0D3F1)),"exact soft accent");
        Check(imkit::ValidateContrast(theme),"ImKit semantic contrast");
        for (auto bg : {t.canvas,t.alternate,t.panel,t.strong_panel,theme.semantic.control.hover,theme.semantic.control.selected}) {
            Check(Contrast(t.text,bg,t.canvas)>=4.5F,"composited body 4.5");
            Check(Contrast(t.secondary,bg,t.canvas)>=4.5F,"composited secondary 4.5");
            Check(Contrast(t.boundary,bg,t.canvas)>=3.F,"actual boundary 3");
            Check(Contrast(t.focus,bg,t.canvas)>=3.F,"focus 3");
        }
        for (auto bg : {t.primary,t.primary_hover,t.primary_pressed}) Check(Contrast(t.on_primary,bg,t.canvas)>=4.5F,"primary text all states 4.5");
        for (auto status : {t.success,t.warning,t.error,t.info}) Check(Contrast(status.foreground,status.background,t.canvas)>=4.5F,"status text 4.5");
        for (auto fg : {t.link,t.script_structure,t.script_key,t.script_signature}) Check(Contrast(fg,t.panel,t.canvas)>=4.5F,"technical/link text 4.5");
        const auto gradient=IdentityGradient(dark);
        Check(gradient[1].position==(dark?.34F:.28F) && gradient[2].position==(dark?.68F:.58F),"exact identity gradient stops");
    }
    // Independent known alpha result: .5 white over .5 red over opaque black.
    const auto layered=Composite(Rgb(0xFFFFFF,.5F),Composite(Rgb(0xFF0000,.5F),Rgb(0)));
    Check(layered.x==.75F && layered.y==.5F && layered.z==.5F && layered.w==1,"nested alpha source-over");
    Check(Contrast(Rgb(0),Rgb(0xFFFFFF),Rgb(0))>20.99F,"WCAG known 21 ratio");
    for (auto choice : {Appearance::Light,Appearance::Dark,Appearance::System}) {
        Check(ParseAppearance(AppearanceId(choice))==choice,"appearance stable ID round-trip");
        Check(SaveAppearance(directory/"appearance",choice),"actual preference save");
        const auto load=LoadAppearance(directory/"appearance"); Check(load.valid && load.choice==choice,"actual reload survives independent read");
    }
    Check(!ParseAppearance("DARK") && !ParseAppearance("system\nprivate"),"malformed IDs rejected");
    Check(IsDark(Appearance::System,true) && !IsDark(Appearance::System,false),"System follows changing host observation");
    Check(!IsDark(Appearance::Light,true) && IsDark(Appearance::Dark,false),"explicit choice independent of OS");
    Check(LoadAppearance(directory/"absent").valid && LoadAppearance(directory/"absent").choice==Appearance::Light,"missing starts Light");
    { std::ofstream file(directory/"invalid");file<<"unknown"; }
    Check(!LoadAppearance(directory/"invalid").valid,"corruption observable");
    Check(!SaveAppearance(directory/"invalid"/"child",Appearance::Dark),"failed write reported");
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename=nullptr;
    ImGui_ImplNull_Init();
    ImGui_ImplNull_NewFrame(); ImGui::NewFrame();
    for (float scale : {1.F,1.5F,2.F,1.F,2.F,1.5F,1.F}) {
        const auto theme=Make(false,scale); Apply(theme);
        Check(theme.typography.body==14*scale && theme.metrics.controlHeight==32*scale,"DPI rebuild exact and noncumulative");
        Check(Equal(ImGui::GetStyle().Colors[ImGuiCol_Border],Wallet(false).boundary),"actual projected boundary opaque");
        const auto before=ImGui::GetStyle();
        { Scope first(Make(true,2)); const auto middle=ImGui::GetStyle();
          { Scope second(Make(false,1.5F)); Check(ImGui::GetStyle().FramePadding.y!=middle.FramePadding.y,"nested projection applies"); }
          Check(std::memcmp(&middle,&ImGui::GetStyle(),sizeof(ImGuiStyle))==0,"nested exact style/font restoration"); }
        Check(std::memcmp(&before,&ImGui::GetStyle(),sizeof(ImGuiStyle))==0,"outer exact style/font restoration");
    }
    ImGui::Render(); ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
    ImGui_ImplNull_Shutdown(); ImGui::DestroyContext();
    std::printf("selected=%d executed=%d passed=%d failed=%d skipped=0\n",checks,checks,checks-failures,failures);
    return failures?1:0;
}
