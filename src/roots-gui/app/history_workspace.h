// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include "app/document_registry.h"
#include "services/refresh.h"
#include "adapters/storage/metadata.h"
namespace roots::gui::app {
// A stable public endpoint identifier for local presentation metadata. This
// hash grants no authentication/session authority and contains no credentials.
std::optional<BackendSessionId> MetadataBackend(const ServerCandidate&);
std::optional<WalletBinding> VerifiedWalletBinding(const BackendCapabilities&);
storage::Scope HistoryMetadataScope(const WalletBinding&);
std::optional<RefreshIdentity> WalletRefreshIdentity(const BackendCapabilities&,uint64_t,const Document&,LockEpoch);
const Document* RegisteredWallet(const DocumentRegistry&,const WalletBinding&);
std::optional<RefreshView> HistoryForDocument(const Document&,const RefreshService&,LockEpoch);
std::optional<RefreshView> WalletBalanceForDocument(const Document&,const RefreshService&,LockEpoch);
std::vector<const HistoryTransaction*> OrderedHistory(const HistoryLedger&,const HistoryDocumentState&,const std::map<std::string,std::string>& labels,bool hidden=false);
std::string HistoryDate(int64_t unix_seconds,bool full=false);
const char* HistoryStateText(HistoryState);
std::string HistoryStatus(const HistoryLedger&,const HistoryTransaction&,bool current);
std::optional<std::string> ExplorerUrl(const NetworkIdentity&,const Hash256&);
std::optional<std::string> ExplorerUrl(const PublicPreferences&,const NetworkIdentity&,const Hash256&);
std::optional<std::string> HistoryCsv(const HistoryLedger&,std::span<const HistoryTransaction* const>,const std::map<std::string,std::string>& labels,bool hidden,bool state_current=true);
}
