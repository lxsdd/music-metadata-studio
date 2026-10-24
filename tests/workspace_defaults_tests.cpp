#include "djmeta/workspace_defaults.h"
#include <initializer_list>
#include <stdexcept>
#include <string>

// Unlike assert(), this remains active in Release/-DNDEBUG CI builds.
#define CHECK(expression) do { \
    if (!(expression)) throw std::runtime_error( \
        "workspace defaults: " #expression); \
} while (false)

int main() {
    const djmeta::WorkspaceDefaults defaults;
    CHECK(djmeta::workspace_defaults_valid(defaults));
    CHECK(djmeta::serialize_workspace_defaults(defaults) == "v1|0|0|0|0");
    CHECK(djmeta::parse_workspace_defaults("v1|0|0|0|0") == defaults);

    for (int tab = 0; tab < 3; ++tab)
        for (int provider = 0; provider < 4; ++provider)
            for (int profile = 0; profile < 3; ++profile)
                for (bool visible : {false, true}) {
                    const djmeta::WorkspaceDefaults s{tab, provider, profile, visible};
                    const auto serialized = djmeta::serialize_workspace_defaults(s);
                    CHECK(serialized.size() == 10);
                    CHECK(djmeta::parse_workspace_defaults(serialized) == s);
                }

    for (const std::string corrupt : {
             "", "v1|", "v2|0|0|0|0", "v1|3|0|0|0", "v1|0|5|0|0",
             "v1|0|0|7|0", "v1|0|0|0|2", "v1|0|0|0|0extra",
             "v1/0/0/0/0", "v1|0|0|0|\n"}) {
        CHECK(djmeta::parse_workspace_defaults(corrupt) == defaults);
    }
    CHECK(djmeta::serialize_workspace_defaults({-1, 0, 0, false}).empty());
    CHECK(djmeta::serialize_workspace_defaults({0, -1, 0, false}).empty());
    CHECK(djmeta::serialize_workspace_defaults({0, 0, 3, false}).empty());
    const djmeta::WorkspaceDefaults auto_source{0, 3, 0, false};
    CHECK(djmeta::serialize_workspace_defaults(auto_source) == "v1|0|3|0|0");
    CHECK(djmeta::parse_workspace_defaults("v1|0|3|0|0") == auto_source);
    CHECK(djmeta::serialize_workspace_defaults({0, 4, 0, false}).empty());
}
