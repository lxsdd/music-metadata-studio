#include "djmeta/normalizer.h"
#include "djmeta/title_version_review.h"
#include "djmeta/rule_loader.h"
#include "djmeta/rules_snapshot.h"
#include "djmeta/structural_guard.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

djmeta::Rule trim_rule() {
    return djmeta::Rule{
        "safe.trim-whitespace", true, 10, {"*"},
        djmeta::MatchKind::Always, "", false,
        djmeta::TransformKind::TrimWhitespace, "",
        djmeta::SafetyClass::Safe,
        "Remove leading and trailing ASCII whitespace.",
        "builtin", ""
    };
}

djmeta::Rule collapse_rule() {
    return djmeta::Rule{
        "safe.collapse-whitespace", true, 20, {"*"},
        djmeta::MatchKind::Always, "", false,
        djmeta::TransformKind::CollapseWhitespace, "",
        djmeta::SafetyClass::Safe,
        "Collapse repeated ASCII whitespace.",
        "builtin", ""
    };
}

djmeta::Rule unicode_whitespace_rule() {
    return djmeta::Rule{
        "safe.normalize-unicode-whitespace", true, 5, {"*"},
        djmeta::MatchKind::Always, "", false,
        djmeta::TransformKind::NormalizeUnicodeWhitespace, "",
        djmeta::SafetyClass::Safe,
        "Map Unicode White_Space to ASCII space.",
        "builtin", ""
    };
}

void test_safe_scalar_structural_byte_guards() {
    require(djmeta::safe_scalar_metadata_field("TITLE", " A Title "),
            "single-line ordinary TITLE remains eligible");
    require(!djmeta::safe_scalar_metadata_field("TITLE", std::string("A\0B", 3)) &&
            !djmeta::safe_scalar_metadata_field("TITLE", "A\nB"),
            "embedded zero or line break must never be classed SAFE");
    require(!djmeta::safe_scalar_metadata_field("CUESHEET", "FILE track.mp3 MP3") &&
            !djmeta::safe_scalar_metadata_field("LYRICS", "one line"),
            "structured cue and lyrics remain protected");
}

void test_no_implicit_blank_tag_deletion() {
    const djmeta::MetadataDocument source{{
        {"TITLE", {"     "}}, {"COMMENT", {"  "}},
        {"DATE_RAW", {"1998-06-15"}},
        {"ARTIST", {"  Example   Artist  "}}
    }};
    const auto result = djmeta::Engine{}.analyze(source,
        {unicode_whitespace_rule(), trim_rule(), collapse_rule()}, "no-delete");
    require(result.canonical_preview.fields[0] == source.fields[0] &&
            result.canonical_preview.fields[1] == source.fields[1] &&
            result.canonical_preview.fields[2] == source.fields[2],
            "generic whitespace cleanup cannot erase a nonempty tag");
    require(result.proposals.size() == 1 &&
            result.proposals[0].field == "ARTIST" &&
            result.proposals[0].proposed_value == "Example Artist",
            "ordinary artist cleanup must stay active");
}

void test_shared_metadata_utf8_validation() {
    require(djmeta::valid_utf8_metadata_text("Tiësto — 東京"),
            "valid international UTF-8 accepted");
    require(djmeta::valid_utf8_metadata_text(""),
            "empty string itself valid UTF-8, deletion requires separate approval");
    for (const auto& malformed : {
        std::string("\xC2", 1), std::string("\x80", 1),
        std::string("\xC0\xAF", 2), std::string("\xED\xA0\x80", 3),
        std::string("\xF4\x90\x80\x80", 4)}) {
        require(!djmeta::valid_utf8_metadata_text(malformed),
                "invalid UTF-8 sequence rejected by shared validator");
    }
}

void test_rules_file_snapshot_revision_and_source_identity() {
    const djmeta::RulesTextSnapshot captured{
        R"({"revision":"user-revision","rules":[1,2]})",
        "C:/User/AppData/Roaming/DJMetadataNormalizer/ruleset.json"
    };
    require(djmeta::rules_snapshot_matches(
        captured, captured.json, captured.source_label),
        "same rules bytes and same source must preserve current preview");
    const auto reject = [&](const std::string& text,
                            const std::string& label) {
        require(!djmeta::rules_snapshot_matches(captured, text, label),
                "changed rule bytes or source must be stale");
        try {
            djmeta::require_rules_snapshot(captured, text, label);
            require(false, "stale rule snapshot was not refused");
        } catch (const std::invalid_argument&) {}
    };
    reject(R"({"revision":"user-revision","rules":[1,3]})",
           captured.source_label); // revision unchanged, rule semantics changed
    reject(captured.json, "embedded default ruleset"); // override vanished
    reject(captured.json + " ", captured.source_label); // byte-exact snapshot
    reject("", captured.source_label); // missing/invalid current source
    djmeta::require_rules_snapshot(
        captured, captured.json, captured.source_label);
}

