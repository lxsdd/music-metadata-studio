#include "djmeta/precision_cue_apple_link.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
template<typename T> void must_reject(T callback) {
    bool rejected = false;
    try { callback(); } catch (const std::invalid_argument&) {
        rejected = true;
    }
    assert(rejected);
}

void valid_links() {
    const auto german = djmeta::parse_apple_precision_cue_album_link(
        "https://music.apple.com/de/album/synthetic-mix/1234567890?i=1234567891");
    assert(german.collection_id == "1234567890");
    assert(german.storefront == "DE");
    const auto bare = djmeta::parse_apple_precision_cue_album_link(
        "https://music.apple.com/album/000012345");
    assert(bare.collection_id == "12345");
    assert(bare.storefront.empty());
    const auto utf8 = djmeta::parse_apple_precision_cue_album_link(
        "https://music.apple.com/US/album/bj%C3%B6rk/9223372036854775807");
    assert(utf8.collection_id == "9223372036854775807");
    assert(utf8.storefront == "US");
}

void invalid_links() {
    for (const auto& url : std::vector<std::string>{
        "http://music.apple.com/de/album/mix/12345",
        "https://music.apple.com.evil.test/de/album/mix/12345",
        "https://user@music.apple.com/de/album/mix/12345",
        "https://music.apple.com:443/de/album/mix/12345",
        "https://music.apple.com/de/artist/mix/12345",
        "https://music.apple.com/de/album/mix/0",
        "https://music.apple.com/de/album/mix/-1",
        "https://music.apple.com/de/album/mix/9223372036854775808",
        "https://music.apple.com/de/album/mix/12345/extra",
        "https://music.apple.com/de/album/mix/12345/",
        "https://music.apple.com/de/album/../12345",
        "https://music.apple.com/de/album/a%2Fb/12345",
        "https://music.apple.com/de/album/a%5Cb/12345",
        "https://music.apple.com/de/album/a%00b/12345",
        "https://music.apple.com/de/album/a%ZZb/12345",
        "https://music.apple.com/de/album/mix/12345?i=0",
        "https://music.apple.com/de/album/mix/12345?i=123&bad=true",
        "https://music.apple.com/de/album/mix/12345?country=US",
        "https://music.apple.com/de/album/mix/12345#hash",
        "https://music.apple.com/de/album/mix/12345\\bad",
        "https://music.apple.com/de/album/mix/12 345",
        "https://music.apple.com/de/album/mix/12345?i=999999999999999999999",
    }) {
        must_reject([&] { (void)djmeta::parse_apple_precision_cue_album_link(url); });
    }
}

void mutation_smoke() {
    std::mt19937 generator(20261018);
    const std::string original =
        "https://music.apple.com/de/album/synthetic-mix/1234567890?i=1234567891";
    for (unsigned i = 0; i < 3000; ++i) {
        auto mutated = original;
        const auto index = static_cast<std::size_t>(generator() % (mutated.size() + 1));
        const auto c = static_cast<char>(generator() % 256);
        if (i % 3 == 0 && index < mutated.size()) mutated[index] = c;
        else if (i % 3 == 1) mutated.insert(mutated.begin() +
            static_cast<std::string::difference_type>(index), c);
        else mutated.resize(index);
        try {
            const auto parsed = djmeta::parse_apple_precision_cue_album_link(mutated);
            assert(!parsed.collection_id.empty());
            for (const auto digit : parsed.collection_id)
                assert(digit >= '0' && digit <= '9');
            assert(parsed.storefront.empty() || parsed.storefront.size() == 2);
        } catch (const std::invalid_argument&) {
            // Invalid links are expected to fail closed.
        }
    }
}
} // namespace

int main() {
    valid_links();
    invalid_links();
    mutation_smoke();
}
