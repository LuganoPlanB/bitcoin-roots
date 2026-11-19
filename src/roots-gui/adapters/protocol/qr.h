// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#pragma once

#include "adapters/protocol/qr_image.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace roots::gui::protocol {
inline constexpr int MAX_QR_VERSION=16;
inline constexpr size_t MAX_QR_TEXT=780;
inline constexpr size_t MAX_QR_PARTS=64;
inline constexpr size_t MAX_QR_PSBT=16*1024;
inline constexpr size_t MAX_QR_IMAGE=ROOTS_GUI_QR_PNG_MAX_FILE;
inline constexpr int MAX_QR_DIMENSION=ROOTS_GUI_QR_PNG_MAX_SIDE;
inline constexpr auto QR_LIFETIME=std::chrono::minutes(2);

struct QrMatrix {
    int size{};
    std::vector<uint8_t> modules;
};
std::optional<QrMatrix> EncodeQr(std::string_view);
std::optional<std::string> DecodeQrLuma(std::span<const uint8_t>,int width,int height,std::stop_token={});
std::optional<std::string> DecodeQrImage(std::string_view,std::stop_token={},roots_gui_qr_png_stats* diagnostics=nullptr);

enum class QrDensity { Compact, Standard, Dense };
struct QrExport {
    std::string encoding,error;
    std::vector<QrMatrix> frames;
    size_t bytes{};
};
QrExport EncodePsbtQr(std::string_view wire,bool animated,QrDensity);

struct QrRead {
    enum class Kind { Error, Partial, Text, Psbt };
    Kind kind{Kind::Error};
    std::string payload,error;
};
class QrAssembler {
public:
    using Clock=std::chrono::steady_clock;
    QrRead Read(std::string_view,Clock::time_point now=Clock::now());
    void Reset();
    bool Expired(Clock::time_point now=Clock::now()) const;
    size_t Collected() const {return collected;}
    size_t Expected() const {return parts.size();}
private:
    Clock::time_point started{};
    std::vector<std::optional<std::string>> parts;
    size_t collected{},length{},part_size{},observations{};
    uint32_t session{};
};
}