void test_preview_is_immutable_and_ordered() {
    const djmeta::MetadataDocument input{{{"TITLE", {"  A   Title  "}}}};
    const djmeta::MetadataDocument input_copy = input;
    const auto result = djmeta::Engine{}.analyze(input, {collapse_rule(), trim_rule()}, "rules-v1-test");

    require(input == input_copy, "analysis mutated the input document");
    require(result.ruleset_revision == "rules-v1-test", "ruleset revision was not retained");
    require(result.changes.size() == 2, "expected trim + collapse trace");
    require(result.changes[0].rule_id == "safe.trim-whitespace", "priority order is not deterministic");
    require(result.changes[0].original_value == "  A   Title  ", "original value not retained");
    require(result.changes[0].before_value == "  A   Title  ", "first trace before-value mismatch");
    require(result.changes[0].proposed_value == "A   Title", "trim result mismatch");
    require(result.changes[1].original_value == "  A   Title  ", "raw original must survive chained rules");
    require(result.changes[1].before_value == "A   Title", "second trace before-value mismatch");
    require(result.changes[1].proposed_value == "A Title", "collapse result mismatch");
    require(result.canonical_preview.fields[0].values[0] == "A Title", "canonical preview mismatch");
    require(result.proposals.size() == 1, "chained rule trace should aggregate to one preview proposal");
    require(result.proposals[0].field_index == 0 && result.proposals[0].value_index == 0, "proposal identity mismatch");
    require(result.proposals[0].original_value == "  A   Title  ", "proposal original mismatch");
    require(result.proposals[0].proposed_value == "A Title", "proposal canonical value mismatch");
    require(result.proposals[0].rule_ids.size() == 2, "proposal rule provenance lost");
    require(result.proposals[0].safety == djmeta::SafetyClass::Safe, "proposal safety aggregation mismatch");
}

void test_unicode_whitespace_schema_v2() {
    const std::string value =
        std::string("\xC2\xA0") + "A" +
        std::string("\xE2\x80\xAF") + "  B" +
        std::string("\xE3\x80\x80");
    const djmeta::MetadataDocument input{{{"TITLE", {value}}}};
    const auto result = djmeta::Engine{}.analyze(
        input,
        {collapse_rule(), trim_rule(), unicode_whitespace_rule()},
        "rules-v2-unicode");

    require(result.changes.size() == 3, "unicode whitespace chain should normalize, trim, and collapse");
    require(result.changes[0].rule_id == "safe.normalize-unicode-whitespace",
            "unicode whitespace rule priority mismatch");
    require(result.changes[1].rule_id == "safe.trim-whitespace",
            "trim should run after unicode whitespace normalization");
    require(result.changes[2].rule_id == "safe.collapse-whitespace",
            "collapse should run after trim");
    require(result.canonical_preview.fields[0].values[0] == "A B",
            "unicode whitespace canonical preview mismatch");
    require(result.proposals.size() == 1, "unicode whitespace chain should aggregate to one proposal");
    require(result.proposals[0].safety == djmeta::SafetyClass::Safe,
            "unicode whitespace proposal must remain SAFE");

    const std::string malformed = std::string("A") + std::string("\xC2", 1) + "B";
    const djmeta::MetadataDocument malformed_input{{{"TITLE", {malformed}}}};
    const auto malformed_result = djmeta::Engine{}.analyze(
        malformed_input, {unicode_whitespace_rule()}, "malformed");
    require(malformed_result.changes.empty(),
            "malformed UTF-8 bytes must be preserved rather than rewritten");
    require(malformed_result.canonical_preview == malformed_input,
            "malformed UTF-8 preservation changed metadata");
}

