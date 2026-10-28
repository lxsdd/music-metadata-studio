#include "djmeta/write_guard.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {
void check(bool yes, const char* message) {
    if (!yes) { std::cerr << "FAIL: " << message << "\n"; std::exit(1); }
}
djmeta::MetadataDocument sample() {
    return {{{"ARTIST", {"Daft Punk"}}, {"TITLE", {"One More Time"}},
             {"GENRE", {"House", "French House"}}}};
}
}

int main() {
    using djmeta::TagWriteDecision;
    using djmeta::compare_physical_text_tags;
    using djmeta::cue_bytes_rewrite_needed;

    const auto source = sample();
    check(compare_physical_text_tags(source, source) == TagWriteDecision::Unchanged,
          "identical tags must not trigger a writer");
    auto changed = sample();
    std::swap(changed.fields.front(), changed.fields.back());
    changed.fields[1].name = "title";
    check(compare_physical_text_tags(source, changed) == TagWriteDecision::Unchanged,
          "tag enumeration order and ASCII field-key case do not justify writes");

    changed = sample();
    changed.fields[2].values[1] = "Electro House";
    check(compare_physical_text_tags(source, changed) == TagWriteDecision::Changed,
          "changing second value of multivalue field requires a write");
    changed = sample();
    std::swap(changed.fields[2].values[0], changed.fields[2].values[1]);
    check(compare_physical_text_tags(source, changed) == TagWriteDecision::Changed,
          "multivalue order must be preserved");
    changed = sample();
    changed.fields.push_back({"DATE", {"2000"}});
    check(compare_physical_text_tags(source, changed) == TagWriteDecision::Changed,
          "new tag requires a write");
    changed = sample();
    changed.fields.pop_back();
    check(compare_physical_text_tags(source, changed) == TagWriteDecision::Changed,
          "tag removal cannot be mistaken for a no-op (deletion separately gated)");
    changed = sample();
    changed.fields[0].values[0] = "daft punk";
    check(compare_physical_text_tags(source, changed) == TagWriteDecision::Changed,
          "changes to value case are real metadata changes");

    changed = sample();
    changed.fields.push_back({"artist", {"Other"}});
    check(compare_physical_text_tags(source, changed) == TagWriteDecision::Unqualified,
          "duplicate case-insensitive physical tag keys fail closed");
    changed = sample();
    changed.fields[0].values.clear();
    check(compare_physical_text_tags(source, changed) == TagWriteDecision::Unqualified,
          "missing multivalue payload fails closed");
    changed = sample();
    changed.fields[0].name.clear();
    check(compare_physical_text_tags(source, changed) == TagWriteDecision::Unqualified,
          "empty field names fail closed");
    changed = sample();
    changed.fields[0].values[0] = std::string("a\0b", 3);
    check(compare_physical_text_tags(source, changed) == TagWriteDecision::Unqualified,
          "NUL-containing values fail closed");

    // Generic physical tag updates must never smuggle embedded CUE edits.
    changed = sample();
    changed.fields.push_back({"CUESHEET", {"FILE x.flac WAVE"}});
    check(compare_physical_text_tags(source, changed) == TagWriteDecision::Unqualified,
          "creating CUESHEET is not an ordinary physical tag update");
    auto with_cue = sample();
    with_cue.fields.push_back({"CUESHEET", {"FILE x.flac WAVE"}});
    changed = with_cue;
    changed.fields.back().values[0] = "FILE y.flac WAVE";
    check(compare_physical_text_tags(with_cue, changed) == TagWriteDecision::Unqualified,
          "mutating embedded CUE requires separate carrier-authorized writer");
    changed = with_cue;
    changed.fields.pop_back();
    check(compare_physical_text_tags(with_cue, changed) == TagWriteDecision::Unqualified,
          "deleting embedded CUE is not an ordinary tag edit");
    changed = with_cue;
    changed.fields[0].values[0] = "Daft Punk & friends";
    check(compare_physical_text_tags(with_cue, changed) == TagWriteDecision::Changed,
          "physical text tag can differ while original CUE remains identical");
    changed = with_cue;
    changed.fields.back().name = "cuesheet";
    check(compare_physical_text_tags(with_cue, changed) == TagWriteDecision::Unchanged,
          "case-only CUESHEET name is not a mutation");

    check(!cue_bytes_rewrite_needed("FILE \"x.mp3\" MP3\r\n",
                                   "FILE \"x.mp3\" MP3\r\n"),
          "byte-identical cue must never be rewritten");
    check(cue_bytes_rewrite_needed("A\r\n", "A\n"),
          "newline-only changes are byte changes; avoid hidden text normalization");
    check(cue_bytes_rewrite_needed("FILE x.mp3 MP3", "FILE y.mp3 MP3"),
          "real cue reference change requires postimage write");
    std::cout << "PASS: exact no-op write suppression for physical tags and CUE bytes\n";
}
