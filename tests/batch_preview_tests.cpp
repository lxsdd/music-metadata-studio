#include "djmeta/batch_preview.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

void check(bool ok, const char* message) {
    if (!ok) { std::cerr << "FAIL: " << message << "\n"; std::exit(1); }
}
djmeta::BatchPreviewInputRow sample(const std::string& path) {
    djmeta::BatchPreviewInputRow row;
    row.source_path = path;
    row.physical_id = path;
    row.profile = "Singles";
    row.destination_root = "Z:\\Music\\Singles";
    row.raw_relative_path = "Test/Artist - Track";
    row.physical_source_qualified = true;
    return row;
}
bool contains(const djmeta::BatchPreviewRow& row, const std::string& text) {
    for (const auto& issue : row.issues) if (issue == text) return true;
    return false;
}

int main() {
    using djmeta::raw_relative_path_lexically_safe;
    for (const std::string& valid : {
             std::string("Album/Artist - Track.mp3"),
             std::string("A\\B\\C.flac"),
             std::string("Bj\xC3\xB6rk.mp3"),
             std::string("COM10.wav"),
             std::string("COM\xC2\xB9mix.wav"),
             std::string(".profile")})
        check(raw_relative_path_lexically_safe(valid),
              "legitimate relative path must pass lexical checks");
    for (const std::string& dangerous : {
             std::string("../escape.mp3"), std::string("A/../B.mp3"),
             std::string("./X.mp3"), std::string("/absolute.mp3"),
             std::string("C:\\\\absolute.mp3"), std::string("a//b.mp3"),
             std::string("a/"), std::string("foo:bar.mp3"),
             std::string("NUL.mp3"), std::string("con"),
             std::string("CON .mp3"), std::string("Aux  .cue"),
             std::string("com9 .wav"), std::string("A/COM\xC2\xB9.flac"),
             std::string("lpt\xC2\xB3.wav"),
             std::string("A/LpT9.txt"), std::string("x. "),
             std::string("x."), std::string("name?.mp3"),
             std::string("name|x"), std::string("line\nfeed.mp3"),
             std::string("nul\0byte.mp3", 12), std::string("\xC3\x28", 2)})
        check(!raw_relative_path_lexically_safe(dangerous),
              "unsafe Title Formatting output must not resemble an approved target");

    auto a = sample("a.mp3"), b = sample("b.mp3");
    const std::vector<djmeta::BatchPreviewInputRow> batch = {a,b};
    const auto c = djmeta::describe_batch_preview(batch);
    check(c.rows.size() == 2 && c.raw_collision_items == 2,
          "duplicate raw targets were not reported for both rows");
    check(contains(c.rows[0], "DUPLICATE_RAW_TARGET"),
          "duplicate target issue missing");
    check(contains(c.rows[1], "CUE_DEPENDENCIES_UNCHECKED") &&
          contains(c.rows[1], "FILESYSTEM_TARGET_UNCHECKED"),
          "unverified host/cue gates must remain visible");
    check(c.unqualified_rows == 2, "unqualified rows must be counted");

    // Different literal text can still name the same prospective Windows
    // target. This is not canonical FileOps path proof.
    a=sample("case-a.mp3");
    b=sample("case-b.mp3");
    b.destination_root="z:\\music\\singles";
    b.raw_relative_path="test\\artist - track";
    const auto spelling=djmeta::describe_batch_preview({a,b});
    check(spelling.raw_collision_items==2 &&
          spelling.rows[0].has_raw_target_collision &&
          spelling.rows[1].has_raw_target_collision &&
          contains(spelling.rows[0],"RAW_TARGET_CASE_OR_SEPARATOR_COLLISION") &&
          contains(spelling.rows[1],"RAW_TARGET_CASE_OR_SEPARATOR_COLLISION") &&
          !contains(spelling.rows[0],"DUPLICATE_RAW_TARGET"),
          "Windows ASCII case and separator spelling differences remain conflicts");
    b.raw_relative_path="Other\\Artist - Track";
    const auto genuinely_different=djmeta::describe_batch_preview({a,b});
    check(genuinely_different.raw_collision_items==0,
          "genuinely different raw Windows destinations are not falsely aliased");
    a=sample("a.mp3");
    b=sample("b.mp3");
    b.raw_relative_path = "Other/Artist - Track";
    b.semantic_proposals_pending = true;
    const auto differing = djmeta::describe_batch_preview({a,b});
    check(differing.raw_collision_items == 0 &&
          contains(differing.rows[1], "UNAPPROVED_METADATA_PROPOSALS"),
          "semantic REVIEW must be visible on a unique raw target");

    a.physical_source_qualified = false;
    a.raw_relative_path.clear();
    const auto invalid = djmeta::describe_batch_preview({a});
    check(contains(invalid.rows[0], "PHYSICAL_SOURCE_UNQUALIFIED") &&
          contains(invalid.rows[0], "TARGET_EXPRESSION_EMPTY"),
          "virtual/duplicate physical audio must not look ready");

    a = sample("a.mp3");
    a.raw_relative_path = "Album/../target.mp3";
    const auto unsafe = djmeta::describe_batch_preview({a});
    check(contains(unsafe.rows[0], "UNSAFE_RAW_RELATIVE_TARGET"),
          "unsafe raw paths must appear in batch preview diagnostics");
    // Raw observations are advisory, never enough to mark final host target
    // inspection as complete. Unknown enum and incomplete guards fail closed.
    a = sample("a.mp3");
    a.source_physical_key = "volume1:source";
    a.raw_target_presence = djmeta::RawTargetPresence::Missing;
    auto observed = djmeta::describe_batch_preview({a});
    check(observed.rows[0].raw_target_presence == djmeta::RawTargetPresence::Missing &&
          contains(observed.rows[0], "FILESYSTEM_TARGET_UNCHECKED"),
          "missing raw candidate cannot clear final target verification");

    a.raw_target_physical_key = "stale-physical-file";
    a.raw_target_guard = "stale-version";
    observed = djmeta::describe_batch_preview({a});
    check(contains(observed.rows[0], "RAW_TARGET_OBSERVATION_CONTRADICTORY"),
          "missing destination must not retain an existing physical file observation");
    a.raw_target_presence = djmeta::RawTargetPresence::NotInspected;
    observed = djmeta::describe_batch_preview({a});
    check(contains(observed.rows[0], "RAW_TARGET_OBSERVATION_CONTRADICTORY"),
          "not-inspected destination cannot retain a prior target identity");
    a.raw_target_physical_key.clear();
    a.raw_target_guard.clear();
    a.raw_target_presence = djmeta::RawTargetPresence::Existing;
    observed = djmeta::describe_batch_preview({a});
    check(contains(observed.rows[0], "RAW_TARGET_PROBE_UNQUALIFIED"),
          "an existing raw candidate without identity guard is unqualified");
    a.raw_target_physical_key = "volume1:unrelated-target";
    a.raw_target_guard = "observed-guard";
    observed = djmeta::describe_batch_preview({a});
    check(contains(observed.rows[0], "RAW_TARGET_EXISTS") &&
          contains(observed.rows[0], "FILESYSTEM_TARGET_UNCHECKED"),
          "existing raw candidate is reported but not approved");
    a.raw_target_physical_key = a.source_physical_key;
    observed = djmeta::describe_batch_preview({a});
    check(contains(observed.rows[0], "RAW_TARGET_ALIASES_SOURCE") &&
          !contains(observed.rows[0], "RAW_TARGET_EXISTS"),
          "raw candidate hardlink alias must not resemble an ordinary overwrite");
    a.raw_target_physical_key = "volume1:another-source";
    b = sample("b.mp3");
    b.source_physical_key = a.raw_target_physical_key;
    observed = djmeta::describe_batch_preview({a, b});
    check(contains(observed.rows[0], "RAW_TARGET_IS_BATCH_SOURCE"),
          "raw candidate must not target a different selected physical source");
    b.source_physical_key = "volume1:third-source";
    b.raw_target_presence = djmeta::RawTargetPresence::Existing;
    b.raw_target_physical_key = a.raw_target_physical_key;
    b.raw_target_guard = "other-observation-of-shared-file";
    observed = djmeta::describe_batch_preview({a, b});
    check(contains(observed.rows[0], "RAW_TARGET_SHARED_PHYSICAL_ID") &&
          contains(observed.rows[1], "RAW_TARGET_SHARED_PHYSICAL_ID"),
          "two distinct raw candidates with one physical ID must both be flagged");

    a.raw_target_presence = djmeta::RawTargetPresence::Unqualified;
    observed = djmeta::describe_batch_preview({a});
    check(contains(observed.rows[0], "RAW_TARGET_OBSERVATION_CONTRADICTORY"),
          "unqualified destination must not silently retain existing target ID");
    check(contains(observed.rows[0], "RAW_TARGET_PROBE_UNQUALIFIED"),
          "unsupported raw candidate must not pass silently");
    a.raw_target_presence = static_cast<djmeta::RawTargetPresence>(99);
    observed = djmeta::describe_batch_preview({a});
    check(contains(observed.rows[0], "RAW_TARGET_PROBE_UNQUALIFIED"),
          "unknown serialized raw candidate state must fail closed");

    a = sample("a.mp3");
    a.cue_dependencies_checked = true;
    a.filesystem_target_checked = true;
    const auto qualified = djmeta::describe_batch_preview({a});
    check(qualified.rows[0].issues.empty(), "fully qualified hints should clear");
    check(batch[0].physical_id == "a.mp3" && batch[1].physical_id == "b.mp3",
          "preview generation must not mutate original input");
    std::cout << "PASS: read-only batch preview statuses and raw collisions\n";
}
