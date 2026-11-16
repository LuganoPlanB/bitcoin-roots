// Original reusable headless runner. Register the application's real panels in tests.cpp.
#include "imgui.h"
#include "imgui_impl_null.h"
#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_exporters.h"
#include <charconv>
#include <cstdio>
#include <cstring>

void RegisterHeadlessTests(ImGuiTestEngine* engine);

int main(int argc, char** argv)
{
    int expected = 0;
    if (argc != 4 && argc != 5)
    {
        std::fprintf(stderr, "usage: ui_tests FILTER REPORT.xml EXPECTED_COUNT\n");
        return 2;
    }
    const char* end = argv[3] + std::strlen(argv[3]);
    const auto parsed = std::from_chars(argv[3], end, expected);
    if (!argv[1][0] || !argv[2][0] || parsed.ec != std::errc{} ||
        parsed.ptr != end || expected <= 0 || (argc == 5 && std::strcmp(argv[4], "undrained") != 0))
        return 2;

    // Check writability and truncate a previous report so stale results cannot pass.
    FILE* report = std::fopen(argv[2], "wb");
    if (!report)
    {
        std::perror("Cannot write JUnit report");
        return 2;
    }
    std::fclose(report);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (!ImGui_ImplNull_Init())
    {
        ImGui::DestroyContext();
        return 2;
    }
    ImGuiTestEngine* engine = ImGuiTestEngine_CreateContext();
    ImGuiTestEngineIO& config = ImGuiTestEngine_GetIO(engine);
    config.ConfigSavedSettings = false;
    config.ConfigBreakOnError = false;
    config.ConfigCaptureEnabled = false;
    config.ConfigRunSpeed = ImGuiTestRunSpeed_Fast;
    config.ConfigFixedDeltaTime = 1.0f / 60.0f;
    config.ConfigLogToTTY = true;
    config.ConfigVerboseLevel = ImGuiTestVerboseLevel_Error;
    config.ConfigVerboseLevelOnError = ImGuiTestVerboseLevel_Info;
    config.ExportResultsFilename = argv[2];
    config.ExportResultsFormat = ImGuiTestEngineExportFormat_JUnitXml;
    RegisterHeadlessTests(engine);
    ImGuiTestEngine_Start(engine, ImGui::GetCurrentContext());
    ImGuiTestEngine_QueueTests(engine, ImGuiTestGroup_Unknown, argv[1]);
    const int frame_limit = argc == 5 ? 0 : 5000;
    int frames = 0;
    while (!ImGuiTestEngine_IsTestQueueEmpty(engine) && frames < frame_limit)
    {
        ImGui_ImplNull_NewFrame();
        ImGui::NewFrame();
        ImGui::Render();
        ImGui_ImplNullRender_RenderDrawData(ImGui::GetDrawData());
        ImGuiTestEngine_PreSwap(engine);
        ImGuiTestEngine_PostSwap(engine);
        ++frames;
    }
    const bool drained = ImGuiTestEngine_IsTestQueueEmpty(engine);
    ImGuiTestEngineResultSummary result;
    ImGuiTestEngine_GetResultSummary(engine, &result);
    if (!drained)
        ImGuiTestEngine_TryAbortEngine(engine);
    bool passed = drained && result.CountTested == expected &&
        result.CountSuccess == expected && result.CountInQueue == 0;
    ImGuiTestEngine_Stop(engine); // Exports JUnit and joins the test coroutine.
    ImGui_ImplNull_Shutdown();
    ImGui::DestroyContext();
    ImGuiTestEngine_DestroyContext(engine);
    // A passing run must leave its report, including on unattended hosts.
    report = std::fopen(argv[2], "rb");
    if (!report)
        passed = false;
    else
    {
        if (std::fgetc(report) == EOF)
            passed = false;
        std::fclose(report);
    }
    std::printf("%s filter=%s tested=%d passed=%d expected=%d remaining=%d frames=%d report=%s\n",
        passed ? "PASS" : "FAIL", argv[1], result.CountTested, result.CountSuccess,
        expected, result.CountInQueue, frames, argv[2]);
    return passed ? 0 : 1;
}
