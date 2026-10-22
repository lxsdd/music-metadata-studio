#include "djmeta/online_query_prefill.h"

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
