#include "djmeta/preview_window_geometry.h"

#include <cstdlib>
#include <iostream>
#include <string>

static void check(bool yes, const char* description) {
    if (!yes) { std::cerr << "FAIL: " << description << "\n"; std::exit(1); }
}
int main() {
    using namespace djmeta;
    for (const PreviewWindowSize original : {
        PreviewWindowSize{750, 520}, PreviewWindowSize{1600, 900},
        PreviewWindowSize{8192, 8192}, PreviewWindowSize{320, 240}
    }) {
        const auto serialized = serialize_preview_window_size(original);
        const auto restored = parse_preview_window_size(serialized);
        check(restored && *restored == original,
              "persisted logical dimensions roundtrip exactly, no precision drift");
        check(serialized == serialize_preview_window_size(*restored),
              "stable canonical encoding prevents unnecessary config writes");
    }
    for (const std::string bad : {
        "", "v1", "v2|700|500", "v1|700", "v1|700|500|extra",
        "v1||500", "v1|700|", "v1|x|500", "v1|700|x",
        "v1|-700|500", "v1|+700|500", "v1|700|500 ", "v1|700|500\n",
        "v1|319|500", "v1|700|239", "v1|8193|500",
        "v1|700|8193", "v1|999999999999999999999999|600",
        "v1|700|999999999999999999999999"
    }) {
        check(!parse_preview_window_size(bad),
              "bad, stale, oversized or unsupported profile cfg fails closed");
    }
    check(serialize_preview_window_size({1, 2}).empty(),
          "out-of-range size is not persisted");
    for (const PreviewWindowPlacement original : {
        PreviewWindowPlacement{-1950,125,1180,750,false},
        PreviewWindowPlacement{220,140,1500,900,true},
        PreviewWindowPlacement{0,0,320,240,false}
    }) {
        const auto saved=serialize_preview_window_placement(original);
        const auto parsed=parse_preview_window_placement(saved);
        check(parsed && *parsed==original,
              "position, normal dimensions and maximize state roundtrip exactly");
        check(serialize_preview_window_placement(*parsed)==saved,
              "unchanged window geometry yields identical cfg string");
    }
    for (const std::string malformed : {
        "", "v1", "v2|0|0|800|600|0", "v1|0|0|800|600",
        "v1|0|0|800|600|0|ignored", "v1|-90000|0|800|600|0",
        "v1|0|0|319|600|0", "v1|0|0|800|8193|0",
        "v1|0|0|800|600|2", "v1|x|0|800|600|0",
        "v1|0|0|800|600|false", "v1|1.3|0|800|600|0"
    })
        check(!parse_preview_window_placement(malformed),
              "invalid/offscreen/unknown geometry cfg is rejected");
    std::cout << "PASS: foobar preview normal position/size/maximize + migration schema\n";

}
