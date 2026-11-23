#include "djmeta/precision_cue_embedded_probe.h"
#include "djmeta/precision_cue_embedded_crosscheck.h"
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using namespace djmeta;
#define CHECK(x) do { if (!(x)) { \
    std::fprintf(stderr, "Embedded CUE test failed line %d: %s\n", __LINE__, #x); \
    std::exit(1); } } while (false)
namespace {
void be(std::string& s, std::uint32_t n) {
    for (int i = 3; i >= 0; --i) s.push_back(static_cast<char>((n >> (8*i)) & 255));
}
void le(std::string& s, std::uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) s.push_back(static_cast<char>((n >> (8*i)) & 255));
}
void sync(std::string& s, std::uint32_t n) {
    for (int i = 3; i >= 0; --i) s.push_back(static_cast<char>((n >> (7*i)) & 127));
}
std::string frame(std::string id, const std::string& payload, unsigned version) {
    std::string out = std::move(id);
    if (version == 4) sync(out,static_cast<std::uint32_t>(payload.size()));
    else be(out,static_cast<std::uint32_t>(payload.size()));
    return out + std::string(2,'\0') + payload;
}
std::string txxx(std::string name, std::string value = "TRACK 01") {
    std::string out(1, '\3');
    out += name;
    out.push_back('\0');
    out += value;
    return out;
}
std::string txxx_utf16(const std::string& name) {
    std::string out(1,'\1');
    out.push_back(static_cast<char>(0xff));
    out.push_back(static_cast<char>(0xfe));
    for (char c : name) { out.push_back(c); out.push_back('\0'); }
    out += std::string(2,'\0');
    out += "synthetic tag value";
    return out;
}
std::string id3(std::string frames, unsigned version = 3) {
    std::string out = "ID3";
    out.push_back(static_cast<char>(version));
    out += std::string(2,'\0');
    sync(out,static_cast<std::uint32_t>(frames.size()));
    return out + frames + "SYNTHETIC MP3 AUDIO BODY - NOT REAL MUSIC";
}
std::string ape(std::string key, unsigned flags = 0) {
    std::string body;
    const std::string value = "FILE \"test.mp3\" WAVE";
    le(body,static_cast<std::uint32_t>(value.size()));
    le(body,flags);
    body += key;
    body.push_back('\0');
    body += value;
    std::string footer = "APETAGEX";
    le(footer,2000);
    le(footer,static_cast<std::uint32_t>(body.size()+32));
    le(footer,1);
    footer += std::string(12,'\0');
    return body + footer;
}
std::string flac_block(unsigned type, const std::string& body, bool last) {
    const unsigned sz = static_cast<unsigned>(body.size());
    std::string out(1,static_cast<char>(type | (last ? 128u : 0u)));
    out.push_back(static_cast<char>((sz>>16)&255));
    out.push_back(static_cast<char>((sz>>8)&255));
    out.push_back(static_cast<char>(sz&255));
    return out + body;
}
std::string vorbis(const std::vector<std::string>& fields) {
    std::string body;
    le(body,5);
    body += "Dummy";
    le(body,static_cast<std::uint32_t>(fields.size()));
    for (const auto& field : fields) {
        le(body,static_cast<std::uint32_t>(field.size()));
        body += field;
    }
    return body;
}
PrecisionCueEmbeddedProbe probe(const std::string& bytes,
                                PrecisionCueEmbeddedCarrier type) {
    std::istringstream input(bytes,std::ios::binary);
    const auto before = input.str();
    const auto result = inspect_precision_cue_embedded(input,type);
    CHECK(input.str() == before); // Absolutely read-only.
    return result;
}
void mp3_matrix() {
    using State = PrecisionCueEmbeddedState;
    const auto uncued = frame("TXXX",txxx("MIXVERSION","DJ"),3);
    const auto cued = frame("TXXX",txxx("CUESHEET"),3);
    CHECK(probe(id3(uncued),PrecisionCueEmbeddedCarrier::Mp3).state ==
          State::NoneDetected);
    CHECK(probe(id3(cued),PrecisionCueEmbeddedCarrier::Mp3).state ==
          State::TextualPresent);
    CHECK(probe(id3(frame("TXXX",txxx("cue_sheet"),4),4),
                PrecisionCueEmbeddedCarrier::Mp3).state == State::TextualPresent);
    CHECK(probe(id3(frame("TXXX",txxx_utf16("CUESHEET"),3)),
                PrecisionCueEmbeddedCarrier::Mp3).state == State::TextualPresent);
    CHECK(probe(id3(cued+cued),PrecisionCueEmbeddedCarrier::Mp3).state ==
          State::MultipleOrConflicting);
    CHECK(probe(id3(cued)+ape("CUESHEET"),PrecisionCueEmbeddedCarrier::Mp3).state ==
          State::MultipleOrConflicting);
    CHECK(probe(id3(uncued)+ape("cueSHEET"),PrecisionCueEmbeddedCarrier::Mp3).state ==
          State::TextualPresent);
    CHECK(probe(id3(uncued)+ape("ALBUM"),PrecisionCueEmbeddedCarrier::Mp3).state ==
          State::NoneDetected);
    CHECK(probe(id3(uncued)+ape("CUESHEET",2),
                PrecisionCueEmbeddedCarrier::Mp3).state == State::Unqualified);
    auto bad = id3(cued);
    bad[6] = static_cast<char>(0x80); // Non-synchsafe tag size.
    CHECK(probe(bad,PrecisionCueEmbeddedCarrier::Mp3).state ==
          State::Unqualified);
    bad = id3(cued,2); // Unsupported ID3v2.2.
    CHECK(probe(bad,PrecisionCueEmbeddedCarrier::Mp3).state ==
          State::Unqualified);
    bad = id3(cued);
    bad[5] = static_cast<char>(0x80); // Unsynchronisation unsupported.
    CHECK(probe(bad,PrecisionCueEmbeddedCarrier::Mp3).state ==
          State::Unqualified);
    bad = id3(cued);
    bad.resize(16); // Truncated tag.
    CHECK(probe(bad,PrecisionCueEmbeddedCarrier::Mp3).state ==
          State::Unqualified);
    bad = id3(cued);
    bad[14] = static_cast<char>(0x7f); // Frame length exceeds tag.
    CHECK(probe(bad,PrecisionCueEmbeddedCarrier::Mp3).state ==
          State::Unqualified);
    CHECK(probe("Synthetic MP3 with no ID3v2 or APEv2 CUE data",
                PrecisionCueEmbeddedCarrier::Mp3).state == State::NoneDetected);
}
void flac_matrix() {
    using State = PrecisionCueEmbeddedState;
    const std::string prefix = "fLaC";
    CHECK(probe(prefix+flac_block(4,vorbis({"ARTIST=DJ","CUESHEET=TRACK 01"}),true),
                PrecisionCueEmbeddedCarrier::Flac).state == State::TextualPresent);
    CHECK(probe(prefix+flac_block(4,vorbis({"TITLE=Mixed"}),true),
                PrecisionCueEmbeddedCarrier::Flac).state == State::NoneDetected);
    CHECK(probe(prefix+flac_block(5,"synthetic binary CUE",true),
                PrecisionCueEmbeddedCarrier::Flac).state == State::NativeBinaryPresent);
    CHECK(probe(prefix+flac_block(4,vorbis({"CUESHEET=text"}),false)+
                flac_block(5,"native binary CUE",true),
                PrecisionCueEmbeddedCarrier::Flac).state == State::MultipleOrConflicting);
    CHECK(probe(prefix+flac_block(4,vorbis({"CUESHEET=a","CUESHEET=b"}),true),
                PrecisionCueEmbeddedCarrier::Flac).state == State::MultipleOrConflicting);
    CHECK(probe(prefix+flac_block(4,"malformed",true),
                PrecisionCueEmbeddedCarrier::Flac).state == State::Unqualified);
    CHECK(probe(prefix+flac_block(4,vorbis({"CUESHEET=x"}),false),
                PrecisionCueEmbeddedCarrier::Flac).state == State::Unqualified);
    const auto truncated = (prefix+flac_block(4,vorbis({"CUESHEET=x"}),true)).substr(0,9);
    CHECK(probe(truncated,PrecisionCueEmbeddedCarrier::Flac).state == State::Unqualified);
    CHECK(probe("invalid FLAC",PrecisionCueEmbeddedCarrier::Flac).state ==
          State::Unqualified);
}
void crosscheck_matrix() {
    using Physical = PrecisionCueEmbeddedState;
    using Exposed = PrecisionCueEmbeddedTextState;
    using Result = PrecisionCueEmbeddedCrosscheckState;
    PrecisionCueEmbeddedTextObservation visible;
    PrecisionCueEmbeddedProbe raw;
    visible.state = Exposed::Absent;
    raw.state = Physical::NoneDetected;
    CHECK(crosscheck_precision_cue_embedded(visible,raw).state == Result::NoCueObserved);
    raw.state = Physical::TextualPresent;
    CHECK(crosscheck_precision_cue_embedded(visible,raw).state == Result::ReviewRequired);
    visible.state = Exposed::SyntaxOnly;
    CHECK(crosscheck_precision_cue_embedded(visible,raw).state ==
          Result::TextualAgreementReadOnly);
    raw.state = Physical::NoneDetected;
    CHECK(crosscheck_precision_cue_embedded(visible,raw).state == Result::ReviewRequired);
    visible.state = Exposed::Absent;
    raw.state = Physical::NativeBinaryPresent;
    CHECK(crosscheck_precision_cue_embedded(visible,raw).state ==
          Result::ProtectedNativeBinary);
    visible.state = Exposed::SyntaxOnly;
    CHECK(crosscheck_precision_cue_embedded(visible,raw).state == Result::ReviewRequired);
    raw.state = Physical::MultipleOrConflicting;
    CHECK(crosscheck_precision_cue_embedded(visible,raw).state == Result::ReviewRequired);
    raw.state = Physical::Unqualified;
    CHECK(crosscheck_precision_cue_embedded(visible,raw).state == Result::ReviewRequired);
    raw.state = Physical::NoneDetected;
    visible.state = Exposed::NeedsReview;
    CHECK(crosscheck_precision_cue_embedded(visible,raw).state == Result::ReviewRequired);
}

} // namespace
int main() {
    mp3_matrix();
    flac_matrix();
    crosscheck_matrix();
    std::puts("PASS: bounded read-only ID3v2, APEv2, Vorbis and FLAC Type5 CUE inventory");
    return 0;
}
