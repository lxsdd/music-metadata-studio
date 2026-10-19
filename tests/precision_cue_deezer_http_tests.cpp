#include "djmeta/precision_cue_deezer_http.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <stdexcept>
#include <string>

namespace {
template<class F> void rejects(F f) {
    bool caught = false;
    try { f(); } catch (const std::invalid_argument&) { caught = true; }
    assert(caught);
}
}
int main() {
    djmeta::DeezerPrecisionCueHttpRequest request;
    request.album_id = "0001071045032";
    assert(djmeta::deezer_precision_cue_http_path(request) ==
           "/album/1071045032");
    // The default request MUST NOT touch the network.
    rejects([&] { (void)djmeta::fetch_deezer_precision_cue_json(request); });
    for (const char* invalid : {
        "", "0", "-1", "1/../2", "1?x=2", "1#x", "1%2f2",
        "https://api.deezer.com/album/1", "18446744073709551615"
    }) {
        request.album_id = invalid;
        rejects([&] { (void)djmeta::deezer_precision_cue_http_path(request); });
    }
}
