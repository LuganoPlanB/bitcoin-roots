// Copyright (c) 2026 Plan-B Foundation
// Distributed under the MIT software license, see the accompanying COPYING file.
#include "adapters/protocol/qr.h"
#include "adapters/protocol/transaction.h"
#include "third_party/qr/nayuki/qrcodegen.h"
#include "third_party/qr/quirc/quirc.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <memory>
#include <utility>

namespace roots::gui::protocol {
namespace {
constexpr std::string_view PSBT_MAGIC{"psbt\xff",5};
constexpr std::string_view UR_PREFIX="ur:psbt/";
constexpr size_t MAX_FRAGMENT=320;
constexpr size_t MAX_ATTEMPTS=MAX_QR_PARTS*5;

// BCR-2020-012 specifies the alphabet in byte order. Only the first
// and last characters of each word are used in minimal Bytewords.
constexpr std::string_view WORDS=
#include "third_party/qr/bytewords.inc"
;
static_assert(WORDS.size()==256*4);

uint32_t Crc32(std::string_view bytes)
{
    uint32_t crc=0xffffffff;
    for (unsigned char byte:bytes) {
        crc^=byte;
        for (int bit=0;bit<8;++bit) crc=(crc>>1)^((crc&1) ? 0xedb88320 : 0);
    }
    return ~crc;
}
void PutBig(std::string& out,uint32_t n,int bytes)
{
    for (int remaining=bytes-1;remaining>=0;--remaining) out+=char(n>>(remaining*8));
}
void Cbor(std::string& out,unsigned type,uint32_t n)
{
    const auto prefix=type<<5;
    if (n<24) out+=char(prefix|n);
    else if (n<=0xff) {out+=char(prefix|24); PutBig(out,n,1);}
    else if (n<=0xffff) {out+=char(prefix|25); PutBig(out,n,2);}
    else {out+=char(prefix|26); PutBig(out,n,4);}
}
std::optional<uint32_t> Cbor(std::string_view& data,unsigned type)
{
    if (data.empty() || (uint8_t(data.front())>>5)!=type) return {};
    const unsigned tag=uint8_t(data.front())&31;
    data.remove_prefix(1);
    if (tag<24) return tag;
    if (tag>26) return {};
    const size_t bytes=size_t(1)<<(tag-24);
    if (data.size()<bytes) return {};
    uint32_t value{};
    for (unsigned char c:data.substr(0,bytes)) value=(value<<8)|c;
    if (value<(bytes==1 ? 24U : bytes==2 ? 256U : 65536U)) return {};
    data.remove_prefix(bytes);
    return value;
}
std::string Bytewords(std::string bytes)
{
    PutBig(bytes,Crc32(bytes),4);
    std::string out;
    out.reserve(bytes.size()*2);
    for (unsigned char byte:bytes) {
        out+=WORDS[byte*4];
        out+=WORDS[byte*4+3];
    }
    return out;
}
std::optional<std::string> ReadBytewords(std::string_view input)
{
    if (input.size()<10 || input.size()%2 || input.size()>MAX_QR_TEXT) return {};
    static const auto lookup=[] {
        std::array<int16_t,26*26> table;
        table.fill(-1);
        for (size_t byte=0;byte<256;++byte)
            table[(WORDS[byte*4]-'a')*26+WORDS[byte*4+3]-'a']=int16_t(byte);
        return table;
    }();
    std::string out;
    out.reserve(input.size()/2);
    for (size_t n=0;n<input.size();n+=2) {
        const auto a=input[n],b=input[n+1];
        if (a<'a' || a>'z' || b<'a' || b>'z') return {};
        const auto value=lookup[(a-'a')*26+b-'a'];
        if (value<0) return {};
        out+=char(value);
    }
    uint32_t checksum{};
    for (unsigned char byte:std::string_view(out).substr(out.size()-4)) checksum=(checksum<<8)|byte;
    out.resize(out.size()-4);
    if (Crc32(out)!=checksum) return {};
    return out;
}
std::string Upper(std::string value)
{
    for (auto& c:value) if (c>='a' && c<='z') c-=32;
    return value;
}
std::string Lower(std::string_view text)
{
    std::string value(text);
    for (auto& c:value) if (c>='A' && c<='Z') c+=32;
    return value;
}
bool Pixels(int width,int height)
{
    return width>=21 && height>=21 && width<=MAX_QR_DIMENSION && height<=MAX_QR_DIMENSION;
}
std::optional<uint32_t> Sequence(std::string_view text)
{
    if (text.empty() || text.front()=='0') return {};
    uint32_t value{};
    const auto end=text.data()+text.size();
    const auto parsed=std::from_chars(text.data(),end,value);
    return parsed.ec==std::errc{} && parsed.ptr==end ? std::optional{value} : std::nullopt;
}
QrRead Error(std::string reason)
{
    return {.error=std::move(reason)};
}
QrRead Psbt(std::string_view cbor)
{
    const auto size=Cbor(cbor,2);
    if (!size || *size>MAX_QR_PSBT || *size!=cbor.size() || !cbor.starts_with(PSBT_MAGIC))
        return Error("UR psbt must contain one exact binary PSBT.");
    return {QrRead::Kind::Psbt,std::string(cbor)};
}
}

std::optional<QrMatrix> EncodeQr(std::string_view text)
{
    if (text.empty() || text.size()>MAX_QR_TEXT || text.find('\0')!=text.npos) return {};
    std::array<uint8_t,qrcodegen_BUFFER_LEN_FOR_VERSION(MAX_QR_VERSION)> code{},scratch{};
    const std::string source(text);
    if (!qrcodegen_encodeText(source.c_str(),scratch.data(),code.data(),qrcodegen_Ecc_MEDIUM,1,MAX_QR_VERSION,
        qrcodegen_Mask_AUTO,true)) return {};
    const int size=qrcodegen_getSize(code.data());
    QrMatrix result{size,{}};
    result.modules.reserve(size_t(size)*size);
    for (int y=0;y<size;++y)
        for (int x=0;x<size;++x) result.modules.push_back(qrcodegen_getModule(code.data(),x,y));
    return result;
}

std::optional<std::string> DecodeQrLuma(std::span<const uint8_t> luma,int width,int height,std::stop_token stop)
{
    if (stop.stop_requested() || !Pixels(width,height) || luma.size()!=size_t(width)*height) return {};
    std::unique_ptr<quirc,decltype(&quirc_destroy)> reader(quirc_new(),quirc_destroy);
    if (!reader || quirc_resize(reader.get(),width,height)!=0) return {};
    auto* image=quirc_begin(reader.get(),nullptr,nullptr);
    std::ranges::copy(luma,image);
    quirc_end(reader.get());
    if (stop.stop_requested() || quirc_count(reader.get())!=1) return {};
    quirc_code code{};
    quirc_data decoded{};
    quirc_extract(reader.get(),0,&code);
    if (quirc_decode(&code,&decoded)!=QUIRC_SUCCESS || decoded.payload_len<=0 ||
        size_t(decoded.payload_len)>MAX_QR_TEXT) return {};
    return std::string(reinterpret_cast<const char*>(decoded.payload),size_t(decoded.payload_len));
}

std::optional<std::string> DecodeQrImage(std::string_view bytes,std::stop_token stop,roots_gui_qr_png_stats* diagnostics)
{
    if (diagnostics) *diagnostics={};
    if (stop.stop_requested() || bytes.empty() || bytes.size()>MAX_QR_IMAGE) return {};
    const auto* input=reinterpret_cast<const unsigned char*>(bytes.data());
    int width{},height{};
    std::unique_ptr<unsigned char,decltype(&roots_gui_qr_png_free)> luma(
        roots_gui_qr_png_decode(input,bytes.size(),&width,&height,diagnostics),roots_gui_qr_png_free);
    if (!luma || stop.stop_requested()) return {};
    return DecodeQrLuma(std::span(luma.get(),size_t(width)*height),width,height,stop);
}

QrExport EncodePsbtQr(std::string_view wire,bool animated,QrDensity density)
{
    if (wire.size()>MAX_QR_PSBT) return {.error="The PSBT exceeds the 16 KiB QR limit. Save the PSBT to a file."};
    if (!wire.starts_with(PSBT_MAGIC)) return {.error="Select a valid PSBT for QR export."};
    if (!animated) {
        if (auto matrix=EncodeQr(TransactionBase64(wire))) return {"Static QR · Base64 PSBT",{}, {std::move(*matrix)},wire.size()};
    }
    constexpr std::array<size_t,3> target{130,200,285};
    std::string message;
    Cbor(message,2,uint32_t(wire.size()));
    message+=wire;
    const auto checksum=Crc32(message);
    const size_t limit=target.at(size_t(density));
    const size_t count=(message.size()+limit-1)/limit;
    if (count>MAX_QR_PARTS) return {.error="This density needs more than 64 frames. Increase density or save the PSBT to a file."};
    QrExport out{"Sequential UR v2 · psbt",{}, {},wire.size()};
    const auto size=(message.size()+count-1)/count;
    for (size_t index=0;index<count;++index) {
        std::string prefix(UR_PREFIX);
        std::string payload;
        if (count==1) payload=message;
        else {
            prefix+=std::to_string(index+1)+"-"+std::to_string(count)+"/";
            Cbor(payload,4,5);
            Cbor(payload,0,uint32_t(index+1)); Cbor(payload,0,uint32_t(count));
            Cbor(payload,0,uint32_t(message.size())); Cbor(payload,0,checksum);
            Cbor(payload,2,uint32_t(size));
            const auto part=std::string_view(message).substr(index*size,size);
            payload+=part;
            payload.append(size-part.size(),'\0');
        }
        auto matrix=EncodeQr(Upper(prefix+Bytewords(std::move(payload))));
        if (!matrix) return {.error="This QR density cannot display the selected PSBT."};
        out.frames.push_back(std::move(*matrix));
    }
    return out;
}

bool QrAssembler::Expired(Clock::time_point now) const
{
    return !parts.empty() && (now<started || now-started>=QR_LIFETIME);
}

void QrAssembler::Reset()
{
    *this=QrAssembler{};
}

QrRead QrAssembler::Read(std::string_view text,Clock::time_point now)
{
    if (Expired(now) || ++observations>MAX_ATTEMPTS)
        return Error("The QR assembly limit expired. Restart the scan to discard the retained frames.");
    if (text.empty() || text.size()>MAX_QR_TEXT ||
        std::ranges::any_of(text,[](unsigned char c){return c<32 || c>126;}))
        return Error("The QR text is empty, too large or contains unsupported characters.");
    const auto lowered=Lower(text);
    if (!lowered.starts_with("ur:")) {
        if (!parts.empty()) return Error("A different QR stream is already open. Restart to import this code.");
        return {QrRead::Kind::Text,std::string(text)};
    }
    if (!lowered.starts_with(UR_PREFIX)) return Error("Only sequential UR v2 psbt is supported. This UR type is unavailable.");
    auto suffix=std::string_view(lowered).substr(UR_PREFIX.size());
    const auto slash=suffix.find('/');
    if (slash==suffix.npos) {
        if (!parts.empty()) return Error("A different QR stream is already open. Restart to import this code.");
        const auto raw=ReadBytewords(suffix);
        return raw ? Psbt(*raw) : Error("UR Bytewords data or checksum is invalid.");
    }
    auto seq=suffix.substr(0,slash);
    const auto hyphen=seq.find('-');
    const auto index=Sequence(seq.substr(0,hyphen));
    const auto count=hyphen==seq.npos ? std::nullopt : Sequence(seq.substr(hyphen+1));
    if (!index || !count || *count<2 || *count>MAX_QR_PARTS)
        return Error("The UR fragment sequence is invalid or exceeds the 64-frame limit.");
    if (*index>*count) return Error("Mixed fountain fragments are unavailable. Scan the indexed frames of the sequence.");
    const auto raw=ReadBytewords(suffix.substr(slash+1));
    if (!raw) return Error("UR Bytewords data or checksum is invalid.");
    std::string_view fields=*raw;
    const auto array=Cbor(fields,4), ordinal=Cbor(fields,0), total=Cbor(fields,0),
        message=Cbor(fields,0), crc=Cbor(fields,0), fragment=Cbor(fields,2);
    if (array!=5 || ordinal!=index || total!=count || !message || !*message || *message>MAX_QR_PSBT+5 ||
        !crc || !fragment || !*fragment || *fragment>MAX_FRAGMENT || *fragment!=fields.size() ||
        (*count-1)*(*fragment)>=*message || *count*(*fragment)<*message)
        return Error("The UR fragment header or size is invalid.");
    if (parts.empty()) {
        started=now;
        parts.resize(*count);
        length=*message;
        session=*crc;
        part_size=*fragment;
    }
    if (parts.size()!=*count || length!=*message || session!=*crc || part_size!=*fragment)
        return Error("This UR fragment belongs to a different session. Restart to use it.");
    auto& slot=parts[*index-1];
    if (slot) {
        if (*slot!=fields) return Error("Conflicting UR fragments were received for the same position.");
    } else {
        slot=std::string(fields);
        ++collected;
    }
    if (collected!=parts.size()) return {QrRead::Kind::Partial};
    std::string assembled;
    assembled.reserve(parts.size()*part_size);
    for (const auto& part:parts) assembled+=*part;
    if (std::ranges::any_of(std::string_view(assembled).substr(length),[](char c){return c!='\0';}))
        return Error("The UR message padding is invalid. Restart the scan.");
    assembled.resize(length);
    if (Crc32(assembled)!=session) return Error("The assembled UR checksum is invalid. Restart the scan.");
    return Psbt(assembled);
}
}
