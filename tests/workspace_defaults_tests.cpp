#include "djmeta/workspace_defaults.h"
#include "djmeta/named_route_profiles.h"
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
    CHECK(djmeta::serialize_workspace_defaults(defaults) == "v1|0|3|0|0");
    CHECK(djmeta::parse_workspace_defaults("v1|0|3|0|0") == defaults);

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
    const djmeta::WorkspaceDefaults old_explicit_musicbrainz{0, 0, 0, false};
    CHECK(djmeta::parse_workspace_defaults("v1|0|0|0|0") ==
          old_explicit_musicbrainz);
    CHECK(djmeta::serialize_workspace_defaults(old_explicit_musicbrainz) ==
          "v1|0|0|0|0");
    const djmeta::WorkspaceDefaults auto_source{0, 3, 0, false};
    CHECK(djmeta::serialize_workspace_defaults(auto_source) == "v1|0|3|0|0");
    CHECK(djmeta::parse_workspace_defaults("v1|0|3|0|0") == auto_source);
    CHECK(djmeta::serialize_workspace_defaults({0, 4, 0, false}).empty());

    // Profile-local route template lifecycle; no source identity or approval.
    using djmeta::NamedRouteProfile;
    const NamedRouteProfile first{"My | DJ Ω",R"(C:\Fixture\Albums)",
                                  R"(%album artist%\%album%\%title%)"};
    const NamedRouteProfile second{"Live mixes",R"(D:\Fixture\Sets)",
                                   R"(%artist% - %title%)"};
    std::vector<NamedRouteProfile> saved;
    CHECK(djmeta::upsert_named_route(saved,first,false));
    CHECK(djmeta::upsert_named_route(saved,second,false));
    const auto encoded=djmeta::serialize_named_routes(saved);
    CHECK(djmeta::parse_named_routes(encoded)==saved);
    CHECK(encoded.find("Fixture")==std::string::npos);
    CHECK(!djmeta::upsert_named_route(saved,first,false));
    bool blocked=false;
    try { (void)djmeta::upsert_named_route(
        saved,{"LIVE MIXES","X:","%title%"},false); }
    catch(const std::invalid_argument&) { blocked=true; }
    CHECK(blocked);
    CHECK(djmeta::upsert_named_route(
        saved,{"LIVE MIXES",R"(C:\Fixture\New)","%title%"},true));
    CHECK(saved.size()==2);
    CHECK(djmeta::erase_named_route(saved,"live mixes"));
    CHECK(!djmeta::erase_named_route(saved,"live mixes"));
    CHECK(!djmeta::named_route_valid({"Singles","C:","%title%"}));
    CHECK(!djmeta::named_route_valid({"ALBEN","C:","%title%"}));
    CHECK(!djmeta::named_route_valid({"Bad\nName","C:","%title%"}));
    CHECK(!djmeta::named_route_valid({"Invalid","C:",std::string("\xC0\xAF",2)}));
    for(const auto& bad:{"","v2|0","v1|00","v1|25","v1|1",
        "v1|0|extra","v1|1|41|42|4g","v1|1|41|42|4",
        "v1|1|41|42|43|extra","v1|2|41|42|43"})
        CHECK(!djmeta::parse_named_routes(bad));
    CHECK(djmeta::parse_named_routes("v1|0") &&
          djmeta::parse_named_routes("v1|0")->empty());
    for(int i=0;i<23;++i)
        CHECK(djmeta::upsert_named_route(
            saved,{"Profile "+std::to_string(i),"C:","%title%"},false));
    CHECK(saved.size()==24);
    blocked=false;
    try { (void)djmeta::upsert_named_route(
        saved,{"Overflow","C:","%title%"},false); }
    catch(const std::invalid_argument&) { blocked=true; }
    CHECK(blocked);
    CHECK(djmeta::parse_named_routes(djmeta::serialize_named_routes(saved))==saved);
}