void test_invalid_utf8_never_produces_generic_safe_whitespace_changes() {
    const std::vector<std::string> invalid{
        std::string("  Bad ") + std::string("\xC2", 1) + "  title ",
        std::string("  Stray ") + std::string("\x80", 1) + "  continuation ",
        std::string("  Overlong ") + std::string("\xC0\xAF", 2) + "  value ",
        std::string("  Surrogate ") + std::string("\xED\xA0\x80", 3) + "  value ",
        std::string("  TooHigh ") + std::string("\xF4\x90\x80\x80", 4) + "  value ",
        std::string("\xC2\xA0") + "  Broken " + std::string("\xC2", 1) + "   "
    };
    const djmeta::MetadataDocument input{{{"TITLE", invalid}, {"ARTIST", {"  Good  Artist  "}}}};
    const auto result = djmeta::Engine{}.analyze(input,
        {unicode_whitespace_rule(), trim_rule(), collapse_rule()}, "v2-malformed-guard");
    require(result.canonical_preview.fields[0] == input.fields[0],
            "malformed UTF-8 must not be partially rewritten by SAFE rules");
    require(result.proposals.size() == 1 &&
            result.proposals[0].field == "ARTIST" &&
            result.proposals[0].proposed_value == "Good Artist",
            "valid neighbor must normalize without emitting invalid-value proposals");
    for (const auto& change : result.changes)
        require(change.field == "ARTIST",
                "generic rule emitted an unsafe partial malformed UTF-8 change");
    require(result.canonical_preview.fields[1].values[0] == "Good Artist",
            "valid UTF-8 normalization unexpectedly disabled");
    const auto repeated = djmeta::Engine{}.analyze(input,
        {unicode_whitespace_rule(), trim_rule(), collapse_rule()}, "v2-malformed-guard");
    require(result.proposals == repeated.proposals,
            "malformed handling must remain deterministic");
}

void test_structured_cuesheet_and_multiline_fields_are_never_flattened() {
    const std::string cue =
        "FILE \"Album.flac\" WAVE\r\n"
        "  TRACK 01 AUDIO\r\n"
        "    INDEX 01 00:00:00\r\n"
        "  TRACK 02 AUDIO\r\n"
        "    INDEX 01 04:32:00\r\n";
    const std::string lyrics = "Verse  1\n  Second line\r\n";
    const std::string comment = "Line one  \nLine  two";
    const std::string unicode_lines =
        std::string("One") + "\xE2\x80\xA8" + "  Two";
    const djmeta::MetadataDocument input{{
        {"CUESHEET", {cue}},
        {"__cuesheet", {cue}},
        {"CUE_SHEET", {cue}},
        {"LYRICS", {lyrics}},
        {"UNSYNCEDLYRICS", {"  One  line  "}},
        {"COMMENT", {comment}},
        {"CUSTOM", {unicode_lines}},
        {"TITLE", {"  A   Title  "}}
    }};
    const auto copy = input;
    const auto result = djmeta::Engine{}.analyze(
        input, {unicode_whitespace_rule(), trim_rule(), collapse_rule()},
        "same-v2-ruleset-revision");
    require(input == copy, "structured input was modified");
    require(result.proposals.size() == 1 && result.changes.size() == 2,
            "generic SAFE cleanup must propose only scalar TITLE changes");
    require(result.canonical_preview.fields.back().values[0] == "A Title",
            "normal scalar TITLE normalization unexpectedly disabled");
    for (std::size_t i = 0; i < input.fields.size() - 1; ++i)
        require(result.canonical_preview.fields[i] == input.fields[i],
                "embedded CUE, lyrics, or line breaks must remain byte-identical");

    // Even an explicit CUESHEET rewrite uses the dedicated editor, not
    // the generic Metadata Normalizer projection.
    djmeta::Rule dangerous{
        "review.cue-rewrite", true, 1, {"CUESHEET"},
        djmeta::MatchKind::Always, "", false,
        djmeta::TransformKind::ReplaceWith, "broken cue",
        djmeta::SafetyClass::Review,
        "CUE must use the separate explicit editor", "manual", ""
    };
    const auto rejected = djmeta::Engine{}.analyze(input, {dangerous}, "cue-protected");
    require(rejected.proposals.empty() && rejected.canonical_preview == input,
            "generic rule must never write embedded CUESHEET field");

    // Explicitly selected semantic lyrics replacements remain REVIEW,
    // rather than being silently modified by wildcard whitespace rules.
    djmeta::Rule manual_lyrics{
        "review.lyrics", true, 1, {"LYRICS"},
        djmeta::MatchKind::Always, "", false,
        djmeta::TransformKind::ReplaceWith, "Corrected line",
        djmeta::SafetyClass::Review, "Manually reviewed lyrics", "manual", ""
    };
    const auto reviewed = djmeta::Engine{}.analyze(input, {manual_lyrics}, "lyrics-review");
    require(reviewed.proposals.size() == 1 &&
            reviewed.proposals[0].field == "LYRICS" &&
            reviewed.proposals[0].safety == djmeta::SafetyClass::Review,
            "explicit lyrics editing must require semantic review");
}

