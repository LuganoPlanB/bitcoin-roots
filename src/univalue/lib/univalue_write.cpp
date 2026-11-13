// Copyright 2014 BitPay Inc.
// Copyright (C) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/licenses/mit-license.php.

#include <univalue.h>
#include <univalue_escapes.h>

#include <string>
#include <vector>

static std::string json_escape(const std::string& inS)
{
    std::string outS;
    outS.reserve(inS.size() * 2);

    for (unsigned int i = 0; i < inS.size(); i++) {
        unsigned char ch = static_cast<unsigned char>(inS[i]);
        const char *escStr = escapes[ch];

        if (escStr)
            outS += escStr;
        else
            outS += static_cast<char>(ch);
    }

    return outS;
}

// NOLINTNEXTLINE(misc-no-recursion)
std::string UniValue::write(unsigned int prettyIndent,
                            unsigned int indentLevel) const
{
    std::string s;
    s.reserve(1024);

    unsigned int modIndent = indentLevel;
    if (modIndent == 0)
        modIndent = 1;

    switch (typ) {
    case VNULL:
        s += "null";
        break;
    case VOBJ:
        writeObject(prettyIndent, modIndent, s);
        break;
    case VARR:
        writeArray(prettyIndent, modIndent, s);
        break;
    case VSTR:
        s += "\"" + json_escape(val) + "\"";
        break;
    case VNUM:
        s += val;
        break;
    case VBOOL:
        s += (val == "1" ? "true" : "false");
        break;
    }

    return s;
}

static void json_write_quoted(const std::string& text, const std::function<void(std::string_view)>& sink)
{
    sink("\"");
    size_t first{0};
    for (size_t pos{0}; pos < text.size(); ++pos) {
        const char* escaped{escapes[static_cast<unsigned char>(text[pos])]};
        if (!escaped) continue;
        if (pos > first) sink(std::string_view{text}.substr(first, pos - first));
        sink(escaped);
        first = pos + 1;
    }
    if (first < text.size()) sink(std::string_view{text}.substr(first));
    sink("\"");
}

// NOLINTNEXTLINE(misc-no-recursion)
void UniValue::writeTo(const std::function<void(std::string_view)>& sink) const
{
    switch (typ) {
    case VNULL: sink("null"); break;
    case VBOOL: sink(val == "1" ? "true" : "false"); break;
    case VNUM: sink(val); break;
    case VSTR: json_write_quoted(val, sink); break;
    case VARR:
        sink("[");
        for (size_t i{0}; i < values.size(); ++i) {
            if (i) sink(",");
            values[i].writeTo(sink);
        }
        sink("]");
        break;
    case VOBJ:
        sink("{");
        for (size_t i{0}; i < keys.size(); ++i) {
            if (i) sink(",");
            json_write_quoted(keys[i], sink);
            sink(":");
            values.at(i).writeTo(sink);
        }
        sink("}");
        break;
    }
}

static void indentStr(unsigned int prettyIndent, unsigned int indentLevel, std::string& s)
{
    s.append(prettyIndent * indentLevel, ' ');
}

// NOLINTNEXTLINE(misc-no-recursion)
void UniValue::writeArray(unsigned int prettyIndent, unsigned int indentLevel, std::string& s) const
{
    s += "[";
    if (prettyIndent)
        s += "\n";

    for (unsigned int i = 0; i < values.size(); i++) {
        if (prettyIndent)
            indentStr(prettyIndent, indentLevel, s);
        s += values[i].write(prettyIndent, indentLevel + 1);
        if (i != (values.size() - 1)) {
            s += ",";
        }
        if (prettyIndent)
            s += "\n";
    }

    if (prettyIndent)
        indentStr(prettyIndent, indentLevel - 1, s);
    s += "]";
}

// NOLINTNEXTLINE(misc-no-recursion)
void UniValue::writeObject(unsigned int prettyIndent, unsigned int indentLevel, std::string& s) const
{
    s += "{";
    if (prettyIndent)
        s += "\n";

    for (unsigned int i = 0; i < keys.size(); i++) {
        if (prettyIndent)
            indentStr(prettyIndent, indentLevel, s);
        s += "\"" + json_escape(keys[i]) + "\":";
        if (prettyIndent)
            s += " ";
        s += values.at(i).write(prettyIndent, indentLevel + 1);
        if (i != (values.size() - 1))
            s += ",";
        if (prettyIndent)
            s += "\n";
    }

    if (prettyIndent)
        indentStr(prettyIndent, indentLevel - 1, s);
    s += "}";
}

