// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/qr.h"
#include "adapters/protocol/transaction.h"
#include "services/send_recipients.h"

#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>

using namespace roots::gui;
namespace p=protocol;
namespace {
std::string Read(const char* path)
{
    std::ifstream file(path,std::ios::binary);
    if (!file) throw std::runtime_error("Missing test input");
    return {std::istreambuf_iterator<char>(file),{}};
}
void Check(bool value)
{
    if (!value) throw std::runtime_error("QR probe invariant");
}
void Paint(const p::QrMatrix& matrix)
{
    constexpr int pitch=4, border=4;
    const int side=(matrix.size+2*border)*pitch;
    std::cout<<"P5\n"<<side<<' '<<side<<"\n255\n";
    for (int y=0;y<side;++y) for (int x=0;x<side;++x) {
        const int mx=x/pitch-border,my=y/pitch-border;
        const bool black=mx>=0 && my>=0 && mx<matrix.size && my<matrix.size
            && matrix.modules[size_t(my)*matrix.size+mx];
        std::cout.put(black ? '\0' : char(255));
    }
}
}
int main(int argc,char** argv)
{
    try {
        if (argc<3) return 2;
        const std::string_view operation=argv[1];
        if (operation=="paint") {
            const auto matrix=p::EncodeQr(Read(argv[2]));
            if (!matrix) return 1;
            Paint(*matrix);
        } else if (operation=="decode") {
            const auto raster=p::DecodeQrImage(Read(argv[2]));
            if (!raster) return 1;
            std::cout<<*raster;
        } else if (operation=="filter") {
            const auto source=p::ParseTransactionDocument(Read(argv[2]),p::DisplayNetwork::Regtest).document;
            if (!source) return 1;
            const auto copy=p::PsbtForQr(*source,p::DisplayNetwork::Regtest,true);
            if (!copy) return 1;
            std::cout<<copy->wire;
        } else if (operation=="payment") {
            const auto request=ParsePaymentRequest(Read(argv[2]),p::DisplayNetwork::Regtest);
            if (!request.error.empty()) {
                std::cerr<<request.error<<'\n';
                return 1;
            }
            std::cout<<request.address<<'\n';
            if (request.amount) std::cout<<request.amount->sats()<<'\n';
        } else if (operation=="collect") {
            p::QrAssembler collector;
            auto now=p::QrAssembler::Clock::now();
            for (int i=3;i<argc;++i) {
                const std::string_view token=argv[i];
                if (token=="--reset") {collector.Reset(); continue;}
                if (token=="--expire") {now+=p::QR_LIFETIME; continue;}
                const auto read=collector.Read(Read(argv[i]),now);
                switch (read.kind) {
                case p::QrRead::Kind::Error: std::cout<<read.error<<'\n'; break;
                case p::QrRead::Kind::Partial:
                    std::cout<<collector.Collected()<<'/'<<collector.Expected()<<'\n';
                    break;
                case p::QrRead::Kind::Psbt: {
                    const auto document=p::ParseTransactionDocument(read.payload,p::DisplayNetwork::Regtest).document;
                    Check(document && document->psbt_version);
                    std::ofstream(argv[2],std::ios::binary)<<document->wire;
                    std::cout<<"PSBT "<<*document->psbt_version<<'\n';
                    break;
                }
                case p::QrRead::Kind::Text: {
                    const auto document=p::ParseTransactionDocument(read.payload,p::DisplayNetwork::Regtest,{},true).document;
                    Check(document && document->psbt_version);
                    std::ofstream(argv[2],std::ios::binary)<<document->wire;
                    std::cout<<"PSBT "<<*document->psbt_version<<'\n';
                    break;
                }
                }
            }
        } else if (operation=="png-budget") {
            if (argc<4) return 2;
            const auto inspect=[](const std::string& bytes,bool expected) {
                roots_gui_qr_png_stats stats{};
                const auto decoded=p::DecodeQrImage(bytes,{},&stats);
                Check(bool(decoded)==expected && stats.decode_limit>0 && stats.expected_scanline_bytes>0
                    && stats.decode_limit<=ROOTS_GUI_QR_PNG_MAX_DECODE && stats.peak_bytes<=stats.decode_limit
                    && stats.decoder_bytes_at_return==0 && (!expected || stats.retained_luma_bytes>0)
                    && (expected || (stats.invalid_inflate && !stats.retained_luma_bytes)));
                return stats;
            };
            for (int i=2;i<argc;++i) {
                const bool expected=i==argc-1;
                const auto stats=inspect(Read(argv[i]),expected);
                Check(roots_gui_qr_png_active_bytes()==0);
                std::cout<<"{\"decoded\":"<<(expected?"true":"false")
                    <<",\"budgetBytes\":"<<stats.decode_limit<<",\"peakBytes\":"<<stats.peak_bytes
                    <<",\"scanlineLimit\":"<<stats.expected_scanline_bytes
                    <<",\"decoderBytesAtReturn\":"<<stats.decoder_bytes_at_return
                    <<",\"activeAfterRelease\":"<<roots_gui_qr_png_active_bytes()
                    <<",\"retainedLumaBytesBeforeRelease\":"<<stats.retained_luma_bytes
                    <<",\"refusedAllocations\":"<<stats.refused_allocations
                    <<",\"rejectedInflate\":"<<(stats.invalid_inflate?"true":"false")<<"}\n";
            }
            const auto valid=Read(argv[argc-1]);
            auto parallel=std::async(std::launch::async,[&] {return inspect(valid,true);});
            const auto current=inspect(valid,true);
            Check(parallel.get().peak_bytes==current.peak_bytes && roots_gui_qr_png_active_bytes()==0);
            std::cout<<"{\"parallelValidDecodes\":2,\"activeAfterJoin\":0}\n";
        } else if (operation=="bounds") {
            Check(!p::EncodeQr(std::string(p::MAX_QR_TEXT+1,'A')) && !p::EncodeQr(std::string("a\0b",3)));
            std::stop_source stop;
            stop.request_stop();
            const std::vector<uint8_t> empty(441,255);
            Check(!p::DecodeQrLuma(empty,21,21) && !p::DecodeQrLuma(empty,21,21,stop.get_token()));
            Check(!p::DecodeQrLuma(empty,p::MAX_QR_DIMENSION+1,21));
            Check(!p::DecodeQrImage(std::string(p::MAX_QR_IMAGE+1,'A')));
            auto oversized=std::string("psbt\xff",5)+std::string(p::MAX_QR_PSBT,'A');
            Check(!p::EncodePsbtQr(oversized,true,p::QrDensity::Dense).error.empty());
            const auto limited=p::EncodePsbtQr(oversized.substr(0,p::MAX_QR_PSBT),true,p::QrDensity::Compact);
            Check(limited.frames.empty() && limited.error.find("64 frames")!=std::string::npos);
            std::cout<<"bounds verified\n";
        } else return 2;
        return 0;
    } catch (const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 3;
    }
}
