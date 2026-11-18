// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
namespace roots::gui::ui::components {
enum class NativeOutcome { Pending,Chosen,Cancelled,Failed,Unavailable,Stale };
struct FileChoice { NativeOutcome outcome{NativeOutcome::Unavailable};std::string path,detail; };
// Owner-thread port. Implementations own native data/processes, never editor
// buffers. A delayed paste must still match the original active editor state.
struct NativeInputPort {
    virtual ~NativeInputPort()=default;
    virtual void BeginView(std::string_view)=0;
    virtual void Privacy(bool)=0;
    virtual void PrivacyLock(){Privacy(true);}
    virtual void EnterEditor(uint32_t,bool sensitive)=0;
    virtual void LeaveEditor()=0;
    virtual std::optional<std::string> Paste(uint32_t,std::string_view,int,int,int,size_t)=0;
    virtual bool FileChooserAvailable() const=0;
    virtual bool NativeDialogActive() const { return false; }
    virtual uint64_t ChooseFile(std::string_view initial,bool save=false)=0;
    virtual FileChoice PollFile(uint64_t)=0;
};
inline thread_local NativeInputPort* native_input{};
struct NativeInputScope {
    NativeInputPort* previous;
    explicit NativeInputScope(NativeInputPort* port):previous(native_input){native_input=port;}
    ~NativeInputScope(){native_input=previous;}
    NativeInputScope(const NativeInputScope&)=delete;
    NativeInputScope& operator=(const NativeInputScope&)=delete;
};
}
