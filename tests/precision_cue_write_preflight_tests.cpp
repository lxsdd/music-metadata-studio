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
}
}

int main() {
    export_planning_positive_and_negative();
    preflight_fail_closed_matrix();
    return 0;
}
