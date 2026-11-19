// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/components/controls.h"
#include <algorithm>
namespace roots::gui::ui::components {
Edit::Edit(std::string_view initial) { if (!SetDraft(initial)) error="Value exceeds the editor limit."; else saved=initial; }
bool Edit::SetDraft(std::string_view value)
{
    if (value.size()>=text.size() || value.find('\0')!=std::string_view::npos) return false;
    std::fill(text.begin(),text.end(),0);std::copy(value.begin(),value.end(),text.begin());return true;
}
bool Edit::SetExternalDraft(std::string_view value) {if(!SetDraft(value))return false;external_buffer=std::string(value);return true;}
bool SetExternalBuffer(std::span<char> buffer,std::optional<std::string>& pending,std::string_view value) {
    if(value.size()>=buffer.size() || value.find('\0')!=std::string_view::npos)return false;
    std::fill(buffer.begin(),buffer.end(),0);std::copy(value.begin(),value.end(),buffer.begin());pending=std::string(value);return true;
}
bool TextInput(const char* id,const char* hint,char* buffer,size_t size,std::optional<std::string>& pending,ImGuiInputTextFlags flags,TextPosition* cursor,bool sensitive,ImVec2 multiline,bool protected_editor) {
    // ImGui owns a separate live edit buffer; use its public callback when an
    // external request changes an active editor, including initial focus.
    struct User {std::optional<std::string>& pending;TextPosition* cursor;NativeInputPort* port;ImGuiID id;};User user{pending,cursor,native_input,ImGui::GetID(id)};
    if(user.port){if(protected_editor)user.port->EnterProtectedEditor(user.id);else user.port->EnterEditor(user.id,sensitive);}
    if(cursor)*cursor={};
    const auto callback=[](ImGuiInputTextCallbackData* data){auto& user=*static_cast<User*>(data->UserData);auto& value=user.pending;
        if(value){data->DeleteChars(0,data->BufTextLen);data->InsertChars(0,value->c_str());}
        if(user.port){const bool selected=data->SelectionStart!=data->SelectionEnd;const int start=selected?std::min(data->SelectionStart,data->SelectionEnd):data->CursorPos,end=selected?std::max(data->SelectionStart,data->SelectionEnd):data->CursorPos;if(auto paste=user.port->Paste(user.id,{data->Buf,size_t(data->BufTextLen)},data->CursorPos,start,end,data->BufSize)){data->DeleteChars(start,end-start);data->InsertChars(start,paste->c_str());data->CursorPos=start+int(paste->size());data->SelectionStart=data->SelectionEnd=data->CursorPos;}}
        if(user.cursor)*user.cursor={data->CursorPos,std::min(data->SelectionStart,data->SelectionEnd),std::max(data->SelectionStart,data->SelectionEnd)};return 0;};
    flags|=ImGuiInputTextFlags_CallbackAlways;
    const bool result=multiline.y>0?ImGui::InputTextMultiline(id,buffer,size,multiline,flags,callback,&user):
        hint?ImGui::InputTextWithHint(id,hint,buffer,size,flags,callback,&user):ImGui::InputText(id,buffer,size,flags,callback,&user);
    if(user.port)user.port->LeaveEditor();pending.reset();return result;
}
bool EditInput(const char* id,Edit& edit,ImGuiInputTextFlags flags,bool sensitive) {return TextInput(id,nullptr,edit.text.data(),edit.text.size(),edit.external_buffer,flags,&edit.cursor,sensitive);}

namespace {
bool Finish(Edit& edit,Commands& host,Command command)
{
    if (!host.Request(command)) { edit.error="Changes could not be saved. Draft retained; retry with Enter.";return false; }
    edit.saved=edit.draft();edit.error.clear();return true;
}
}
std::optional<Money> ParseAmount(std::string_view text)
{
    auto value=Money::ParseBtc(text);
    if (!value || value->sats()<0 || value->sats()>2100000000000000LL) return {};
    return value;
}
bool CommitAmountEdit(const char* id,Edit& edit,Commands& host)
{
    if (!edit.dirty()) { edit.error.clear();return true; }
    const auto amount=ParseAmount(edit.draft());
    if (!amount) {
        edit.error="Enter a nonnegative BTC amount with at most 8 decimals, within the Bitcoin supply.";return false;
    }
    return Finish(edit,host,CommitAmount{id,*amount});
}
bool CommitFeeEdit(const char* id,Edit& edit,Commands& host)
{
    if (!edit.dirty()) { edit.error.clear();return true; }
    const auto fee=FeeRate::Parse(edit.draft());
    if (!fee) { edit.error="Enter a nonnegative sat/vB rate with at most 8 decimals.";return false; }
    return Finish(edit,host,CommitFee{id,*fee});
}
bool CommitLabelEdit(const char* id,Edit& edit,Commands& host)
{
    if (!edit.dirty()) { edit.error.clear();return true; }
    if (std::any_of(edit.draft().begin(),edit.draft().end(),[](unsigned char c) { return c<32 || c==127; })) {
        edit.error="Use a single-line label without control characters.";return false;
    }
    return Finish(edit,host,CommitLabel{id,std::string(edit.draft())});
}
}
