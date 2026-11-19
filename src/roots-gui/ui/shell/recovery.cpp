// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/shell/workspace.h"
#include "ui/theme/theme.h"
#include <algorithm>
namespace roots::gui::ui::shell {
void DrawRecoveryDialog(State& s,app::Requests& host,Facts& f,const imkit::Theme& theme,float scale)
{
    if(!s.recovery || s.recovery->Locked() || !s.recovery_dialog.open)RetireMetadataBackup(s);
    if(!s.recovery || s.recovery->Locked())return;
    if(s.close_recovery_popup){if(ImGui::BeginPopupModal("Session Recovery###recovery-dialog")){ImGui::CloseCurrentPopup();ImGui::EndPopup();}s.close_recovery_popup=false;RestoreDocumentFocus(s,s.documents.ActiveId());}
    if(!s.recovery_dialog.open)return;
    const auto* vp=ImGui::GetMainViewport();const float width=std::min(520*scale,vp->WorkSize.x-32*scale);
    ImGui::SetNextWindowSizeConstraints({width,0},{width,std::max(1.F,vp->WorkSize.y-24*scale)});
    imkit::ComponentOptions options;options.theme=&theme;
    if(!imkit::BeginDialog("Session Recovery###recovery-dialog",s.recovery_dialog,options)){
        if(!s.recovery_dialog.open)RetireMetadataBackup(s);
        return;
    }
    s.presentation.DialogOnly();const auto p=ImGui::GetWindowPos(),extent=ImGui::GetWindowSize();s.presentation.Group("recovery-dialog","Session Recovery",components::ax::SemanticRole::Dialog,p,{p.x+extent.x,p.y+extent.y});
    ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x);
    if(DrawMetadataBackup(s,host,f,theme,scale)){
        ImGui::PopTextWrapPos();s.presentation.EndGroup();imkit::EndDialog();return;
    }
    const char* explanation=s.recovery->Protected()?"Local draft recovery is encrypted. Restored notes, tabs and drafts stay offline.":"Opt in to save GUI notes, exact coin choices and tabs locally. Restored drafts stay offline. Use Lock to protect this workspace.";
    ImGui::TextUnformatted(explanation);s.presentation.Annotate("recovery-explanation","Public recovery behavior",explanation);
    const char* distinction="To recover funds, use your daemon wallet backup or original signer recovery material. GUI recovery contains no private keys, credentials or transaction approvals.";
    ImGui::TextUnformatted(distinction);s.presentation.Annotate("wallet-recovery-boundary","Wallet recovery",distinction);
    if(s.recovery->Pending()){ImGui::TextUnformatted("A saved public workspace is available. Choose whether to restore it.");s.presentation.Annotate("recovery-prompt","Recovery choice","Saved public documents are available; no document or action has been restored.");}
    const auto action=[&](const char* id,const char* label,bool available,auto operation){
        ImGui::PushID(id);ImGui::BeginDisabled(!available);const bool pressed=theme::ActionButton(label,imkit::ActionVariant::Secondary,{0,30*scale},theme);
        const auto lo=ImGui::GetItemRectMin(),hi=ImGui::GetItemRectMax();f.document_actions[id]={{lo.x,lo.y,hi.x,hi.y},ImGui::GetItemID(),ImGui::IsItemFocused(),!available,false,pressed,ImGui::IsItemVisible()};
        const auto external=s.presentation.Interact(label,components::ax::SemanticRole::Button,components::ax::SemanticAction::Press|components::ax::SemanticAction::Focus,!available);ImGui::EndDisabled();ImGui::PopID();
        const bool accepted=pressed && available && operation();
        if(external)s.presentation.Defer(*external,[operation,pressed,accepted](const components::SemanticRequest&){return pressed?accepted:operation();},[&s,available]{return available && s.recovery_dialog.open;});
    };
    const auto finish=[&s]{s.recovery_dialog.open=false;s.close_recovery_popup=true;return true;};
    if(s.recovery->Pending()){
        action("restore-session","Restore public workspace",s.documents.Documents().empty(),[&s,finish]{if(!s.recovery->Restore(s.documents))return false;s.privacy.hidden=s.recovery->Hidden();s.unit=s.recovery->Unit();ApplyGeneralPresentation(s);return finish();});
        action("start-empty","Start empty; keep saved recovery",true,[&s,finish]{return s.recovery->StartEmpty() && finish();});
    }else{
        action("enable-recovery",s.recovery->Enabled()?"Save public workspace now":s.recovery->RetainedWorkspace()?"Replace saved workspace; enable recovery":"Enable public recovery",s.recovery->Error().empty(),[&s]{return s.recovery->Enabled()?s.recovery->Checkpoint(s.documents):s.recovery->Enable(s.documents);});
        const auto* d=s.documents.Active();const auto id=d?d->id:0;const bool unlocked=d && d->observation!=app::ObservationState::Locked;
        const bool is_send=d && d->SendWallet() && (d->coin_draft || d->section==app::Section::Send
            || d->recipients.Dirty() || !d->funding_rate.empty() || d->funding_target || d->funding_change || !d->selected_coins.empty() || !d->excluded_coins.empty());
        if(is_send)action("clear-public-draft","Clear Send",CanClearSend(s,id),[&s,id,finish]{return BeginClearSend(s,id) && finish();});
        else action("clear-public-draft","Clear public draft (recoverable)",s.recovery->Enabled() && unlocked,[&s,id]{return s.recovery->Clear(s.documents,id);});
        if(s.recovery->HasClearedSend())action("recover-send","Recover cleared Send",!d || unlocked,[&s,finish]{
            if(!s.recovery->RecoverSend(s.documents)){s.notice="The cleared Send draft is retained. Finish editing its current draft or make room to reopen it.";return false;}
            RestoreDocumentFocus(s,s.documents.ActiveId());return finish();
        });
        action("recover-last","Recover last Clear / close",s.recovery->Enabled() && s.recovery->HasRecovery() && (!d || unlocked),[&s]{return s.recovery->RecoverLast(s.documents);});
        action("disable-recovery","Disable and forget saved recovery",s.recovery->Enabled(),[&s]{return s.recovery->Disable();});
        if(CanOpenMetadataBackup(s)){
            action("backup-wallet-metadata","Back up local wallet metadata",true,[&s,&host]{return OpenMetadataBackup(s,host,MetadataBackupMode::Export);});
            action("restore-wallet-metadata","Restore local wallet metadata",true,[&s,&host]{return OpenMetadataBackup(s,host,MetadataBackupMode::Restore);});
        }else if(!storage::WalletMetadataBackupAvailable()){
            const char* unavailable="Encrypted wallet metadata archives need the Linux private-file implementation.";
            ImGui::TextUnformatted(unavailable);
            s.presentation.Annotate("metadata-unavailable","Archive dependency",unavailable);
        }
    }
    if(!s.recovery->Error().empty()){ImGui::TextWrapped("Recovery error: %s. Previous snapshot retained.",s.recovery->Error().c_str());s.presentation.Annotate("recovery-error","Recovery error",s.recovery->Error(),components::ax::SemanticRole::Alert);action("retry-recovery","Retry reading recovery",true,[&s]{return s.recovery->Retry();});}
    action("close-recovery","Close",true,finish);
    ImGui::PopTextWrapPos();s.presentation.EndGroup();imkit::EndDialog();
}
}
