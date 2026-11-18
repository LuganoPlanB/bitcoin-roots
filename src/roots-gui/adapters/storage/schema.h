// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <array>
#include <string_view>
namespace roots::gui::storage::schema {
inline constexpr int ApplicationId=0x52475549;
inline constexpr int Version=3;
// Version1 owns user metadata. Version2 adds an independent observation cache.
// Exact definitions also identify supported schemas; unknown schemas fail closed.
inline constexpr std::array<std::string_view,8> V1{
    "CREATE TABLE scopes(id INTEGER PRIMARY KEY,backend TEXT NOT NULL,genesis TEXT NOT NULL,wallet TEXT NOT NULL,revision INTEGER NOT NULL CHECK(revision>=0),display_name TEXT NOT NULL,UNIQUE(backend,genesis,wallet)) STRICT",
    "CREATE TABLE labels(scope INTEGER NOT NULL REFERENCES scopes(id),kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 2),target TEXT NOT NULL,label TEXT NOT NULL,PRIMARY KEY(scope,kind,target)) STRICT",
    "CREATE TABLE layouts(scope INTEGER NOT NULL REFERENCES scopes(id),table_id TEXT NOT NULL,sort_column TEXT NOT NULL,descending INTEGER NOT NULL CHECK(descending IN(0,1)),PRIMARY KEY(scope,table_id)) STRICT",
    "CREATE TABLE widths(scope INTEGER NOT NULL,table_id TEXT NOT NULL,column_id TEXT NOT NULL,width INTEGER NOT NULL CHECK(width BETWEEN 16 AND 8192),PRIMARY KEY(scope,table_id,column_id),FOREIGN KEY(scope,table_id) REFERENCES layouts(scope,table_id)) STRICT",
    "CREATE TABLE freezes(scope INTEGER NOT NULL REFERENCES scopes(id),kind INTEGER NOT NULL CHECK(kind IN(0,2)),target TEXT NOT NULL,PRIMARY KEY(scope,kind,target)) STRICT",
    "CREATE TABLE drafts(scope INTEGER NOT NULL REFERENCES scopes(id),document TEXT NOT NULL,revision TEXT NOT NULL,note TEXT NOT NULL,amount INTEGER,PRIMARY KEY(scope,document)) STRICT",
    "CREATE TABLE workspace(slot INTEGER PRIMARY KEY CHECK(slot=1),revision INTEGER NOT NULL CHECK(revision>0),refresh INTEGER NOT NULL,hidden INTEGER NOT NULL CHECK(hidden IN(0,1)),restore INTEGER NOT NULL CHECK(restore IN(0,1)),appearance INTEGER NOT NULL CHECK(appearance BETWEEN 0 AND 2),unit INTEGER NOT NULL CHECK(unit BETWEEN 0 AND 2)) STRICT",
    "CREATE TABLE profiles(id TEXT PRIMARY KEY,revision INTEGER NOT NULL CHECK(revision>0),genesis TEXT NOT NULL,wallet TEXT,host TEXT NOT NULL,port INTEGER NOT NULL CHECK(port BETWEEN 1 AND 65535),tls INTEGER NOT NULL CHECK(tls IN(0,1)),ca TEXT NOT NULL,proxy INTEGER NOT NULL CHECK(proxy BETWEEN 0 AND 2),proxy_host TEXT NOT NULL,proxy_port INTEGER NOT NULL CHECK(proxy_port BETWEEN 1 AND 65535),credential_ref TEXT NOT NULL) STRICT"
};
inline constexpr std::string_view V2="CREATE TABLE observations(scope INTEGER NOT NULL REFERENCES scopes(id),key TEXT NOT NULL,backend_session TEXT NOT NULL,wallet_id TEXT NOT NULL,document TEXT NOT NULL,revision TEXT NOT NULL,epoch TEXT NOT NULL,source_freshness INTEGER NOT NULL CHECK(source_freshness BETWEEN 0 AND 4),observed_ms INTEGER NOT NULL CHECK(observed_ms>0),height INTEGER,amount INTEGER,PRIMARY KEY(scope,key)) STRICT";
inline constexpr std::string_view V3="CREATE TABLE recovery(slot INTEGER PRIMARY KEY CHECK(slot=1),revision INTEGER NOT NULL CHECK(revision>0),enabled INTEGER NOT NULL CHECK(enabled IN(0,1)),public_documents TEXT NOT NULL) STRICT";
}
