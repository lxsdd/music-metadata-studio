#include "djmeta/precision_cue_write_preflight.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <string>
#include <vector>

using namespace djmeta;

namespace {
PrecisionCuePlan synthetic_plan(std::string_view basename = "Disc Über 東京.mp3") {
    PrecisionCueDisc disc;
    disc.album_title = "Synthetic Mix – 東京";
    disc.album_performer = "Various Artists";
    disc.audio_file_basename = std::string(basename);
    disc.tracks = {
        {"Start (Mixed)", "DJ A", 100001},
        {"End (Mixed)", "DJ B", 200002},
    };
    return make_precision_cue(disc);
}

void export_planning_positive_and_negative() {
    const auto cue = synthetic_plan();
    const auto good = plan_precision_cue_external_export(cue, "Disc Über 東京.mp3");
    assert(good.eligible && good.target_cue_basename == "Disc Über 東京.cue");
    assert(precision_cue_export_targets_unique({good,
        plan_precision_cue_external_export(synthetic_plan("Disc 2.MP3"), "Disc 2.MP3")}));
    assert(!precision_cue_export_targets_unique({good, good}));

    auto wrong = plan_precision_cue_external_export(cue, "Other.mp3");
    assert(!wrong.eligible);
    wrong = plan_precision_cue_external_export(cue, "../escape.mp3");
    assert(!wrong.eligible);
    wrong = plan_precision_cue_external_export(cue, "CON.mp3");
    assert(!wrong.eligible);

    auto altered = cue;
    altered.external_cue_utf8 += "FILE \"unexpected.mp3\" WAVE\n";
    assert(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.external_cue_utf8 = "FILE \"Disc Über 東京.mp3\" WAVE\n";
    assert(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.external_cue_utf8[0] = static_cast<char>(0xff);
    assert(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.external_cue_utf8 += "  TRACK 03 AUDIO\n";
    assert(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.external_cue_utf8 += "    INDEX 01 03:20:00\n";
    assert(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.external_cue_utf8 += "    INDEX 00 00:00:00\n";
    assert(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    // Valid FILE inventories alone cannot protect a generated CUE from
    // syntactically plausible injected/reordered directives.
    const auto reject_changed_cue = [&](std::string_view before, std::string_view after) {
        auto changed = cue;
        const auto at = changed.external_cue_utf8.find(before);
        assert(at != std::string::npos);
        changed.external_cue_utf8.replace(at, before.size(), after);
        assert(!plan_precision_cue_external_export(changed, "Disc Über 東京.mp3").eligible);
    };
    reject_changed_cue("REM DISCNUMBER 1\n",
                       "REM DISCNUMBER 1\nREM COMMENT \"unexpected\"\n");
    reject_changed_cue("REM TOTALDISCS 1\n",
                       "REM TOTALDISCS 1\nTITLE \"injected\"\n");
    reject_changed_cue("  TRACK 01 AUDIO\n", "  TRACK 02 AUDIO\n");
    reject_changed_cue("  TRACK 01 AUDIO\n", "  TRACK 1 AUDIO\n");
    reject_changed_cue("    INDEX 01 00:00:00\n",
                       "    INDEX 01 00:00:00\n    INDEX 01 00:00:00\n");
    reject_changed_cue("  TRACK 02 AUDIO\n",
                       "  TRACK 02 AUDIO\n    REM COMMENT \"injected\"\n");
    reject_changed_cue("    TITLE \"Start (Mixed)\"\n",
                       "    TITLE \"Start (Mixed)\"\n    TITLE \"injected\"\n");
    reject_changed_cue("    TITLE \"Start (Mixed)\"\n",
                       "    TITLE \"Start (Mixed)\" INJECT\n");
    reject_changed_cue("REM DISCNUMBER 1\n", "REM DISCNUMBER 0\n");
    reject_changed_cue("REM DISCNUMBER 1\n", "REM DISCNUMBER 2\n");
    reject_changed_cue("REM TOTALDISCS 1\n", "REM TOTALDISCS 01\n");
    reject_changed_cue("    INDEX 01 00:00:00\n",
                       "    INDEX 01 00:00:00 \n");
    reject_changed_cue("REM DISCNUMBER 1\n",
                       "REM DATE \"2026\"\nREM GENRE \"Electronic\"\nREM DISCNUMBER 1\n");
    altered = cue;
    altered.boundaries[1].cue_frame++;
    assert(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.boundaries[0].elapsed_ms = 1;
    assert(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.total_duration_ms = altered.boundaries.back().elapsed_ms;
    assert(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    altered = cue;
    altered.boundaries.clear();
    assert(!plan_precision_cue_external_export(altered, "Disc Über 東京.mp3").eligible);
    assert(!precision_cue_export_targets_unique({good,
        plan_precision_cue_external_export(cue, "Other.mp3")}));
}

PrecisionCueWritePreflight approved() {
    PrecisionCueWritePreflight p;
    p.preview_available = true;
    p.preview_qualified = true;
    p.release_identity_confirmed = true;
    p.disc_identity_confirmed = true;
    p.explicit_user_approval = true;
    p.source_snapshot_current = true;
    p.target_snapshot_current = true;
    p.path_qualified = true;
    p.cue_content_qualified = true;
    return p;
}

void preflight_fail_closed_matrix() {
    using enum PrecisionCueWriteDecision;
    const PrecisionCueWritePreflight blank;
    assert(preflight_precision_cue_external_write(blank) == Reject);
    auto p = approved();
    assert(preflight_precision_cue_external_write(p) == ReadyForExplicitWrite);

    p = approved(); p.preview_available = false;
    assert(preflight_precision_cue_external_write(p) == Reject);
    p = approved(); p.path_qualified = false;
    assert(preflight_precision_cue_external_write(p) == Reject);
    p = approved(); p.cue_content_qualified = false;
    assert(preflight_precision_cue_external_write(p) == Reject);

    p = approved(); p.preview_qualified = false;
    assert(preflight_precision_cue_external_write(p) == ReviewRequired);
    p.explicit_review_resolved = true;
    assert(preflight_precision_cue_external_write(p) == ReadyForExplicitWrite);
    p.preview_available = false;
    assert(preflight_precision_cue_external_write(p) == Reject);

    p = approved(); p.release_identity_confirmed = false;
    assert(preflight_precision_cue_external_write(p) == ReviewRequired);
    p = approved(); p.disc_identity_confirmed = false;
    assert(preflight_precision_cue_external_write(p) == ReviewRequired);
    p = approved(); p.source_snapshot_current = false;
    assert(preflight_precision_cue_external_write(p) == ReviewRequired);
    p = approved(); p.target_snapshot_current = false;
    assert(preflight_precision_cue_external_write(p) == ReviewRequired);
    p = approved(); p.explicit_user_approval = false;
    assert(preflight_precision_cue_external_write(p) == ReviewRequired);

    p = approved(); p.target_exists = true;
    assert(preflight_precision_cue_external_write(p) == ReviewRequired);
    p.existing_content_known = true;
    assert(preflight_precision_cue_external_write(p) == ReviewRequired);
    p.overwrite_approved = true;
    assert(preflight_precision_cue_external_write(p) == ReadyForExplicitWrite);
    p.explicit_user_approval = false;
    assert(preflight_precision_cue_external_write(p) == ReviewRequired);
    p.existing_content_equal = true;
    assert(preflight_precision_cue_external_write(p) == NoOp);
    p.source_snapshot_current = false;
    assert(preflight_precision_cue_external_write(p) == ReviewRequired);
    p.source_snapshot_current = true; p.target_snapshot_current = false;
    assert(preflight_precision_cue_external_write(p) == ReviewRequired);
    p = approved(); p.existing_content_known = true;
    assert(preflight_precision_cue_external_write(p) == Reject);
    p = approved(); p.existing_content_equal = true;
    assert(preflight_precision_cue_external_write(p) == Reject);
    p = approved(); p.target_exists = true; p.existing_content_equal = true;
    assert(preflight_precision_cue_external_write(p) == Reject);
}
}


void sha256_exact_byte_regressions() {
    assert(precision_cue_sha256_hex("") ==
           "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    assert(precision_cue_sha256_hex("abc") ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(precision_cue_sha256_hex(std::string(64, 'a')) ==
           "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb");
    assert(precision_cue_sha256_hex(
           "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
           "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    const auto cue = synthetic_plan().external_cue_utf8;
    assert(precision_cue_sha256(cue) != precision_cue_sha256(cue + " "));
    assert(precision_cue_sha256(cue) != precision_cue_sha256(cue + "\\0"));
}

void commit_revalidation_fail_closed_matrix() {
    using enum PrecisionCueWriteDecision;
    PrecisionCueWriteCommitEvidence e;
    e.preview = approved();
    e.at_commit = approved();
    e.source_snapshot_token_preview = 101;
    e.source_snapshot_token_commit = 101;
    e.target_snapshot_token_preview = 202;
    e.target_snapshot_token_commit = 202;
    const auto reviewed = synthetic_plan().external_cue_utf8;
    e.reviewed_cue_sha256 = precision_cue_sha256(reviewed);
    e.at_commit_cue_sha256 = precision_cue_sha256(reviewed);
    assert(revalidate_precision_cue_external_commit(e) == ReadyForExplicitWrite);
    e.at_commit_cue_sha256 = precision_cue_sha256(reviewed + " ");
    assert(revalidate_precision_cue_external_commit(e) == ReviewRequired);
    e.at_commit_cue_sha256 = e.reviewed_cue_sha256;
    e.reviewed_cue_sha256 = {};
    assert(revalidate_precision_cue_external_commit(e) == ReviewRequired);
    e.reviewed_cue_sha256 = precision_cue_sha256(reviewed);
    e.source_snapshot_token_commit = 103;
    assert(revalidate_precision_cue_external_commit(e) == ReviewRequired);
    e.source_snapshot_token_commit = 101;
    e.target_snapshot_token_commit = 203;
    assert(revalidate_precision_cue_external_commit(e) == ReviewRequired);
    e.target_snapshot_token_commit = 202;
    e.target_snapshot_token_preview = 0;
    assert(revalidate_precision_cue_external_commit(e) == ReviewRequired);
    e.target_snapshot_token_preview = 202;
    e.at_commit.target_exists = true;
    assert(revalidate_precision_cue_external_commit(e) == ReviewRequired);
    e.at_commit = approved();
    e.at_commit.explicit_user_approval = false;
    assert(revalidate_precision_cue_external_commit(e) == ReviewRequired);
    e.at_commit = approved();
    e.preview.source_snapshot_current = false;
    assert(revalidate_precision_cue_external_commit(e) == ReviewRequired);
}

int main() {
    export_planning_positive_and_negative();
    preflight_fail_closed_matrix();
    sha256_exact_byte_regressions();
    commit_revalidation_fail_closed_matrix();
    return 0;
}
