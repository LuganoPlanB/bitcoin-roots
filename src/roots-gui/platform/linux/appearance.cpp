// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "platform/linux/appearance.h"
#include <string_view>
#if defined(__linux__)
#include <dlfcn.h>
#endif
namespace roots::gui::platform {
struct SystemAppearance::Impl {
#if defined(__linux__)
    void* library{}; void* settings{};
    void* (*source)(){}; void* (*lookup)(void*, const char*, int){};
    int (*has_key)(void*, const char*){}; void* (*create)(void*, void*, const char*){};
    char* (*get_string)(void*, const char*){}; void (*free_string)(void*){};
    void (*unref)(void*){}; void (*schema_unref)(void*){}; int (*iterate)(void*, int){};
    ~Impl() { if (settings) unref(settings); if (library) dlclose(library); }
#endif
};
SystemAppearance::SystemAppearance() : m_impl(new Impl)
{
#if defined(__linux__)
    auto& p=*m_impl;
    p.library=dlopen("libgio-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!p.library) return;
#define ROOTS_GIO(member, name) p.member=reinterpret_cast<decltype(p.member)>(dlsym(p.library, name)); if (!p.member) return
    ROOTS_GIO(source,"g_settings_schema_source_get_default"); ROOTS_GIO(lookup,"g_settings_schema_source_lookup");
    ROOTS_GIO(has_key,"g_settings_schema_has_key"); ROOTS_GIO(create,"g_settings_new_full");
    ROOTS_GIO(get_string,"g_settings_get_string"); ROOTS_GIO(free_string,"g_free");
    ROOTS_GIO(unref,"g_object_unref"); ROOTS_GIO(schema_unref,"g_settings_schema_unref");
    ROOTS_GIO(iterate,"g_main_context_iteration");
#undef ROOTS_GIO
    auto* source=p.source(); if (!source) return;
    auto* schema=p.lookup(source,"org.gnome.desktop.interface",true); if (!schema) return;
    if (p.has_key(schema,"color-scheme")) p.settings=p.create(schema,nullptr,nullptr);
    p.schema_unref(schema);
#endif
}
SystemAppearance::~SystemAppearance() { delete m_impl; }
std::optional<bool> SystemAppearance::Dark()
{
#if defined(__linux__)
    auto& p=*m_impl;
    if (!p.settings) return {};
    // Drain only ready desktop events, with a bound; ImGui remains on its owner.
    for (int i=0; i<16 && p.iterate(nullptr,false); ++i) {}
    char* raw=p.get_string(p.settings,"color-scheme");
    if (!raw) return {};
    const std::string_view value(raw);
    const std::optional<bool> result=value=="prefer-dark" ? std::optional{true} :
        value=="prefer-light" || value=="default" ? std::optional{false} : std::nullopt;
    p.free_string(raw); return result;
#else
    return {};
#endif
}
}