void test_exact_alias_and_safety() {
    djmeta::Rule alias{
        "review.label.defected-records", true, 100, {"LABEL"},
        djmeta::MatchKind::Exact, "defected records", false,
        djmeta::TransformKind::ReplaceWith, "Defected",
        djmeta::SafetyClass::Review,
        "Example only; collection aliases must be migrated from qualified user rules.",
        "manual", ""
    };
    const djmeta::MetadataDocument input{{{"label", {"Defected Records"}}}};
    const auto result = djmeta::Engine{}.analyze(input, {alias}, "r2");
    require(result.changes.size() == 1, "case-insensitive exact alias did not match");
    require(result.changes[0].safety == djmeta::SafetyClass::Review, "safety classification changed");
    require(result.canonical_preview.fields[0].values[0] == "Defected", "alias replacement mismatch");
}

void test_multivalue_preservation() {
    const djmeta::MetadataDocument input{{{"ARTIST", {"  Artist A  ", "Artist B"}}}};
    const auto result = djmeta::Engine{}.analyze(input, {trim_rule()}, "r3");
    require(result.changes.size() == 1, "only one multivalue element should change");
    require(result.changes[0].value_index == 0, "multivalue index lost");
    require(result.canonical_preview.fields[0].values.size() == 2, "multivalue cardinality changed");
    require(result.canonical_preview.fields[0].values[0] == "Artist A", "first multivalue not normalized");
    require(result.canonical_preview.fields[0].values[1] == "Artist B", "unchanged multivalue was modified");
}

void test_duplicate_field_names_keep_structural_identity() {
    const djmeta::MetadataDocument input{{
        {"CUSTOM", {"  first  "}},
        {"CUSTOM", {"  second  "}}
    }};
    const auto result = djmeta::Engine{}.analyze(input, {trim_rule()}, "dup-fields");
    require(result.changes.size() == 2, "duplicate fields should both retain independent trace");
    require(result.proposals.size() == 2, "duplicate fields should produce independent proposals");
    require(result.changes[0].field_index == 0 && result.changes[1].field_index == 1,
            "duplicate field trace lost field index");
    require(result.proposals[0].field_index == 0 && result.proposals[1].field_index == 1,
            "duplicate field proposals lost field index");
    require(result.proposals[0].proposed_value == "first", "first duplicate field proposal mismatch");
    require(result.proposals[1].proposed_value == "second", "second duplicate field proposal mismatch");
}

void test_disabled_rule() {
    auto rule = trim_rule();
    rule.enabled = false;
    const djmeta::MetadataDocument input{{{"TITLE", {"  X  "}}}};
    const auto result = djmeta::Engine{}.analyze(input, {rule}, "r4");
    require(result.changes.empty(), "disabled rule executed");
    require(result.canonical_preview == input, "disabled rule changed preview");
}

void test_equal_priority_is_stable_by_rule_id() {
    djmeta::Rule z{
        "z-rule", true, 50, {"TITLE"},
        djmeta::MatchKind::Exact, "Y", true,
        djmeta::TransformKind::ReplaceWith, "Z",
        djmeta::SafetyClass::Review, "ordering test", "manual", ""
    };
    djmeta::Rule a{
        "a-rule", true, 50, {"TITLE"},
        djmeta::MatchKind::Exact, "X", true,
        djmeta::TransformKind::ReplaceWith, "Y",
        djmeta::SafetyClass::Review, "ordering test", "manual", ""
    };

    const djmeta::MetadataDocument input{{{"TITLE", {"X"}}}};
    const auto result = djmeta::Engine{}.analyze(input, {z, a}, "r5");
    require(result.changes.size() == 2, "equal-priority rules should both execute in id order");
    require(result.changes[0].rule_id == "a-rule", "equal-priority rules must sort by stable rule id");
    require(result.changes[1].rule_id == "z-rule", "second equal-priority rule order mismatch");
    require(result.canonical_preview.fields[0].values[0] == "Z", "equal-priority chain result mismatch");
}

