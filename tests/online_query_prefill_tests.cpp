#include "djmeta/online_query_prefill.h"
#include "djmeta/single_source_tag_load.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

int main() {
    using djmeta::should_replace_auto_online_query;
    const auto check = [](bool actual, bool expected, const char* label) {
        if (actual != expected) {
            std::cerr << "FAIL: " << label << '\n';
            std::exit(EXIT_FAILURE);
        }
    };
    check(should_replace_auto_online_query(L"Track A", L"Track A", false),
          true, "prior auto query replaced when selecting new track with no TITLE");
    check(should_replace_auto_online_query(L"", L"Track A", false),
          true, "empty search field accepts next automatic suggestion");
    check(should_replace_auto_online_query(L"My manual query", L"Track A", false),
          false, "manual query survives routine selection and redraw");
    check(should_replace_auto_online_query(L"My manual query", L"Track A", true),
          true, "explicit double click may populate current track suggestion");
    check(should_replace_auto_online_query(L"Äther – 夜", L"Äther – 夜", false),
          true, "Unicode automatic query is recognized exactly");
    check(should_replace_auto_online_query(L"Äther – 夜 custom", L"Äther – 夜", false),
          false, "Unicode manual edits are never overwritten on redraw");
    check(should_replace_auto_online_query(L"Discogs https://discogs.com/release/1",
                                           L"", false),
          false, "a manually supplied lookup is not erased for a CUE");
    // Explicit single physical selection always defeats a potentially
    // incompletely imported metadata cache. Bulk and CUE virtual handles
    // preserve foobar's asynchronous, change-only library-scale behavior.
    using djmeta::SingleSourceTagLoad;
    using djmeta::single_source_tag_load_policy;
    check(single_source_tag_load_policy(1, true) ==
              SingleSourceTagLoad::ForcePhysicalRefresh,
          true, "single physical FLAC refreshes original tags from file");
    check(single_source_tag_load_policy(1, false) ==
              SingleSourceTagLoad::CheckIfChanged,
          true, "virtual single subsong cannot force physical refresh");
    check(single_source_tag_load_policy(2, true) ==
              SingleSourceTagLoad::CheckIfChanged &&
          single_source_tag_load_policy(54129, true) ==
              SingleSourceTagLoad::CheckIfChanged,
          true, "multi-track/large library remains change-only");
    check(single_source_tag_load_policy(0, true) ==
              SingleSourceTagLoad::CheckIfChanged,
          true, "empty context has no forced load");
    using djmeta::ProviderQueryTransition;
    using djmeta::provider_query_transition;
    check(provider_query_transition(false,false)==
              ProviderQueryTransition::Preserve,
          true, "MusicBrainz/Discogs switch preserves a manually typed query");
    check(provider_query_transition(false,true)==
              ProviderQueryTransition::Clear &&
          provider_query_transition(true,true)==
              ProviderQueryTransition::Clear,
          true, "entering Beatport always removes old TITLE/foreign URL");
    check(provider_query_transition(true,false)==
              ProviderQueryTransition::ClearAndSuggest,
          true, "leaving Beatport clears old URL then suggests local TITLE");
    std::cout << "PASS: auto/manual online query selection policy\n";
}
