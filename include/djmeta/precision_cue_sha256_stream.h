#pragma once
// Incremental FIPS 180-4 SHA-256. Use bounded buffers for physical audio
// read-only integrity checks; never materialize entire files in memory.
// This is not a write authorization or a substitute for OS file identity.
#include "djmeta/precision_cue_sha256.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace djmeta {
class PrecisionCueSha256Stream {
public:
    PrecisionCueSha256Stream() = default;
    void update(std::string_view bytes) {
        if (finalized_) throw std::logic_error("SHA256 already finalized");
        constexpr auto limit = (std::numeric_limits<std::uint64_t>::max)() / 8u;
        if (bytes.size() > limit - byte_count_)
            throw std::length_error("SHA256 length exceeds FIPS 180-4 bound");
        byte_count_ += static_cast<std::uint64_t>(bytes.size());
        std::size_t pos = 0;
        if (pending_ != 0) {
            const auto copy = (std::min)(64 - pending_, bytes.size());
            for (std::size_t i = 0; i < copy; ++i)
                buffer_[pending_+i] = static_cast<std::uint8_t>(bytes[i]);
            pending_ += copy;
            pos += copy;
            if (pending_ == 64) {
                compress(buffer_.data());
                pending_ = 0;
            } else {
                return; // Preserve a partially filled block across updates.
            }
        }
        // Route every block through the fixed-size owned buffer: never
        // hand the compressor a pointer into a potentially tiny string_view.
        // This also keeps bounds checks explicit for GCC -Warray-bounds.
        while (pos < bytes.size() && bytes.size()-pos >= buffer_.size()) {
            for (std::size_t i=0; i<buffer_.size(); ++i)
                buffer_[i] = static_cast<std::uint8_t>(bytes[pos+i]);
            compress(buffer_.data());
            pos += buffer_.size();
        }
        const auto remaining = bytes.size()-pos;
        for (std::size_t i = 0; i < remaining; ++i)
            buffer_[i] = static_cast<std::uint8_t>(bytes[pos+i]);
        pending_ = remaining;
    }
    std::array<std::uint8_t,32> finalize() {
        if (!finalized_) {
            const auto bit_length = byte_count_ * 8u;
            buffer_[pending_++] = 0x80u;
            if (pending_ > 56) {
                while (pending_ < 64) buffer_[pending_++] = 0;
                compress(buffer_.data());
                pending_ = 0;
            }
            while (pending_ < 56) buffer_[pending_++] = 0;
            for (unsigned j = 0; j < 8; ++j)
                buffer_[56+j] = static_cast<std::uint8_t>(
                    bit_length >> (8u*(7u-j)));
            compress(buffer_.data());
            pending_ = 0;
            for (std::size_t i=0; i<8; ++i)
                for (std::size_t j=0; j<4; ++j)
                    digest_[4*i+j]=static_cast<std::uint8_t>(
                        h_[i] >> (24 - 8*j));
            finalized_ = true;
        }
        return digest_;
    }
    std::uint64_t bytes_hashed() const noexcept { return byte_count_; }
private:
    static constexpr std::array<std::uint32_t,64> constants_ = {{
        0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
        0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
        0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
        0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
        0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
        0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
        0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
        0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
    }};
    void compress(const std::uint8_t* p) {
        std::array<std::uint32_t,64> w{};
        for (std::size_t i=0; i<16; ++i) {
            const std::size_t j=i*4;
            w[i]=(std::uint32_t(p[j])<<24)|(std::uint32_t(p[j+1])<<16)|
                 (std::uint32_t(p[j+2])<<8)|std::uint32_t(p[j+3]);
        }
        for (std::size_t i=16; i<64; ++i) {
            const auto a=w[i-15],b=w[i-2];
            const auto x=std::rotr(a,7)^std::rotr(a,18)^(a>>3);
            const auto y=std::rotr(b,17)^std::rotr(b,19)^(b>>10);
            w[i]=w[i-16]+x+w[i-7]+y;
        }
        auto a=h_[0],b=h_[1],c=h_[2],d=h_[3],
             e=h_[4],f=h_[5],g=h_[6],z=h_[7];
        for (std::size_t i=0; i<64; ++i) {
            const auto s1=std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25);
            const auto choose=(e&f)^(~e&g);
            const auto t1=z+s1+choose+constants_[i]+w[i];
            const auto s0=std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22);
            const auto majority=(a&b)^(a&c)^(b&c);
            const auto t2=s0+majority;
            z=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
        }
        h_[0]+=a;h_[1]+=b;h_[2]+=c;h_[3]+=d;
        h_[4]+=e;h_[5]+=f;h_[6]+=g;h_[7]+=z;
    }
    std::array<std::uint32_t,8> h_ = {{
        0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
        0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
    }};
    std::array<std::uint8_t,64> buffer_{};
    std::size_t pending_ = 0;
    std::uint64_t byte_count_ = 0;
    std::array<std::uint8_t,32> digest_{};
    bool finalized_ = false;
};
} // namespace djmeta