std::string read_text(const char* path) {
    std::ifstream in(path, std::ios::binary);
    require(static_cast<bool>(in), "could not open test ruleset");
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void require_invalid_ruleset(const std::string& json, const char* message) {
    try {
        (void)djmeta::parse_ruleset_json(json);
    } catch (const std::invalid_argument&) {
        return;
    }
    require(false, message);
}

void test_bridge_metadata_vector_loader() {
    const std::string json =
        R"([{"name":"ARTIST","values":["A","B"]},{"name":"ARTIST","values":["A; B"]},)"
        R"({"name":"EMPTY","values":[""]},{"name":"TITLE","values":["caf\u00e9"]}])";
    const auto doc = djmeta::parse_metadata_vectors_json(json);
    require(doc.fields.size() == 4, "metadata vector field count mismatch");
    require(doc.fields[0].name == "ARTIST" && doc.fields[0].values.size() == 2,
            "true multivalue metadata was not preserved");
    require(doc.fields[1].name == "ARTIST" && doc.fields[1].values[0] == "A; B",
            "duplicate field entry or literal separator was altered");
    require(doc.fields[2].values.size() == 1 && doc.fields[2].values[0].empty(),
            "empty metadata value was not preserved");
    require(doc.fields[3].values[0] == "caf\xC3\xA9", "metadata unicode escape decode mismatch");

    require_invalid_ruleset(
        R"({"schema_version":1,"ruleset_id":"x","revision":"r","rules":[],"unknown":true})",
        "unknown top-level property was accepted");

    try {
        (void)djmeta::parse_metadata_vectors_json(R"([{"name":"TITLE","values":[],"unknown":1}])");
        require(false, "unknown metadata-vector property was accepted");
    } catch (const std::invalid_argument&) {
    }
}

void test_persisted_ruleset_loader() {
    const auto ruleset = djmeta::parse_ruleset_json(read_text("rules/default-rules.json"));
    require(ruleset.schema_version == 2, "persisted schema version mismatch");
    require(ruleset.id == "lxsdd.dj-metadata-normalizer.default", "persisted ruleset id mismatch");
    require(ruleset.revision == "2026-10-07.2", "persisted ruleset revision mismatch");
    require(ruleset.rules.size() == 3, "persisted default rule count mismatch");
    require(ruleset.rules[0].source_kind == "builtin", "rule provenance was not loaded");

    const djmeta::MetadataDocument input{{{"TITLE", {"  A   B  "}}}};
    const auto result = djmeta::Engine{}.analyze(input, ruleset.rules, ruleset.revision);
    require(result.changes.size() == 2, "persisted default rules did not execute");
    require(result.canonical_preview.fields[0].values[0] == "A B", "persisted rules canonical preview mismatch");

    const std::string unicode =
        R"({"schema_version":1,"ruleset_id":"x","revision":"r","rules":[)"
        R"({"id":"review.x","enabled":true,"priority":1,"fields":["TITLE"],)"
        R"("match":{"kind":"exact","value":"caf\u00e9"},"transform":{"kind":"replace_with","replacement":"Cafe"},)"
        R"("safety":"REVIEW","source":{"kind":"manual","rationale":"caf\u00e9"}}]})";
    const auto parsed_unicode = djmeta::parse_ruleset_json(unicode);
    require(parsed_unicode.rules[0].match_value == "caf\xC3\xA9", "unicode escape did not decode as UTF-8");
    require(parsed_unicode.rules[0].rationale == "caf\xC3\xA9", "unicode provenance did not decode as UTF-8");

    const auto legacy_v1 = djmeta::parse_ruleset_json(
        R"({"schema_version":1,"ruleset_id":"legacy","revision":"r1","rules":[)"
        R"({"id":"safe.trim","enabled":true,"priority":1,"fields":["*"],"match":{"kind":"always"},)"
        R"("transform":{"kind":"trim_whitespace"},"safety":"SAFE","source":{"kind":"builtin","rationale":"legacy trim"}}]})");
    require(legacy_v1.schema_version == 1 && legacy_v1.rules.size() == 1,
            "schema-v1 backward compatibility was lost");

    require_invalid_ruleset(
        R"({"schema_version":1,"ruleset_id":"x","revision":"r","rules":[)"
        R"({"id":"safe.unicode","enabled":true,"priority":1,"fields":["*"],"match":{"kind":"always"},)"
        R"("transform":{"kind":"normalize_unicode_whitespace"},"safety":"SAFE","source":{"kind":"builtin","rationale":"v2 only"}}]})",
        "schema-v1 accepted a schema-v2-only transform");

    require_invalid_ruleset(
        R"({"schema_version":3,"ruleset_id":"x","revision":"r","rules":[]})",
        "unsupported schema version was accepted");
    require_invalid_ruleset(
        R"({"schema_version":1,"ruleset_id":"x","revision":"r","rules":[{"id":"safe.x","enabled":true,"priority":1,"fields":["TITLE"],"match":{"kind":"always"},"transform":{"kind":"replace_with","replacement":"X"},"safety":"SAFE","source":{"kind":"manual","rationale":"unsafe"}}]})",
        "semantic replacement was accepted as SAFE");
}

void test_disabled_semantic_candidates_are_opt_in() {
    const auto candidates = djmeta::parse_ruleset_json(
        read_text("rules/examples/semantic-review-candidates.json"));
    require(candidates.schema_version == 2 &&
            candidates.id == "lxsdd.dj-metadata-normalizer.semantic-candidates" &&
            candidates.rules.size() == 6, "illustrative semantic candidates schema changed");
    for (const auto& rule : candidates.rules) {
        require(!rule.enabled, "semantic example must be disabled by default");
        require(rule.safety == djmeta::SafetyClass::Review,
                "semantic example must require REVIEW, never SAFE/CONFIDENT");
        require(rule.match == djmeta::MatchKind::Exact &&
                rule.transform == djmeta::TransformKind::ReplaceWith,
                "semantic examples must match whole values exactly");
    }

    const djmeta::MetadataDocument source{{
        {"ARTIST", {"Example Artist Alias", "Unrelated Artist"}},
        {"LABEL", {"Example Label Records"}},
        {"TITLE", {"Example Track (Original Mix)"}},
        {"VERSION", {"Orig. Mix"}},
        {"GENRE", {"Example House Subgenre"}},
        {"ARTIST", {"Artist A feat. Artist B"}}
    }};
    const auto original_fingerprint = djmeta::fingerprint(source);
    const auto untouched = djmeta::Engine{}.analyze(
        source, candidates.rules, candidates.revision);
    require(untouched.proposals.empty() && untouched.changes.empty() &&
            untouched.canonical_preview == source,
            "disabled semantic candidates modified the preview");

    auto enabled_rules = candidates.rules;
    for (auto& rule : enabled_rules) rule.enabled = true;
    const auto reviewed = djmeta::Engine{}.analyze(
        source, enabled_rules, candidates.revision);
    require(reviewed.proposals.size() == enabled_rules.size(),
            "each enabled exact semantic candidate must emit one proposal");
    for (const auto& item : reviewed.proposals)
        require(item.safety == djmeta::SafetyClass::Review &&
                item.rule_ids.size() == 1 && !item.rationales.empty(),
                "semantic proposal must retain review safety and provenance");
    require(reviewed.canonical_preview.fields[0].values.size() == 2 &&
            reviewed.canonical_preview.fields[0].values[1] == "Unrelated Artist",
            "unrelated multivalue artist must be preserved");
    require(reviewed.canonical_preview.fields.back().values.size() == 1 &&
            reviewed.canonical_preview.fields.back().values[0] == "Artist A; Artist B",
            "literal feat example must not silently create real multivalue tags");
    require(djmeta::fingerprint(source) == original_fingerprint,
            "opt-in semantic analysis mutated source metadata");
    require(reviewed.ruleset_revision == candidates.revision,
            "rule revision provenance lost in semantic analysis");
}

void test_readonly_title_version_suffix_diagnostic() {
    using djmeta::MetadataDocument;
    using djmeta::TitleVersionReviewState;
    const MetadataDocument klf{{{"TITLE", {"The KLF — What Time Is Love (Original Version)"}},
                                 {"ARTIST", {"The KLF"}}}};
    const auto unchanged=klf;
    const auto before=djmeta::fingerprint(klf);
    const auto candidate=djmeta::review_title_version(klf);
    require(candidate.state==TitleVersionReviewState::Candidate &&
            candidate.suffix=="Original Version" &&
            candidate.base_title=="The KLF — What Time Is Love" &&
            candidate.version.empty(), "original version suffix read-only hint");
    require(klf==unchanged && djmeta::fingerprint(klf)==before,
            "title/version inspection must not mutate originals");
    const MetadataDocument same{{{"TITLE", {"Song [Extended Mix]"}},
                                 {"VERSION", {"extended mix"}}}};
    require(djmeta::review_title_version(same).state==
            TitleVersionReviewState::Consistent,
            "matching explicit VERSION does not propose a split");
    const MetadataDocument conflict{{{"TITLE", {"Song (Extended Mix)"}},
                                     {"VERSION", {"Radio Edit"}}}};
    const auto disputed=djmeta::review_title_version(conflict);
    require(disputed.state==TitleVersionReviewState::Conflict &&
            disputed.version=="Radio Edit" && disputed.suffix=="Extended Mix",
            "contradictory TITLE/VERSION retained for manual review");
    for(const auto& source: {
        MetadataDocument{{{"TITLE", {"Song (Live)"}}}},
        MetadataDocument{{{"TITLE", {"Song (1992)"}}}},
        MetadataDocument{{{"TITLE", {"Song(Extended Mix)"}}}},
        MetadataDocument{{{"TITLE", {"Song"}}}}
    }) require(djmeta::review_title_version(source).state==
               TitleVersionReviewState::NoEvidence,
               "unknown/unbalanced/undelimited suffix is not a version");
    for(const auto& source: {
        MetadataDocument{{{"TITLE", {"Song (Extended Mix) (Radio Edit)"}}}},
        MetadataDocument{{{"TITLE", {"Song (Original Mix)"}},
                          {"VERSION", {"Original Mix","Radio Edit"}}}},
        MetadataDocument{{{"TITLE", {"Song (Original Mix)"}},
                          {"TITLE", {"Other"}}}},
        MetadataDocument{{{"TITLE", {"Song (Original Mix)"}},
                          {"VERSION", {std::string("bad\0value",9)}}}},
        MetadataDocument{{{"TITLE", {"Song\n(Original Mix)"}}}}
    }) require(djmeta::review_title_version(source).state==
               TitleVersionReviewState::Ambiguous,
               "duplicate/multivalue/structural/unsafe mix identity fails closed");
    require(djmeta::review_title_version({}).state==
            TitleVersionReviewState::NoEvidence,
            "missing TITLE never fabricates a mix");
}

void test_fingerprint_contract() {
    const djmeta::MetadataDocument a{{{"ARTIST", {"A", "B"}}, {"TITLE", {"Track"}}}};
    const djmeta::MetadataDocument b = a;
    djmeta::MetadataDocument c = a;
    c.fields[0].values[1] = "C";

    const std::string fa = djmeta::fingerprint(a);
    require(fa.size() == 64, "fingerprint is not SHA-256 hex width");
    require(fa == djmeta::fingerprint(b), "equal documents must fingerprint equally");
    require(fa != djmeta::fingerprint(c), "metadata change must change fingerprint");
    const std::string empty = djmeta::fingerprint({});
    require(empty == "bc90e3740621acaeaf320fd210e1e592dcdb12f0f3e7e67a89c0b5e92bb3c8ad",
            "empty document SHA-256 regression vector changed");
}

} // namespace

int main() {
    test_preview_is_immutable_and_ordered();
    test_rules_file_snapshot_revision_and_source_identity();
    test_shared_metadata_utf8_validation();
    test_no_implicit_blank_tag_deletion();
    test_safe_scalar_structural_byte_guards();
    test_unicode_whitespace_schema_v2();
    test_structured_cuesheet_and_multiline_fields_are_never_flattened();
    test_invalid_utf8_never_produces_generic_safe_whitespace_changes();
    test_exact_alias_and_safety();
    test_multivalue_preservation();
    test_duplicate_field_names_keep_structural_identity();
    test_disabled_rule();
    test_equal_priority_is_stable_by_rule_id();
    test_bridge_metadata_vector_loader();
    test_persisted_ruleset_loader();
    test_fingerprint_contract();
    test_readonly_title_version_suffix_diagnostic();
    test_disabled_semantic_candidates_are_opt_in();
    std::cout << "PASS: djmeta core deterministic preview tests\n";
    return 0;
}
