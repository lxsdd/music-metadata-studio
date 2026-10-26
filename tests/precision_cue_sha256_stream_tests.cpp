#include "djmeta/precision_cue_sha256_stream.h"

#include <algorithm>
#include <array>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace djmeta;
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr,"SHA256 stream test failure line %d: %s\n",\
        __LINE__,#condition); std::exit(1); } } while(false)

std::string hex(const std::array<std::uint8_t,32>& digest) {
    constexpr char h[]="0123456789abcdef";
    std::string result;
    for (const auto b:digest) {
        result.push_back(h[b>>4]);
        result.push_back(h[b&15u]);
    }
    return result;
}
void verify_all_fragmentations(std::string_view bytes) {
    const auto reference=precision_cue_sha256(bytes);
    for (const auto stride : {std::size_t{1},std::size_t{2},std::size_t{3},
        std::size_t{7},std::size_t{31},std::size_t{55},
        std::size_t{63},std::size_t{64},std::size_t{65},
        std::size_t{127},std::size_t{4096}}) {
        PrecisionCueSha256Stream digest;
        digest.update({});
        for (std::size_t at=0;at<bytes.size();at+=stride)
            digest.update(bytes.substr(at,(std::min)(stride,bytes.size()-at)));
        CHECK(digest.bytes_hashed()==bytes.size());
        CHECK(digest.finalize()==reference);
        CHECK(digest.finalize()==reference); // Idempotent snapshot.
        bool threw=false;
        try { digest.update("x"); }
        catch(const std::logic_error&) { threw=true; }
        CHECK(threw);
    }
}
void standard_vectors() {
    PrecisionCueSha256Stream empty;
    CHECK(hex(empty.finalize()) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    PrecisionCueSha256Stream abc;
    abc.update("a"); abc.update("b"); abc.update("c");
    CHECK(hex(abc.finalize()) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string million(1000000,'a');
    PrecisionCueSha256Stream big;
    for (unsigned i=0;i<1000;++i) big.update(std::string_view(million).substr(0,1000));
    CHECK(big.bytes_hashed()==million.size());
    CHECK(hex(big.finalize()) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
    verify_all_fragmentations(million);
}
int main() {
    standard_vectors();
    for (const std::size_t len : std::vector<std::size_t>{0,1,2,3,7,31,55,
        56u,57u,63u,64u,65u,66u,119u,120u,121u,127u,128u,129u,
        255u,256u,257u,512u,1024u,4097u,65535u}) {
        std::string data(len,'\0');
        for (std::size_t i=0;i<len;++i)
            data[i]=static_cast<char>((i*37u+123u)&255);
        verify_all_fragmentations(data);
    }
    std::mt19937_64 random(0x5143877128ULL); // fixed synthetic seed
    for(unsigned i=0;i<40;++i) {
        const auto len=static_cast<std::size_t>(random()%16384);
        std::string data(len,'\0');
        for(auto& c:data) c=static_cast<char>(random()&255);
        verify_all_fragmentations(data);
    }
    std::puts("PASS: stream SHA256 FIPS vectors + exact one-shot equivalence");
    return 0;
}
