// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "ui/localization.h"

#include "app/history_workspace.h"

#include <cstdlib>

namespace roots::gui::ui::locale {
namespace {
struct Message {
    std::string_view english;
    std::string_view german;
    std::string_view pseudo;
};

constexpr Message CATALOG[] = {
    {"Transactions", "Transaktionen", "[Transaction history]"},
    {"Send", "Senden", "[Send payment]"},
    {"Receive", "Empfangen", "[Receive payment]"},
    {"Addresses", "Adressen", "[Wallet addresses]"},
    {"UTXOs", "UTXOs", "[Unspent outputs]"},
    {"Settings", "Einstellungen", "[Wallet settings]"},
    {"Payment recipient", "Zahlungsempfänger", "[Current payment recipient]"},
    {"Recipients", "Empfänger", "[Payment recipients]"},
    {"A comma or period marks decimals. Enter amounts without digit grouping.",
        "Komma oder Punkt trennt Dezimalstellen. Beträge ohne Tausendertrennzeichen eingeben.",
        "[Use a comma or period for decimals. Enter the amount without grouping.]"},
    {"Pay to", "Zieladresse", "[Payment destination]"},
    {"Apply destination", "Ziel übernehmen", "[Apply payment destination]"},
    {"Wallet address", "Wallet-Adresse", "[Open wallet address]"},
    {"Choose an open wallet address", "Adresse einer offenen Wallet wählen", "[Choose an address from an open wallet]"},
    {"Import QR", "QR importieren", "[Import payment QR]"},
    {"Import payment QR", "Zahlungs-QR importieren", "[Import payment QR code]"},
    {"Label (local)", "Bezeichnung (lokal)", "[Local payment label]"},
    {"Amount (BTC)", "Betrag (BTC)", "[Payment amount (BTC)]"},
    {"Amount (sats)", "Betrag (sats)", "[Payment amount (satoshis)]"},
    {"Use BTC", "BTC verwenden", "[Use bitcoin units]"},
    {"Use sats", "sats verwenden", "[Use satoshi units]"},
    {"Use satoshis", "Satoshis verwenden", "[Use satoshis for this payment]"},
    {"Max", "Maximum", "[Remaining funds]"},
    {"Unset Max", "Maximum abwählen", "[Clear remaining funds]"},
    {"Use remaining funds for this recipient", "Restbetrag für dieses Ziel verwenden", "[Use remaining funds for this recipient]"},
    {"Add recipient", "Empfänger hinzufügen", "[Add payment recipient]"},
    {"Remove selected recipient", "Ausgewählten Empfänger entfernen", "[Remove selected payment recipient]"},
    {"Apply", "Übernehmen", "[Apply changes]"},
    {"Cancel", "Abbrechen", "[Cancel this action]"},
    {"Signing review", "Signaturprüfung", "[Open transaction signing review]"},
    {"Record signing review", "Signaturprüfung erfassen", "[Record transaction review]"},
    {"Approve for signing", "Zum Signieren freigeben", "[Approve transaction for signing]"},
    {"Revoke signing review", "Signaturprüfung widerrufen", "[Revoke recorded signing review]"},
    {"Finalize transaction", "Transaktion fertigstellen", "[Finalize signed transaction]"},
    {"Sign with daemon", "Mit Daemon signieren", "[Sign transaction with daemon]"},
    {"Unlock daemon wallet", "Daemon-Wallet entsperren", "[Unlock the daemon wallet]"},
    {"Enrich PSBT", "PSBT ergänzen", "[Enrich transaction PSBT]"},
    {"Check node policy", "Node-Regeln prüfen", "[Check transaction against node policy]"},
    {"Broadcast", "Senden", "[Broadcast final transaction]"},
    {"Check Status", "Status prüfen", "[Check transaction status]"},
    {"Monitor transaction", "Transaktion überwachen", "[Monitor submitted transaction]"},
    {"Stop monitoring", "Überwachung beenden", "[Stop monitoring transaction]"},
    {"Cancel wait", "Warten abbrechen", "[Cancel pending wait]"},
    {"Hidden", "Ausgeblendet", "[Amount hidden]"},
    {"Unavailable", "Nicht verfügbar", "[Amount unavailable]"},
    {"Unknown date", "Unbekanntes Datum", "[Unknown observation date]"},
    {"Date (UTC)", "Datum (UTC)", "[Observation date (UTC)]"},
    {"Value", "Wert", "[Output value]"},
};

Language Category(const char* variable)
{
    // Read the environment without changing libc's process-wide locale. This
    // also keeps formatting predictable on hosts without generated locales.
    for (const auto* key : {"LC_ALL", variable, "LANG"}) {
        const auto* value = std::getenv(key);
        if (value && *value) return Select(value);
    }
    return Language::English;
}
} // namespace

Language Select(std::string_view setting)
{
    setting = setting.substr(0, setting.find_first_of(".@"));
    if (setting == "qps-ploc") return Language::Pseudo;
    for (const auto* supported : {"de", "de_DE", "de_AT", "de_CH", "de_BE", "de_LI", "de_LU",
                                  "de-DE", "de-AT", "de-CH", "de-BE", "de-LI", "de-LU"}) {
        if (setting == supported) return Language::German;
    }
    return Language::English;
}

Language Messages()
{
    static const auto language = Category("LC_MESSAGES");
    return language;
}

const char* LanguageTag()
{
    switch (Messages()) {
    case Language::German: return "de";
    case Language::Pseudo: return "qps-ploc";
    default: return "en";
    }
}

std::string_view Translate(std::string_view message, Language language)
{
    if (language == Language::English) return message;
    for (const auto& entry : CATALOG) {
        if (entry.english == message) return language == Language::German ? entry.german : entry.pseudo;
    }
    return message;
}

const char* Text(const char* message)
{
    return Translate(message, Messages()).data();
}

std::string Decimal(Money amount, Language language, bool trim)
{
    auto text = amount.btc();
    const auto point = text.find('.');
    if (trim) {
        const auto last = text.find_last_not_of('0');
        text.resize(last == point ? point : last + 1);
    }
    if (language == Language::German && point < text.size()) text[point] = ',';
    return text;
}

std::string Btc(Money amount, bool trim)
{
    static const auto language = Category("LC_NUMERIC");
    return Decimal(amount, language, trim);
}

std::string BothUnits(Money amount)
{
    return Btc(amount) + " BTC · " + std::to_string(amount.sats()) + (amount.sats() == 1 ? " sat" : " sats");
}

std::string Date(int64_t unix_seconds, bool full, Language language)
{
    const auto utc = app::HistoryDate(unix_seconds, full);
    if (utc == "Unknown date") return std::string(Translate(utc, language));
    if (language != Language::German || utc.size() < 10 || utc[4] != '-' || utc[7] != '-') return utc;
    return utc.substr(8, 2) + "." + utc.substr(5, 2) + "." + utc.substr(0, 4) + utc.substr(10);
}

std::string LocalDate(int64_t unix_seconds, bool full)
{
    static const auto language = Category("LC_TIME");
    return Date(unix_seconds, full, language);
}

} // namespace roots::gui::ui::locale
