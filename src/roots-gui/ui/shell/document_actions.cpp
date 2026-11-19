// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include <algorithm>
#include <filesystem>
namespace roots::gui::ui::shell {
namespace {
namespace a=app;
void Begin(State& s,DocumentDialog kind,uint64_t id) {
    s.actions=DocumentActions();s.actions.kind=kind;s.actions.target=id;s.actions.return_focus=s.documents.ActiveId();
    if(s.recovery)s.actions.metadata_epoch=s.recovery->Gate().Epoch();
    s.actions.dialog.open=true;s.actions.dialog.initialFocus=true;
}
components::ItemFacts Last() {const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();return {{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),bool(ImGui::GetItemFlags()&ImGuiItemFlags_Disabled),false,false,ImGui::IsItemVisible()};}
void Finish(State& s,bool native=true) {
    s.actions.dialog.open=false;
    if(native){s.actions.kind=DocumentDialog::None;RestoreDocumentFocus(s,s.actions.return_focus);ImGui::CloseCurrentPopup();}
    else s.close_document_popup=true;
}
bool CloseViews(State& s,std::span<const uint64_t> ids){const bool ok=s.recovery?s.recovery->Close(s.documents,ids):s.documents.Close(ids);if(!ok)s.actions.error=s.recovery?s.recovery->Error():"Document set changed.";return ok;}
bool DocumentActionAvailable(const State& s,const a::Requests& host,std::string_view id) {
    const auto& x=s.actions;const auto* d=s.documents.Find(x.target);
    if(x.kind==DocumentDialog::None || !x.dialog.open || s.close_document_popup)return false;
    if(id=="cancel")return true;
    if(x.kind!=DocumentDialog::Import && !d)return false;
    if(id=="confirm-clear-send")return x.kind==DocumentDialog::ClearSend && CanClearSend(s,x.target)
        && d->draft_revision==x.send_revision && s.recovery->Gate().Epoch()==x.send_epoch
        && s.coin_controls.Owner(x.target)==x.send_owner;
    if(id=="save-close" && x.kind==DocumentDialog::Close){for(auto key:x.close_ids){const auto* doc=s.documents.Find(key);if(!doc || (doc->Dirty() && !host.CanSubmit({a::Command::SaveDraft,key,doc->section,{}}).available))return false;}return !x.close_ids.empty();}
    if(id=="discard-close")return x.kind==DocumentDialog::Close && !x.close_ids.empty() && std::all_of(x.close_ids.begin(),x.close_ids.end(),[&](auto key){return s.documents.Find(key)!=nullptr;});
    if(id=="apply-name")return x.kind==DocumentDialog::Rename && !x.name.draft().empty() && x.name.draft().find_first_of("\r\n")==std::string_view::npos && host.CanSubmit({a::Command::DocumentMetadata,x.target,d->section,{},std::string(x.name.draft()),x.icon}).available;
    if(id=="confirm-delete")return x.kind==DocumentDialog::Delete && d->kind==a::DocumentKind::Wallet && host.CanSubmit({a::Command::DeleteWallet,x.target,d->section,{}}).available;
    if(id=="import")return x.kind==DocumentDialog::Import && !x.file_dialog && x.format>=0 && size_t(x.format)<x.candidate.formats.size() && host.CanSubmit({a::Command::ImportSource,0,a::Section::Settings,{},x.candidate.source,int(x.candidate.formats[x.format])}).available;
    return false;
}
bool ApplyDocumentAction(State& s,a::Requests& host,std::string_view id,bool native) {
    auto& x=s.actions;const auto* d=s.documents.Find(x.target);
    if(!DocumentActionAvailable(s,host,id))return false;
    if(x.kind==DocumentDialog::None)return false;
    if(id=="cancel"){Finish(s,native);return true;}
    if(x.kind!=DocumentDialog::Import && !d)return false;
    if(id=="confirm-clear-send"){if(!ConfirmClearSend(s,host))return false;Finish(s,native);return true;}
    if(id=="save-close" && x.kind==DocumentDialog::Close){
        for(auto key:x.close_ids){const auto* doc=s.documents.Find(key);if(!doc){x.error="Document set changed; cancel and try again.";return false;}
            if(doc->Dirty()){const a::Request request{a::Command::SaveDraft,key,doc->section,{}};const auto available=host.CanSubmit(request);if(!available.available){x.error=available.reason.empty()?"Draft persistence became unavailable; all views remain open.":available.reason;return false;}const auto receipt=host.Submit(request);if(!receipt.accepted){x.error=receipt.reason.empty()?"Draft persistence service is unavailable; all views remain open.":receipt.reason;return false;}}}
        if(!SaveReceiveLabelsBeforeClose(s,x.close_ids) || !CloseViews(s,x.close_ids))return false;x.return_focus=s.documents.ActiveId();Finish(s,native);return true;
    }
    if(id=="discard-close" && x.kind==DocumentDialog::Close){if(!CloseViews(s,x.close_ids))return false;x.return_focus=s.documents.ActiveId();Finish(s,native);return true;}
    if(id=="apply-name" && x.kind==DocumentDialog::Rename){
        if(x.name.draft().empty() || x.name.draft().find_first_of("\r\n")!=std::string_view::npos)return false;
        const auto receipt=host.Submit({a::Command::DocumentMetadata,x.target,d->section,{},std::string(x.name.draft()),x.icon});
        if(!receipt.accepted){x.error=receipt.reason.empty()?"Metadata service did not acknowledge the change.":receipt.reason;return false;}
        s.documents.Rename(x.target,std::string(x.name.draft()));s.documents.SetIcon(x.target,x.icon);Finish(s,native);return true;
    }
    if(id=="confirm-delete" && x.kind==DocumentDialog::Delete){
        const auto receipt=host.Submit({a::Command::DeleteWallet,x.target,d->section,{}});
        if(!receipt.accepted){
            x.error=receipt.reason.empty()?"Local wallet metadata could not be deleted.":receipt.reason;
            return false;
        }
        s.notice=receipt.reason;
        Finish(s,native);
        return true;
    }
    if(id=="import" && x.kind==DocumentDialog::Import && !x.file_dialog && x.format>=0 && size_t(x.format)<x.candidate.formats.size()){
        const auto receipt=host.Submit({a::Command::ImportSource,0,a::Section::Settings,{},x.candidate.source,int(x.candidate.formats[x.format])});x.error=receipt.reason.empty()?"Import adapter is unavailable; no file was read and no wallet was created.":receipt.reason;return receipt.accepted;
    }
    return false;
}
void Button(State& s,a::Requests& host,Facts& f,const char* id,const char* label,const imkit::Theme& theme,bool primary=false,bool enabled=true) {
    enabled=enabled && DocumentActionAvailable(s,host,id);
    ImGui::PushID(id);ImGui::BeginDisabled(!enabled);const bool pressed=theme::ActionButton(label,primary?imkit::ActionVariant::Primary:imkit::ActionVariant::Secondary,{},theme);
    f.document_actions[id]=Last();const auto external=s.presentation.Interact(label,components::ax::SemanticRole::Button,components::ax::SemanticAction::Press|components::ax::SemanticAction::Focus,!enabled);
    ImGui::EndDisabled();ImGui::PopID();const bool accepted=pressed && ApplyDocumentAction(s,host,id,true);
    if(external)s.presentation.Defer(*external,[&s,&host,id=std::string(id),pressed,accepted](const components::SemanticRequest&){return pressed?accepted:ApplyDocumentAction(s,host,id,false);},[&s,&host,id=std::string(id),pressed]{return pressed || DocumentActionAvailable(s,host,id);});
}
bool TabActionAvailable(const State& s,const a::Requests& host,uint64_t id,std::string_view label) {
    const auto* d=s.documents.Find(id);if(!d)return false;const auto& docs=s.documents.Documents();const auto pos=std::find_if(docs.begin(),docs.end(),[&](const auto& doc){return doc.id==id;})-docs.begin();
    if((s.privacy.hidden || d->observation==a::ObservationState::Locked)
        && (label=="Rename / Icon" || label=="Reset Name / Icon" || label=="Delete Wallet..."))return false;
    if(label=="Move Left")return pos>0;if(label=="Move Right")return pos+1<std::ptrdiff_t(docs.size());
    if(label=="Rename / Icon" || label=="Close" || label=="Close All")return true;
    if(label=="Close Others")return docs.size()>1;
    if(label=="Reset Name / Icon")return host.CanSubmit({a::Command::DocumentMetadata,id,d->section,{},d->original_name,0}).available;
    if(label=="Lock Wallet")return d->kind==a::DocumentKind::Wallet && host.CanSubmit({a::Command::LockCurrent,id,d->section,{}}).available;
    if(label=="Delete Wallet...")return d->kind==a::DocumentKind::Wallet;
    return false;
}
bool ApplyTabAction(State& s,a::Requests& host,uint64_t id,std::string_view label,bool native) {
    const auto* d=s.documents.Find(id);if(!d)return false;
    if(!TabActionAvailable(s,host,id,label)){s.notice=label=="Separate Wallet Window..."?"Separate wallet windows are unavailable: this native backend has one context/thread owner. The tab, drafts and selection remain here.":"Command is unavailable; the document is retained.";return false;}
    if(!native)s.close_tab_popup=id;++s.semantic_context_revision;
    if(label=="Move Left")return s.documents.Move(id,-1);
    if(label=="Move Right")return s.documents.Move(id,1);
    if(label=="Rename / Icon"){BeginRename(s,id);return s.actions.kind==DocumentDialog::Rename;}
    if(label=="Reset Name / Icon"){const auto name=d->original_name;const auto receipt=host.Submit({a::Command::DocumentMetadata,id,d->section,{},name,0});if(receipt.accepted){s.documents.Rename(id,name);s.documents.SetIcon(id,0);}else s.notice=receipt.reason;return receipt.accepted;}
    if(label=="Close")return BeginClose(s,id);
    if(label=="Close Others")return BeginClose(s,id,true);
    if(label=="Close All")return BeginClose(s,id,false,true);
    if(d->kind!=a::DocumentKind::Wallet)return false;
    if(label=="Lock Wallet"){if(s.recovery)return RequestPrivacyLock(s,host);const auto receipt=host.Submit({a::Command::LockCurrent,id,d->section,{}});s.notice=receipt.reason;return receipt.accepted;}
    if(label=="Delete Wallet..."){BeginDelete(s,id);return s.actions.kind==DocumentDialog::Delete;}
    if(label=="Separate Wallet Window...")s.notice="Separate wallet windows are unavailable: this native backend has one context/thread owner. The tab, drafts and selection remain here.";
    return false;
}
}
bool BeginClose(State& s,uint64_t id,bool others,bool all)
{
    const auto ids=s.documents.CloseTargets(id,others,all);if(ids.empty())return false;
    if(!AllowPolicyClose(s,ids) || !SaveReceiveLabelsBeforeClose(s,ids))return false;
    const bool dirty=std::any_of(ids.begin(),ids.end(),[&](auto key){return s.documents.Find(key)->Dirty();});
    if(!dirty){const bool closed=CloseViews(s,ids);if(!closed)s.notice=s.actions.error;RestoreDocumentFocus(s,s.documents.ActiveId());return closed;}
    Begin(s,DocumentDialog::Close,id);s.actions.close_ids=ids;return true;
}
void BeginRename(State& s,uint64_t id)
{
    const auto* d=s.documents.Find(id);if(!d)return;const auto name=d->name;const int icon=d->icon;
    Begin(s,DocumentDialog::Rename,id);s.actions.name=components::Edit(name);s.actions.icon=icon;
}
void BeginDelete(State& s,uint64_t id)
{
    const auto* d=s.documents.Find(id);if(!d || d->kind!=a::DocumentKind::Wallet){s.notice="Select a wallet tab to request wallet deletion.";return;}Begin(s,DocumentDialog::Delete,id);
}
bool BeginClearSend(State& s,uint64_t id)
{
    if (s.actions.kind!=DocumentDialog::None || !CanClearSend(s,id)) return false;
    Begin(s,DocumentDialog::ClearSend,id);
    s.actions.send_revision=s.documents.Active()->draft_revision;
    s.actions.send_epoch=s.recovery->Gate().Epoch();
    s.actions.send_owner=s.coin_controls.Owner(id);
    return true;
}
void BeginImport(State& s,std::string_view source)
{
    if(source.size()>4096)return;Begin(s,DocumentDialog::Import,0);
    std::copy(source.begin(),source.end(),s.actions.source.begin());s.actions.candidate=a::ClassifyImportPath(source);
    if(s.actions.candidate.formats.size()==1)s.actions.format=0;
}
void DrawTabActions(State& s,a::Requests& host,Facts& f,uint64_t id,const imkit::Theme&)
{
    const auto* d=s.documents.Find(id);if(!d)return;
    const auto& docs=s.documents.Documents();const auto index=std::find_if(docs.begin(),docs.end(),[&](const auto& item){return item.id==id;})-docs.begin();
    const auto item=[&](const char* label,bool enabled=true){const bool pressed=ImGui::MenuItem(label,nullptr,false,enabled);f.document_actions[label]=Last();
        const auto external=s.presentation.Interact(label,components::ax::SemanticRole::MenuItem,components::ax::SemanticAction::Press|components::ax::SemanticAction::Focus,!enabled || !TabActionAvailable(s,host,id,label));
        const bool accepted=pressed && ApplyTabAction(s,host,id,label,true);
        if(external)s.presentation.Defer(*external,[&s,&host,id,label=std::string(label),pressed,accepted](const components::SemanticRequest&){return pressed?accepted:ApplyTabAction(s,host,id,label,false);},[&s,&host,id,label=std::string(label),pressed]{return pressed || TabActionAvailable(s,host,id,label);});};
    const bool names_visible=!s.privacy.hidden && d->observation!=a::ObservationState::Locked;
    item("Move Left",index>0);item("Move Right",index+1<std::ptrdiff_t(docs.size()));item("Rename / Icon",names_visible);item("Reset Name / Icon",names_visible);item("Close");item("Close Others",docs.size()>1);item("Close All");
    d=s.documents.Find(id);item("Lock Wallet",d && d->kind==a::DocumentKind::Wallet);item("Delete Wallet...",names_visible && d && d->kind==a::DocumentKind::Wallet);item("Separate Wallet Window...",d && d->kind==a::DocumentKind::Wallet);
}
void ClosePendingDocumentPopup(State& s) {
    if(!s.close_document_popup)return;if(ImGui::BeginPopupModal("Document action###document-dialog")){ImGui::CloseCurrentPopup();ImGui::EndPopup();}
    s.close_document_popup=false;s.actions.kind=DocumentDialog::None;RestoreDocumentFocus(s,s.actions.return_focus);
}
void DrawDocumentDialog(State& s,a::Requests& host,Facts& f,const imkit::Theme& theme,float scale)
{
    auto& x=s.actions;if(x.kind==DocumentDialog::None)return;
    const auto* vp=ImGui::GetMainViewport();const float width=std::min(500*scale,vp->WorkSize.x-48*scale);
    ImGui::SetNextWindowSizeConstraints({width,0},{width,std::max(1.F,vp->WorkSize.y-32*scale)});
    imkit::ComponentOptions options;options.theme=&theme;
    if(!imkit::BeginDialog("Document action###document-dialog",x.dialog,options)) {
        if(!x.dialog.open){RestoreDocumentFocus(s,x.return_focus);x.kind=DocumentDialog::None;}return;
    }
    f.document_dialog_visible=true;s.presentation.DialogOnly();const auto origin=ImGui::GetWindowPos(),extent=ImGui::GetWindowSize();s.presentation.Group("semantic-dialog","Document action",components::ax::SemanticRole::Dialog,origin,{origin.x+extent.x,origin.y+extent.y});
    const auto* d=s.documents.Find(x.target);
    const bool stale=x.kind!=DocumentDialog::Import && !d;
    const float desired_height=(x.kind==DocumentDialog::ClearSend?86:220)*scale;
    const float body_height=std::min(desired_height,std::max(40*scale,vp->WorkSize.y-(x.error.empty()?210:280)*scale));
    ImGui::BeginChild("document-body",{0,body_height},ImGuiChildFlags_NavFlattened);
    ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x);
    if(stale)ImGui::TextUnformatted("The document is no longer open.");
    else if(x.kind==DocumentDialog::Close) {
        ImGui::TextUnformatted("Unsaved document drafts");
        const char* close_text=s.recovery && s.recovery->Enabled()?"Close saves a recoverable public copy first. Session Recovery can reopen the last closed document. No daemon wallet is deleted or locked.":"Save needs an acknowledged persistence service. Discard closes these views and their drafts; it never deletes or locks a daemon wallet.";
        ImGui::TextUnformatted(close_text);s.presentation.Annotate("close-explanation","Close behavior",close_text);
        for(auto id:x.close_ids)if(const auto* doc=s.documents.Find(id)){
            const auto name=s.privacy.hidden || doc->observation==a::ObservationState::Locked?"Document "+std::to_string(id):doc->name;
            ImGui::PushID(std::to_string(id).c_str());ImGui::TextUnformatted(name.c_str());s.presentation.Annotate("closing-name","Document with unsaved drafts",name);ImGui::PopID();
        }
    } else if(x.kind==DocumentDialog::ClearSend) {
        ImGui::TextUnformatted("Clear this Send draft?");
        const char* detail=s.recovery->Enabled()
            ? "Recipients, coin choices and fee settings will be cleared. Session Recovery keeps a copy of this Send draft."
            : "Recipients, coin choices and fee settings will be cleared. Session Recovery keeps a copy until you quit.";
        ImGui::TextUnformatted(detail);s.presentation.Annotate("clear-send-explanation","Clear this Send draft?",detail);
    } else if(x.kind==DocumentDialog::Rename) {
        ImGui::TextUnformatted("Rename / Icon");ImGui::TextUnformatted("Local display metadata only. This does not rename a daemon wallet directory.");s.presentation.Annotate("rename-explanation","Rename behavior","Local display metadata only. This does not rename a daemon wallet directory.");
        ImGui::TextUnformatted("Name");ImGui::SetNextItemWidth(-1);
        components::EditInput("###document-name",x.name);f.document_actions["name"]=Last();
        const auto edit=s.presentation.Interact("Document name",components::ax::SemanticRole::TextField,components::ax::SemanticAction::SetValue|components::ax::SemanticAction::Focus,false,false,x.name.draft(),"Use a nonempty single-line display name.",x.name.draft().empty() || x.name.draft().find_first_of("\r\n")!=std::string_view::npos,true);if(edit)s.presentation.Defer(*edit,[&x](const components::SemanticRequest& r){return x.name.SetExternalDraft(r.value);});
        s.presentation.BindTextPosition(f.document_actions.at("name").id,x.name.draft(),x.name.cursor);
        const char* icons[]{"Default","Circle","Diamond"};
        for(int i=0;i<3;++i){const bool pressed=ImGui::RadioButton(icons[i],x.icon==i);f.document_actions[icons[i]]=Last();
            const auto action=s.presentation.Interact(icons[i],components::ax::SemanticRole::Radio,components::ax::SemanticAction::Select|components::ax::SemanticAction::Focus,false,x.icon==i);if(pressed)x.icon=i;if(action)s.presentation.Defer(*action,[&x,i,pressed](const components::SemanticRequest&){if(!pressed)x.icon=i;return true;});}
    } else if(x.kind==DocumentDialog::Delete) {
        ImGui::TextUnformatted("Delete local wallet metadata");ImGui::TextUnformatted(d->name.c_str());s.presentation.Annotate("delete-target","Target document",d->name);
        const char* detail="Remove the saved display name, icon, labels, freezes and table settings for this wallet. The tab, drafts and transaction records are retained. No daemon wallet is unloaded or deleted.";
        ImGui::TextUnformatted(detail);s.presentation.Annotate("delete-explanation","Delete behavior",detail);
    } else {
        ImGui::TextUnformatted("Import public wallet / transaction");ImGui::TextUnformatted("Extension hints only. Choose a format; the service must validate the public file and network. No file is read or imported by this shell.");s.presentation.Annotate("import-explanation","Import behavior","Extension hints only. Choose a format; the service must validate the public file and network. No file is read or imported by this shell.");
        ImGui::TextUnformatted("Path");ImGui::SetNextItemWidth(-1);
        if(components::TextInput("###import-source",nullptr,x.source.data(),x.source.size(),x.external_source,0,&x.source_cursor)) {x.candidate=a::ClassifyImportPath(x.source.data());x.format=x.candidate.formats.size()==1?0:-1;x.error.clear();}
        f.document_actions["source"]=Last();const auto source=s.presentation.Interact("Public import path",components::ax::SemanticRole::TextField,components::ax::SemanticAction::SetValue|components::ax::SemanticAction::Focus,false,false,x.source.data(),"Public path only; import service must validate format and network.",x.candidate.formats.empty(),true);
        s.presentation.BindTextPosition(f.document_actions.at("source").id,x.source.data(),x.source_cursor);
        if(source)s.presentation.Defer(*source,[&x](const components::SemanticRequest& r){const bool accepted=components::SetExternalBuffer(x.source,x.external_source,r.value);if(accepted){x.candidate=a::ClassifyImportPath(x.source.data());x.format=x.candidate.formats.size()==1?0:-1;x.error.clear();}return accepted;});
        auto* input=components::native_input;
        if(x.file_dialog && input){const auto result=input->PollFile(x.file_dialog);if(result.outcome!=components::NativeOutcome::Pending){x.file_dialog=0;x.file_return_focus=true;if(result.outcome==components::NativeOutcome::Chosen){if(x.file_source==x.source.data() && x.file_format==x.format && components::SetExternalBuffer(x.source,x.external_source,result.path)){x.candidate=a::ClassifyImportPath(x.source.data());x.format=x.candidate.formats.size()==1?0:-1;x.error.clear();}else x.error="File choice completed after the path changed; current path retained.";}else if(result.outcome!=components::NativeOutcome::Cancelled)x.error=result.detail;}}
        const bool browse_available=input && input->FileChooserAvailable() && !x.file_dialog;
        ImGui::BeginDisabled(!browse_available);if(x.file_return_focus && browse_available){ImGui::SetWindowFocus();ImGui::SetKeyboardFocusHere();x.file_return_focus=false;}const bool browse=theme::ActionButton("Browse public file",imkit::ActionVariant::Secondary,{},theme);f.document_actions["browse"]=Last();
        const auto browse_action=s.presentation.Interact("Choose public file",components::ax::SemanticRole::Button,components::ax::SemanticAction::Press|components::ax::SemanticAction::Focus,!browse_available,false,{},browse_available?"":"Native file chooser unavailable or already pending.");ImGui::EndDisabled();
        const auto choose=[&x,input]{x.file_source=x.source.data();x.file_format=x.format;x.file_dialog=input->ChooseFile(x.file_source);if(!x.file_dialog)x.error="Native file chooser is busy or unavailable; current path retained.";return x.file_dialog!=0;};
        const bool native_chosen=browse && browse_available && choose();
        if(browse_action)s.presentation.Defer(*browse_action,[choose,browse,native_chosen](const components::SemanticRequest&){return browse?native_chosen:choose();},[&s,&x,input,browse]{return browse || (!s.close_document_popup && x.kind==DocumentDialog::Import && x.dialog.open && !x.file_dialog && input && input->FileChooserAvailable());});
        if(x.candidate.formats.empty()){ImGui::TextUnformatted("Unsupported or missing format. Supported hints: .psbt, .tx/.txn/.hex, .desc, .json, .txt.");s.presentation.Annotate("format-status","Import format status","Unsupported or missing format. Supported hints: .psbt, .tx/.txn/.hex, .desc, .json, .txt.",components::ax::SemanticRole::Alert);}
        else {
            ImGui::TextUnformatted(x.candidate.formats.size()>1?"Ambiguous format — choose explicitly:":"Format hint:");s.presentation.Annotate("format-status","Import format status",x.candidate.formats.size()>1?"Ambiguous format — choose explicitly:":"Format hint:");
            for(size_t i=0;i<x.candidate.formats.size();++i){const auto* name=a::FormatName(x.candidate.formats[i]);const bool pressed=ImGui::RadioButton(name,x.format==int(i));f.document_actions[name]=Last();
                const auto action=s.presentation.Interact(name,components::ax::SemanticRole::Radio,components::ax::SemanticAction::Select|components::ax::SemanticAction::Focus,false,x.format==int(i));if(pressed)x.format=int(i);if(action){const auto format=x.candidate.formats[i];s.presentation.Defer(*action,[&x,format,pressed](const components::SemanticRequest&){const auto current=std::find(x.candidate.formats.begin(),x.candidate.formats.end(),format);if(current==x.candidate.formats.end())return false;if(!pressed)x.format=int(current-x.candidate.formats.begin());return true;},[&s,&x,format,pressed]{return pressed || (!s.close_document_popup && x.dialog.open && x.kind==DocumentDialog::Import && std::find(x.candidate.formats.begin(),x.candidate.formats.end(),format)!=x.candidate.formats.end());});}}
        }
    }
    ImGui::PopTextWrapPos();ImGui::EndChild();
    if(!x.error.empty()){const auto detail=s.privacy.hidden?std::string("Document action failed. Result details hidden."):x.error;ImGui::BeginChild("document-error",{0,56*scale},ImGuiChildFlags_NavFlattened);ImGui::TextWrapped("%s",detail.c_str());ImGui::EndChild();s.presentation.Annotate("document-error","Document action status",detail,imkit::accessibility::SemanticRole::Alert);}
    Button(s,host,f,"cancel",stale?"Close":"Cancel",theme);if(!x.dialog.open){s.presentation.EndGroup();imkit::EndDialog();return;}
    if(!stale){ImGui::SameLine();
        if(x.kind==DocumentDialog::Close){Button(s,host,f,"save-close","Save and Close",theme,true);ImGui::SameLine();Button(s,host,f,"discard-close",s.recovery && s.recovery->Enabled()?"Close; keep recovery":"Discard Drafts and Close",theme);}
        else if(x.kind==DocumentDialog::Rename){const bool valid=!x.name.draft().empty() && x.name.draft().find_first_of("\r\n")==std::string_view::npos;Button(s,host,f,"apply-name","Apply Name / Icon",theme,true,valid);}
        else if(x.kind==DocumentDialog::Delete)Button(s,host,f,"confirm-delete","Delete Local Metadata",theme,true);
        else if(x.kind==DocumentDialog::ClearSend)Button(s,host,f,"confirm-clear-send","Confirm Clear",theme,true);
        else Button(s,host,f,"import","Request Import",theme,true,x.format>=0 && size_t(x.format)<x.candidate.formats.size());
    }
    s.presentation.EndGroup();imkit::EndDialog();
}
}
