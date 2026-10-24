#include "djmeta/workspace_defaults.h"
#include <cassert>
#include <initializer_list>
#include <string>

int main() {
    const djmeta::WorkspaceDefaults defaults;
    assert(djmeta::workspace_defaults_valid(defaults));
    assert(djmeta::serialize_workspace_defaults(defaults) == "v1|0|0|0|0");
    assert(djmeta::parse_workspace_defaults("v1|0|0|0|0") == defaults);

    for (int tab = 0; tab < 3; ++tab)
        for (int provider = 0; provider < 3; ++provider)
            for (int profile = 0; profile < 3; ++profile)
                for (bool visible : {false, true}) {
                    const djmeta::WorkspaceDefaults s{tab, provider, profile, visible};
                    const auto serialized = djmeta::serialize_workspace_defaults(s);
                    assert(serialized.size() == 10);
                    assert(djmeta::parse_workspace_defaults(serialized) == s);
                }

    for (const std::string corrupt : {
             "", "v1|", "v2|0|0|0|0", "v1|3|0|0|0", "v1|0|5|0|0",
             "v1|0|0|7|0", "v1|0|0|0|2", "v1|0|0|0|0extra",
             "v1/0/0/0/0", "v1|0|0|0|\n"}) {
        assert(djmeta::parse_workspace_defaults(corrupt) == defaults);
    }
    assert(djmeta::serialize_workspace_defaults({-1, 0, 0, false}).empty());
    assert(djmeta::serialize_workspace_defaults({0, -1, 0, false}).empty());
    assert(djmeta::serialize_workspace_defaults({0, 0, 3, false}).empty());
}
