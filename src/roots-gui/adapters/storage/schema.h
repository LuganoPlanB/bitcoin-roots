// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once
#include <array>
#include <string_view>
namespace roots::gui::storage::schema {
inline constexpr int ApplicationId=0x52475549;
inline constexpr int Version=11;
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
inline constexpr std::string_view V4="CREATE TABLE recent_wallets(backend TEXT NOT NULL,genesis TEXT NOT NULL,wallet TEXT NOT NULL,position INTEGER NOT NULL UNIQUE CHECK(position BETWEEN 0 AND 15),PRIMARY KEY(backend,genesis,wallet)) STRICT";
inline constexpr std::array<std::string_view,2> V5{
    "CREATE TABLE receive_state(scope INTEGER PRIMARY KEY REFERENCES scopes(id),serial INTEGER NOT NULL CHECK(serial>0),pending INTEGER NOT NULL CHECK(pending IN(0,1,2))) STRICT",
    "CREATE TABLE receive_addresses(scope INTEGER NOT NULL REFERENCES receive_state(scope),serial INTEGER NOT NULL CHECK(serial>0),address TEXT NOT NULL,script TEXT NOT NULL,descriptor TEXT NOT NULL,key_path TEXT NOT NULL,observed INTEGER NOT NULL CHECK(observed>0),queued INTEGER NOT NULL CHECK(queued IN(0,1)),PRIMARY KEY(scope,serial),UNIQUE(scope,address)) STRICT"
};
inline constexpr std::string_view V6="CREATE TABLE rescan_state(scope INTEGER PRIMARY KEY REFERENCES scopes(id),serial INTEGER NOT NULL CHECK(serial>0),first INTEGER,last INTEGER,CHECK((first IS NULL AND last IS NULL) OR (first IS NOT NULL AND last IS NOT NULL AND first>=0 AND last>=first AND last<=2147483647))) STRICT";
inline constexpr std::string_view V7="CREATE TABLE broadcast_attempts(genesis TEXT NOT NULL CHECK(length(genesis)=64),txid TEXT NOT NULL CHECK(length(txid)=64),wtxid TEXT NOT NULL CHECK(length(wtxid)=64),wire_digest TEXT NOT NULL CHECK(length(wire_digest)=64),observed_ms INTEGER NOT NULL CHECK(observed_ms>0),PRIMARY KEY(genesis,txid)) STRICT";
inline constexpr std::string_view V8="CREATE TABLE broadcast_details(genesis TEXT NOT NULL,txid TEXT NOT NULL,wire TEXT NOT NULL CHECK(length(wire) BETWEEN 2 AND 131072),serial INTEGER NOT NULL CHECK(serial>0),attempted_ms INTEGER NOT NULL CHECK(attempted_ms>0),outcome INTEGER NOT NULL CHECK(outcome BETWEEN 0 AND 4),outcome_ms INTEGER CHECK(outcome_ms IS NULL OR outcome_ms>=attempted_ms),backend TEXT NOT NULL,wallet TEXT NOT NULL,PRIMARY KEY(genesis,txid),FOREIGN KEY(genesis,txid) REFERENCES broadcast_attempts(genesis,txid),CHECK((outcome=0)=(outcome_ms IS NULL))) STRICT";
inline constexpr std::string_view V9="CREATE TABLE general_settings(slot INTEGER PRIMARY KEY REFERENCES workspace(slot) CHECK(slot=1),trim_decimals INTEGER NOT NULL CHECK(trim_decimals IN(0,1)),group_outputs INTEGER NOT NULL CHECK(group_outputs IN(0,1)),explorer INTEGER NOT NULL CHECK(explorer BETWEEN 0 AND 2),explorer_template TEXT NOT NULL CHECK(length(explorer_template)<=512),explorer_genesis TEXT NOT NULL CHECK(length(explorer_genesis) IN(0,64))) STRICT";
inline constexpr std::string_view V10="CREATE TABLE desktop_settings(slot INTEGER PRIMARY KEY REFERENCES workspace(slot) CHECK(slot=1),notifications INTEGER NOT NULL CHECK(notifications IN(0,1)),camera_keep_awake INTEGER NOT NULL CHECK(camera_keep_awake IN(0,1))) STRICT";
inline constexpr std::string_view V11="CREATE TABLE wallet_icons(scope INTEGER PRIMARY KEY REFERENCES scopes(id),icon INTEGER NOT NULL CHECK(icon BETWEEN 0 AND 2)) STRICT";
}
