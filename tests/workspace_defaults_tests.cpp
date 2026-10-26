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
    // Clipboard transfer: exact schema, UTF-8, complete atomic merge,
    // conflicting names, 24-profile capacity and true no-op.
    const std::vector<NamedRouteProfile> portable{first,second};
    const auto transfer=djmeta::export_named_routes_transfer(portable);
    CHECK(transfer.starts_with("MMS-NAMED-ROUTES/1\n"));
    CHECK(djmeta::parse_named_routes_transfer(transfer)==portable);
    CHECK(transfer.find("Fixture")==std::string::npos);
    CHECK(!djmeta::parse_named_routes_transfer(encoded));
    CHECK(!djmeta::parse_named_routes_transfer("MMS-NAMED-ROUTES/2\nv1|0"));
    CHECK(!djmeta::parse_named_routes_transfer(transfer+"|extra"));
    CHECK(!djmeta::parse_named_routes_transfer(transfer+"\n"));
    CHECK(!djmeta::parse_named_routes_transfer(
        "MMS-NAMED-ROUTES/1\nv1|1|41|42|43|extra"));
    CHECK(!djmeta::parse_named_routes_transfer(
        std::string(131072+djmeta::named_route_transfer_prefix.size()+1,'A')));
    const auto imported=djmeta::plan_named_routes_import({},portable);
    CHECK(imported.added==2 && imported.replaced==0 &&
          imported.unchanged==0 && imported.changed());
    CHECK(imported.merged==portable);
    const auto repeated=djmeta::plan_named_routes_import(portable,portable);
    CHECK(!repeated.changed() && repeated.unchanged==2 &&
          repeated.merged==portable);
    const NamedRouteProfile revised{"LIVE MIXES",R"(E:\Portable\Mixes)","%title%"};
    const auto merged=djmeta::plan_named_routes_import(
        portable,{revised,first});
    CHECK(merged.added==0 && merged.replaced==1 &&
          merged.unchanged==1 && merged.changed());
    CHECK(merged.merged.size()==2 && merged.merged[1]==revised);
    CHECK(portable[1]==second); // never mutate input
    blocked=false;
    try { (void)djmeta::plan_named_routes_import(saved,{{"Extra","C:","%title%"}}); }
    catch(const std::invalid_argument&) { blocked=true; }
    CHECK(blocked);
    CHECK(saved.size()==24);
    blocked=false;
    try { (void)djmeta::plan_named_routes_import({},{{"Singles","C:","%title%"}}); }
    catch(const std::invalid_argument&) { blocked=true; }
    CHECK(blocked);
    blocked=false;
    try { (void)djmeta::plan_named_routes_import({first},{{"my | dj Ω","C:","%title%"},{"MY | DJ Ω","D:","%title%"}}); }
    catch(const std::invalid_argument&) { blocked=true; }
    CHECK(blocked);

}
