#include "djmeta/batch_plan.h"
#include "djmeta/cue_association.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

void require(bool ok, const char* reason) {
    if (!ok) {
        std::cerr << "FAIL: " << reason << "\n";
        std::exit(1);
    }
}

bool has_reason(const djmeta::ItemDecision& decision, const std::string& code) {
    for (const auto& reason : decision.reasons) {
        if (reason == code) return true;
    }
    return false;
}

djmeta::FilePlanItem audio(const std::string& name) {
    djmeta::FilePlanItem item;
    item.physical_id = name;
    item.source_path = "Z:/Music/Downloads/" + name + ".mp3";
    item.source_key = "z:/music/downloads/" + name + ".mp3";
    item.source_guard = "source-observation-" + name;
    item.target_path = "Z:/Music/Singles/" + name + ".mp3";
    item.target_key = "z:/music/singles/" + name + ".mp3";
    item.action = djmeta::FileAction::Move;
    item.role = djmeta::FileRole::Audio;
    item.metadata_fingerprint = "raw-" + name;
    item.planned_metadata_fingerprint = "canonical-" + name;
    item.ruleset_revision = "2026-10-07.2";
    item.routing_profile = "Singles";
    item.naming_expression = "%artist% - %title%";
    item.target_presence = djmeta::TargetPresence::Missing;
    item.cue_links = djmeta::CueLinkState::NoExternalCue;
    return item;
}

void test_ready_and_immutable() {
    const std::vector<djmeta::FilePlanItem> batch = {audio("A"), audio("B")};
    const auto copy = batch;
    const auto result = djmeta::review_batch_plan(batch);
    require(batch[0].source_path == copy[0].source_path, "planner mutated input");
    require(result.plan_fingerprint.size() == 64, "plan fingerprint must be SHA-256");
    require(result.ready_to_apply && result.blocked == 0 &&
            result.requires_overwrite == 0, "clean batch must be ready");
    require(result.decisions[0].status == djmeta::PlanStatus::Ready,
            "clean item should be ready");
    require(djmeta::review_batch_plan(batch).plan_fingerprint == result.plan_fingerprint,
            "plan fingerprint must be deterministic");

    auto changed = batch;
    changed[0].manual_override = true;
    require(djmeta::review_batch_plan(changed).plan_fingerprint != result.plan_fingerprint,
            "manual routing override must change plan fingerprint");
    changed = batch;
    changed[0].planned_metadata_fingerprint = "different selected postimage";
    require(djmeta::review_batch_plan(changed).plan_fingerprint != result.plan_fingerprint,
            "selected metadata proposals must bind plan");
    changed = batch;
    changed[0].target_path += ".new";
    require(djmeta::review_batch_plan(changed).plan_fingerprint != result.plan_fingerprint,
            "target path edit must change plan");
    changed = batch;
    changed[0].routing_profile = "Alben";
    require(djmeta::review_batch_plan(changed).plan_fingerprint != result.plan_fingerprint,
            "routing profile edit must change plan");
}

void test_individual_overwrite_approval_selection_is_exact() {
    auto replace = audio("ApprovedExisting");
    replace.target_presence = djmeta::TargetPresence::Existing;
    replace.target_guard = "observed-existing-target-v1";
    replace.target_physical_id = "target-physical-existing";
    auto missing = audio("NotAnOverwrite");
    auto tag_only = audio("MetadataOnly");
    tag_only.action = djmeta::FileAction::None;
    tag_only.target_path.clear();
    tag_only.target_key.clear();
    auto exact_noop = audio("AlreadyAtTarget");
    exact_noop.target_path = exact_noop.source_path;
    exact_noop.target_key = exact_noop.source_key;
    exact_noop.target_presence = djmeta::TargetPresence::Existing;
    exact_noop.target_guard = "observed-same-source-target";
    exact_noop.target_physical_id = "self-os-file-id";
    const std::vector<djmeta::FilePlanItem> batch = {
        replace, missing, tag_only, exact_noop};
    const auto preview = djmeta::review_batch_plan(batch);
    require(preview.requires_overwrite == 1 && preview.blocked == 0,
            "only one existing destination needs explicit individual overwrite consent");
    const djmeta::BatchApproval exact{
        preview.plan_fingerprint, false, {"ApprovedExisting"}};
    const auto good = djmeta::review_batch_plan(batch, &exact);
    require(good.ready_to_apply && good.blocked == 0 &&
            good.decisions[0].will_replace_existing_target,
            "valid exact overwrite subset may approve the selected existing target");

    for (const std::vector<std::string>& ids : {
             std::vector<std::string>{"ApprovedExisting","UnknownId"},
             {"ApprovedExisting","ApprovedExisting"},
             {"NotAnOverwrite"},
             {"MetadataOnly"},
             {"AlreadyAtTarget"},
             {""}}) {
        const djmeta::BatchApproval unsafe{preview.plan_fingerprint,false,ids};
        const auto rejected = djmeta::review_batch_plan(batch,&unsafe);
        require(!rejected.ready_to_apply && rejected.blocked == batch.size(),
                "unknown, repeated, empty or non-overwrite selection must block whole batch");
        for (const auto& decision : rejected.decisions)
            require(decision.status == djmeta::PlanStatus::Blocked &&
                    has_reason(decision,"INVALID_OVERWRITE_APPROVAL_SELECTION") &&
                    !decision.will_replace_existing_target &&
                    !decision.metadata_write_needed &&
                    !decision.cue_bytes_write_needed &&
                    !decision.file_operation_needed,
                    "invalid overwrite subset must clear every mutation intent");
        try {
            (void)djmeta::freeze_reviewed_batch_preview(batch,unsafe);
            require(false,"invalid overwrite selection must not freeze");
        } catch(const std::invalid_argument&) {}
    }
    const djmeta::BatchApproval invalid_blanket{
        preview.plan_fingerprint,true,{"UnknownId"}};
    const auto blanket = djmeta::review_batch_plan(batch,&invalid_blanket);
    require(!blanket.ready_to_apply && blanket.blocked == batch.size(),
            "blanket overwrite cannot hide an invalid individually selected identifier");
}

void test_large_batch_one_confirmation() {
    std::vector<djmeta::FilePlanItem> batch;
    for (int i = 0; i < 100; ++i) {
        auto item = audio("Track" + std::to_string(i));
        item.target_presence = djmeta::TargetPresence::Existing;
        item.target_guard = "guard-" + std::to_string(i);
        item.target_physical_id = "target-file-" + std::to_string(i);
        batch.push_back(std::move(item));
    }
    const auto preview = djmeta::review_batch_plan(batch);
    require(preview.requires_overwrite == 100 &&
            !preview.ready_to_apply && preview.blocked == 0,
            "100 existing targets should yield 100 visible warnings, not blocks");

    const djmeta::BatchApproval approval{preview.plan_fingerprint, true};
    const auto approved = djmeta::review_batch_plan(batch, &approval);
    require(approved.ready_to_apply && approved.requires_overwrite == 100,
            "one batch-wide approval should approve all 100 reviewed replacements");
    for (const auto& item : approved.decisions)
        require(item.will_replace_existing_target,
                "all 100 reviewed replacements should be approved");
}

void test_batch_overwrite_once_and_target_guard() {
    auto a = audio("A");
    auto b = audio("B");
    a.target_presence = djmeta::TargetPresence::Existing;
    a.target_guard = "target-A-content-v1";
    a.target_physical_id = "existing-file-A";
    b.target_presence = djmeta::TargetPresence::Existing;
    b.target_guard = "target-B-content-v1";
    b.target_physical_id = "existing-file-B";
    std::vector<djmeta::FilePlanItem> batch = {a, b};

    const auto preview = djmeta::review_batch_plan(batch);
    require(!preview.ready_to_apply && preview.blocked == 0 &&
            preview.requires_overwrite == 2, "two existing targets require one batch consent");
    require(preview.decisions[0].status == djmeta::PlanStatus::NeedsOverwriteApproval,
            "existing target must raise a warning");

    const djmeta::BatchApproval approval{preview.plan_fingerprint, true};
    const auto approved = djmeta::review_batch_plan(batch, &approval);
    require(approved.ready_to_apply && approved.requires_overwrite == 2 &&
            approved.decisions[0].will_replace_existing_target &&
            approved.decisions[1].will_replace_existing_target,
            "one explicit batch approval must authorize both existing targets");

    batch[1].target_guard = "target-B-content-v2";
    const auto stale = djmeta::review_batch_plan(batch, &approval);
    require(!stale.ready_to_apply && stale.blocked == 2 &&
            has_reason(stale.decisions[0], "STALE_BATCH_APPROVAL") &&
            has_reason(stale.decisions[1], "STALE_BATCH_APPROVAL"),
            "changed destination guard must invalidate entire approval");

    batch = {a, b};
    djmeta::BatchApproval individual{
        djmeta::review_batch_plan(batch).plan_fingerprint, false, {"A"}};
    const auto mixed = djmeta::review_batch_plan(batch, &individual);
    require(mixed.decisions[0].status == djmeta::PlanStatus::Ready &&
            mixed.decisions[1].status == djmeta::PlanStatus::NeedsOverwriteApproval,
            "per-item consent must remain available");

    batch[0].target_guard += "-updated";
    const auto stale_individual = djmeta::review_batch_plan(batch, &individual);
    require(stale_individual.blocked == 2 &&
            !stale_individual.decisions[0].will_replace_existing_target,
            "per-item overwrite approval must also be guarded by plan fingerprint");
}

void test_many_individual_overwrite_approvals_are_indexed() {
    // Realistic large DJ library selection: each target already exists and
    // has its own inspected identity guard and explicit per-file approval.
    // The test is semantic, not a timing assertion (CI runners vary).
    constexpr int kTracks = 15000;
    std::vector<djmeta::FilePlanItem> items;
    items.reserve(kTracks);
    for (int i = 0; i < kTracks; ++i) {
        auto item = audio("library-" + std::to_string(i));
        item.target_presence = djmeta::TargetPresence::Existing;
        item.target_guard = "inspected-identity-" + std::to_string(i);
        item.target_physical_id = "target-real-physical-" + std::to_string(i);
        items.push_back(std::move(item));
    }
    const auto first = djmeta::review_batch_plan(items);
    require(first.blocked == 0 && first.requires_overwrite == kTracks &&
            !first.ready_to_apply,
            "large batch with existing destinations requires explicit approvals");
    djmeta::BatchApproval approval;
    approval.reviewed_plan_fingerprint = first.plan_fingerprint;
    approval.individually_approved_physical_ids.reserve(kTracks);
    for (int i = 0; i < kTracks; ++i)
        approval.individually_approved_physical_ids.push_back(
            "library-" + std::to_string(i));
    const auto accepted = djmeta::review_batch_plan(items, &approval);
    require(accepted.ready_to_apply && accepted.blocked == 0 &&
            accepted.requires_overwrite == kTracks,
            "15000 individually approved targets must be uniformly plannable");
    for (const auto& decision : accepted.decisions)
        require(decision.status == djmeta::PlanStatus::Ready &&
                decision.will_replace_existing_target,
                "each selected overwrite must have its own approved target");
    // One missing member of the indexed set cannot inherit neighboring consent.
    approval.individually_approved_physical_ids.pop_back();
    const auto partial = djmeta::review_batch_plan(items, &approval);
    require(!partial.ready_to_apply && partial.blocked == 0 &&
            partial.decisions.back().status ==
                djmeta::PlanStatus::NeedsOverwriteApproval &&
            !partial.decisions.back().will_replace_existing_target,
            "last unapproved target must not be covered by earlier approvals");
}

void test_missing_target_guard_and_no_inspection() {
    auto a = audio("A");
    a.target_presence = djmeta::TargetPresence::Unchecked;
    auto result = djmeta::review_batch_plan({a});
    require(result.blocked == 1 &&
            has_reason(result.decisions[0], "TARGET_NOT_INSPECTED"),
            "uninspected destination must fail closed");

    a.target_presence = djmeta::TargetPresence::Existing;
    a.target_guard.clear();
    a.target_physical_id = "existing-target-observation";
    result = djmeta::review_batch_plan({a});
    require(result.blocked == 1 &&
            has_reason(result.decisions[0], "EXISTING_TARGET_NOT_GUARDED"),
            "existing target without identity guard must fail closed");
}

void test_new_target_after_clean_review_requires_new_consent() {
    auto item = audio("A"); // target absent at preview time
    const auto preview = djmeta::review_batch_plan({item});
    require(preview.ready_to_apply, "absent target should initially be plannable");
    const djmeta::BatchApproval prior{preview.plan_fingerprint, true};

    // A third party creates the file between the preview and Apply.
    item.target_presence = djmeta::TargetPresence::Existing;
    item.target_guard = "newly-created-target";
    item.target_physical_id = "new-real-file";
    const auto changed = djmeta::review_batch_plan({item}, &prior);
    require(changed.blocked == 1 && !changed.ready_to_apply &&
            !changed.decisions[0].will_replace_existing_target &&
            has_reason(changed.decisions[0], "STALE_BATCH_APPROVAL"),
            "new destination after approval must not be silently overwritten");
}

void test_stale_approval_rejects_tag_only_and_noop_rows() {
    auto a = audio("TagOnly-A");
    a.action = djmeta::FileAction::None;
    a.target_key.clear();
    a.target_path.clear();
    a.target_presence = djmeta::TargetPresence::Unchecked;

    auto b = audio("TagOnly-B");
    b.action = djmeta::FileAction::None;
    b.target_key.clear();
    b.target_path.clear();

    const std::vector<djmeta::FilePlanItem> batch{a, b};
    const auto initial = djmeta::review_batch_plan(batch);
    require(initial.ready_to_apply && initial.blocked == 0 &&
            initial.decisions[0].status == djmeta::PlanStatus::Ready &&
            initial.decisions[1].status == djmeta::PlanStatus::Ready,
            "tag-only file plan must stay advisory and unchanged without a stale approval");

    const djmeta::BatchApproval valid{initial.plan_fingerprint, false};
    const auto same = djmeta::review_batch_plan(batch, &valid);
    require(same.ready_to_apply && same.blocked == 0,
            "matching approval must not block a legitimate unchanged file plan");

    auto mutated = batch;
    mutated[0].planned_metadata_fingerprint = "new-in-memory-tag-review";
    const auto stale = djmeta::review_batch_plan(mutated, &valid);
    require(!stale.ready_to_apply && stale.blocked == 2 &&
            has_reason(stale.decisions[0], "STALE_BATCH_APPROVAL") &&
            has_reason(stale.decisions[1], "STALE_BATCH_APPROVAL"),
            "changed tag postimage invalidates all batch approvals, even tag-only rows");

    mutated = batch;
    mutated[1].ruleset_revision = "new-rules-version";
    const auto stale_rules = djmeta::review_batch_plan(mutated, &valid);
    require(!stale_rules.ready_to_apply && stale_rules.blocked == 2 &&
            has_reason(stale_rules.decisions[0], "STALE_BATCH_APPROVAL") &&
            has_reason(stale_rules.decisions[1], "STALE_BATCH_APPROVAL"),
            "changed ruleset revision invalidates unchanged file rows too");

    // One active move plus a tag-only row: no silently excluded item
    // may escape the shared plan identity guard.
    mutated = batch;
    mutated[1].action = djmeta::FileAction::Move;
    mutated[1].target_path = "Z:/Music/Singles/TagOnly-B.mp3";
    mutated[1].target_key = "z:/music/singles/tagonly-b.mp3";
    mutated[1].target_presence = djmeta::TargetPresence::Missing;
    const auto mixed = djmeta::review_batch_plan(mutated, &valid);
    require(mixed.blocked == 2 && !mixed.ready_to_apply &&
            has_reason(mixed.decisions[0], "STALE_BATCH_APPROVAL") &&
            has_reason(mixed.decisions[1], "STALE_BATCH_APPROVAL"),
            "mixed move/tag-only stale batch cannot partially escape approval guard");
}

void test_intra_batch_conflicts_cannot_be_overridden() {
    auto a = audio("A");
    auto b = audio("B");
    b.target_key = a.target_key;
    b.target_path = a.target_path;
    auto batch = std::vector<djmeta::FilePlanItem>{a, b};
    auto preview = djmeta::review_batch_plan(batch);
    require(preview.blocked == 2 &&
            has_reason(preview.decisions[0], "MULTIPLE_SOURCES_SAME_TARGET"),
            "different sources with same destination must be blocked");

    const djmeta::BatchApproval allow{preview.plan_fingerprint, true};
    const auto still_blocked = djmeta::review_batch_plan(batch, &allow);
    require(still_blocked.blocked == 2 && !still_blocked.ready_to_apply,
            "batch overwrite must not pick a winning source");

    b = audio("B");
    b.action = djmeta::FileAction::None; // selected for tag-only updates
    a.target_key = b.source_key;
    a.target_path = b.source_path;
    preview = djmeta::review_batch_plan({a, b});
    require(has_reason(preview.decisions[0], "TARGET_IS_BATCH_SOURCE"),
            "cannot replace a selected tag-only file by a batch move");
}


void test_existing_target_physical_identity_and_hardlink_alias_gates() {
    auto a=audio("target-A");
    auto b=audio("target-B");
    a.target_presence=djmeta::TargetPresence::Existing;
    a.target_guard="existing-v1";
    a.target_physical_id="os-file-123";
    b.target_presence=djmeta::TargetPresence::Existing;
    b.target_guard="other-path-existing-v1";
    b.target_physical_id="os-file-123"; // two paths/hardlinks, SAME target
    const auto alias=djmeta::review_batch_plan({a,b});
    require(alias.blocked==2 && !alias.ready_to_apply &&
            has_reason(alias.decisions[0],"MULTIPLE_TARGETS_SAME_PHYSICAL_FILE") &&
            has_reason(alias.decisions[1],"MULTIPLE_TARGETS_SAME_PHYSICAL_FILE"),
            "two distinct destination paths cannot independently overwrite one hardlinked target");

    b.target_physical_id="os-file-456";
    const auto independent=djmeta::review_batch_plan({a,b});
    require(independent.blocked==0 && independent.requires_overwrite==2 &&
            !independent.ready_to_apply,
            "two truly independent observed target identities require explicit overwrites");
    const djmeta::BatchApproval approved{independent.plan_fingerprint,true,{}};
    require(djmeta::review_batch_plan({a,b},&approved).ready_to_apply,
            "exact source and target identities may enter advisory frozen plan");
    b.target_physical_id="os-file-789";
    const auto stale=djmeta::review_batch_plan({a,b},&approved);
    require(stale.blocked==2 &&
            has_reason(stale.decisions[0],"STALE_BATCH_APPROVAL"),
            "target physical file replacement invalidates previously approved plan");

    b.target_physical_id.clear();
    const auto uninspected=djmeta::review_batch_plan({a,b});
    require(uninspected.blocked>=1 &&
            has_reason(uninspected.decisions[1],
                "EXISTING_TARGET_PHYSICAL_ID_NOT_INSPECTED"),
            "existing destination cannot be approved without OS physical ID");

    b.target_physical_id=a.physical_id;
    const auto source_alias=djmeta::review_batch_plan({a,b});
    require(!source_alias.ready_to_apply &&
            has_reason(source_alias.decisions[1],
                "TARGET_IS_SELECTED_PHYSICAL_SOURCE"),
            "different destination path pointing to selected audio hardlink must be blocked");

    b.target_presence=djmeta::TargetPresence::Missing;
    const auto impossible=djmeta::review_batch_plan({a,b});
    require(has_reason(impossible.decisions[1],"TARGET_ID_WITHOUT_EXISTING_FILE"),
            "missing target cannot still carry a nonempty OS file ID");

    b.target_presence=djmeta::TargetPresence::Existing;
    b.target_physical_id="os-file-456";
    b.target_physical_id.push_back('\0');
    const auto nul=djmeta::review_batch_plan({a,b});
    require(has_reason(nul.decisions[1],"EMBEDDED_NUL_IN_PLAN_IDENTITY"),
            "embedded NUL in target physical ID cannot truncate approved identity");
}

void test_duplicate_subsong_source_and_self_copy() {
    auto a = audio("A");
    auto b = audio("B");
    b.source_key = a.source_key; // two virtual tracks, same physical file
    auto result = djmeta::review_batch_plan({a, b});
    require(result.blocked == 2 &&
            has_reason(result.decisions[0], "DUPLICATE_PHYSICAL_SOURCE"),
            "two virtual subsongs must not produce two file operations");

    a = audio("A");
    a.action = djmeta::FileAction::Copy;
    a.target_path = a.source_path;
    a.target_key = a.source_key;
    result = djmeta::review_batch_plan({a});
    require(result.blocked == 1 &&
            has_reason(result.decisions[0], "COPY_SOURCE_EQUALS_TARGET"),
            "copy-to-self must not be allowed");
}

void test_duplicate_selected_tag_only_source_cannot_escape_preflight() {
    auto active = audio("Shared");
    auto tag_only = active;
    tag_only.action = djmeta::FileAction::None;
    tag_only.target_key.clear();
    tag_only.target_path.clear();
    // A selected physical file also appears as a second metadata-only row.
    // Even though only one row moves a file, writing decisions must not
    // silently bind two selections of the same physical audio.
    const auto shared = djmeta::review_batch_plan({active, tag_only});
    require(shared.blocked == 2 && !shared.ready_to_apply &&
            has_reason(shared.decisions[0], "DUPLICATE_PHYSICAL_SOURCE") &&
            has_reason(shared.decisions[1], "DUPLICATE_PHYSICAL_SOURCE") &&
            has_reason(shared.decisions[1], "DUPLICATE_PHYSICAL_ID"),
            "tag-only duplicate of active physical source must block both rows");

    // Different host paths can still report the same physical identity
    // (hardlink/alias). Treat it as one physical file, not two approvals.
    tag_only.source_key = "different-canonical-path";
    const auto alias = djmeta::review_batch_plan({active, tag_only});
    require(alias.blocked == 2 && !alias.ready_to_apply &&
            has_reason(alias.decisions[0], "DUPLICATE_PHYSICAL_ID") &&
            has_reason(alias.decisions[1], "DUPLICATE_PHYSICAL_ID"),
            "selected alias with same physical identity must block both rows");

    // Duplicated metadata-only selections cannot be implicitly considered
    // Unchanged/Ready by the read-only planner.
    active.action = djmeta::FileAction::None;
    const auto tag_only_twice = djmeta::review_batch_plan({active, tag_only});
    require(tag_only_twice.blocked == 2 && !tag_only_twice.ready_to_apply,
            "two metadata-only rows with same physical identity must block");
    const djmeta::BatchApproval blanket{shared.plan_fingerprint, true};
    const auto still = djmeta::review_batch_plan({active, tag_only}, &blanket);
    require(still.blocked == 2 && !still.ready_to_apply,
            "blanket approval cannot override source identity conflicts");

    const auto independent = djmeta::review_batch_plan({audio("A"), audio("B")});
    require(independent.ready_to_apply,
            "distinct physical identities remain independently plannable");
}

void test_external_cue_dependencies() {
    auto a = audio("A");
    a.cue_links = djmeta::CueLinkState::Unchecked;
    auto result = djmeta::review_batch_plan({a});
    require(result.blocked == 1 &&
            has_reason(result.decisions[0], "CUE_DEPENDENCY_UNQUALIFIED"),
            "audio rename without cue inspection must be blocked");

    a.cue_links = djmeta::CueLinkState::Verified;
    result = djmeta::review_batch_plan({a});
    require(has_reason(result.decisions[0], "VERIFIED_CUE_NOT_IN_PLAN"),
            "verified cue must be represented by a separate planned item");

    djmeta::FilePlanItem cue;
    cue.physical_id = "cue-A";
    cue.source_path = "Z:/Music/Downloads/A.cue";
    cue.source_key = "z:/music/downloads/a.cue";
    cue.source_guard = "source-observation-" + cue.source_key;
    cue.target_path = "Z:/Music/Singles/A.cue";
    cue.target_key = "z:/music/singles/a.cue";
    cue.action = djmeta::FileAction::Move;
    cue.role = djmeta::FileRole::ExternalCue;
    cue.associated_audio_id = a.physical_id;
    cue.ruleset_revision = a.ruleset_revision;
    cue.cue_links = djmeta::CueLinkState::Verified;
    cue.cue_source_fingerprint = "cue-exact-source-bytes";
    cue.cue_postimage_fingerprint = "cue-renamed-FILE-reference-bytes";
    cue.cue_references_will_change = true;
    cue.target_presence = djmeta::TargetPresence::Missing;

    std::vector<djmeta::FilePlanItem> batch = {a, cue};
    result = djmeta::review_batch_plan(batch);
    require(result.ready_to_apply, "verified CUE + audio move should be plannable");
    const djmeta::BatchApproval approval{result.plan_fingerprint, true};

    batch[1].cue_postimage_fingerprint += "changed";
    result = djmeta::review_batch_plan(batch, &approval);
    require(result.blocked == 2 &&
            has_reason(result.decisions[1], "STALE_BATCH_APPROVAL"),
            "modified CUE FILE reference plan must invalidate approval");

    batch[1].cue_source_fingerprint.clear();
    result = djmeta::review_batch_plan(batch);
    require(has_reason(result.decisions[1], "CUE_REFERENCE_PLAN_UNQUALIFIED"),
            "unverified CUE source bytes must block operation");
}


void test_multifile_cue_planned_audio_dependencies() {
    auto a=audio("first");
    auto b=audio("second");
    a.cue_links=djmeta::CueLinkState::Verified;
    b.cue_links=djmeta::CueLinkState::Verified;
    auto cue=audio("mix-cue");
    cue.role=djmeta::FileRole::ExternalCue;
    cue.source_path="Z:/Music/Downloads/multi.cue";
    cue.source_key="z:/music/downloads/multi.cue";
    cue.target_path="Z:/Music/Singles/multi.cue";
    cue.target_key="z:/music/singles/multi.cue";
    cue.cue_links=djmeta::CueLinkState::Verified;
    // Full source-derived chain: parse the two FILE references, resolve
    // each canonical host physical source, stage only the verified CUE
    // reference postimage and pass its association into batch preflight.
    const std::string source_cue =
        "REM GENRE House\n"
        "FILE \"first.wav\" WAVE\nTRACK 01 AUDIO\n"
        "FILE \"second.wav\" WAVE\nTRACK 02 AUDIO\n";
    const auto cue_link=djmeta::qualify_external_cue_association(
        source_cue,
        {{0,"first.wav",a.source_key,"first-new.wav",true,true},
         {1,"second.wav",b.source_key,"second-new.wav",true,true}},
        {{a.physical_id,a.source_key,a.target_key},
         {b.physical_id,b.source_key,b.target_key}});
    require(cue_link.qualified && cue_link.changes_references &&
            cue_link.related_selected_audio_ids==
                std::vector<std::string>({a.physical_id,b.physical_id}) &&
            cue_link.proposed_cue_bytes.find("second-new.wav")!=std::string::npos,
            "parsed Multi-FILE references map both real physical source identities");
    cue.associated_audio_ids=cue_link.related_selected_audio_ids;
    cue.cue_source_fingerprint=cue_link.original_cue_fingerprint;
    cue.cue_postimage_fingerprint=cue_link.proposed_cue_fingerprint;
    cue.cue_references_will_change=cue_link.changes_references;
    cue.metadata_fingerprint.clear();
    cue.planned_metadata_fingerprint.clear();
    std::vector<djmeta::FilePlanItem> items={a,b,cue};
    const auto ok=djmeta::review_batch_plan(items);
    require(ok.ready_to_apply && ok.blocked==0 &&
            ok.decisions[2].cue_bytes_write_needed,
            "one external CUE with two qualified physical audio references");
    const djmeta::BatchApproval approval{ok.plan_fingerprint,false,{}};
    const auto frozen=djmeta::freeze_reviewed_batch_preview(items,approval);
    require(frozen.items.size()==3 && frozen.items[2].associated_audio_ids.size()==2 &&
            frozen.items[2].cue_postimage_fingerprint==
                cue_link.proposed_cue_fingerprint,
            "frozen plan retains actual source-derived CUE bytes and both audio dependencies");
    auto changed=items;
    changed[2].associated_audio_ids={b.physical_id,a.physical_id};
    const auto stale=djmeta::review_batch_plan(changed,&approval);
    require(stale.blocked==3 &&
            has_reason(stale.decisions[2],"STALE_BATCH_APPROVAL"),
            "reordering Multi-FILE members invalidates exact approval");
    // An updated remote/local FILE-reference proposal is a new plan even
    // if every audio row still refers to its earlier physical destination.
    changed=items;
    changed[2].cue_postimage_fingerprint="changed FILE postimage";
    const auto renamed=djmeta::review_batch_plan(changed,&approval);
    require(renamed.blocked==3 &&
            has_reason(renamed.decisions[2],"STALE_BATCH_APPROVAL"),
            "CUE reference postimage change invalidates multi-file approval");

    changed=items;
    changed[2].associated_audio_ids={a.physical_id};
    const auto incomplete=djmeta::review_batch_plan(changed);
    require(incomplete.blocked>=1 &&
            has_reason(incomplete.decisions[1],"VERIFIED_CUE_NOT_IN_PLAN"),
            "dropping second selected physical FILE cannot silently qualify plan");
    changed=items;
    changed[2].associated_audio_ids.push_back("absent-file");
    const auto missing=djmeta::review_batch_plan(changed);
    require(!missing.ready_to_apply &&
            has_reason(missing.decisions[2],"CUE_REFERENCE_PLAN_UNQUALIFIED"),
            "unselected FILE identity blocks the CUE");
    changed=items;
    changed[2].associated_audio_ids={a.physical_id,a.physical_id};
    const auto duplicate=djmeta::review_batch_plan(changed);
    require(!duplicate.ready_to_apply &&
            has_reason(duplicate.decisions[2],"CUE_AUDIO_RELATIONSHIP_AMBIGUOUS"),
            "duplicate physical source in Multi-FILE CUE blocks");
    changed=items;
    changed[2].associated_audio_id="different-legacy-id";
    const auto contradiction=djmeta::review_batch_plan(changed);
    require(!contradiction.ready_to_apply &&
            has_reason(contradiction.decisions[2],"CUE_AUDIO_RELATIONSHIP_AMBIGUOUS"),
            "legacy and multi-file relationships cannot contradict");
    changed=items;
    changed[1].cue_links=djmeta::CueLinkState::NoExternalCue;
    const auto denied=djmeta::review_batch_plan(changed);
    require(!denied.ready_to_apply &&
            has_reason(denied.decisions[1],"CUE_ASSOCIATION_CONTRADICTS_NO_EXTERNAL_CUE") &&
            has_reason(denied.decisions[2],"CUE_REFERENCE_PLAN_UNQUALIFIED"),
            "one contradictory FILE participant blocks complete plan");
    changed=items;
    changed[2].associated_audio_ids[1].push_back('\0');
    const auto nul=djmeta::review_batch_plan(changed);
    require(!nul.ready_to_apply &&
            has_reason(nul.decisions[2],"EMBEDDED_NUL_IN_PLAN_IDENTITY"),
            "NUL in second FILE association cannot bypass guard");
    changed=items;
    changed[0].associated_audio_ids={b.physical_id};
    const auto wrongRole=djmeta::review_batch_plan(changed);
    require(!wrongRole.ready_to_apply &&
            has_reason(wrongRole.decisions[0],"CUE_AUDIO_RELATIONSHIP_AMBIGUOUS"),
            "non-CUE file cannot declare multi-FILE relation");
}

void test_contradictory_audio_cue_evidence_rejected() {
    auto source = audio("contradictory");
    djmeta::FilePlanItem cue;
    cue.physical_id = "contradictory-cue";
    cue.source_path = "Z:/Music/Downloads/contradictory.cue";
    cue.source_key = "z:/music/downloads/contradictory.cue";
    cue.source_guard = "source-observation-" + cue.source_key;
    cue.target_path = "Z:/Music/Singles/contradictory.cue";
    cue.target_key = "z:/music/singles/contradictory.cue";
    cue.action = djmeta::FileAction::Move;
    cue.role = djmeta::FileRole::ExternalCue;
    cue.associated_audio_id = source.physical_id;
    cue.ruleset_revision = source.ruleset_revision;
    cue.cue_links = djmeta::CueLinkState::Verified;
    cue.cue_source_fingerprint = "verified-source-cue-bytes";
    cue.cue_postimage_fingerprint = "verified-proposed-cue-bytes";
    cue.cue_references_will_change = true;
    cue.target_presence = djmeta::TargetPresence::Missing;

    // The host cannot simultaneously assert "no external CUE" and supply
    // an explicitly associated, active CUE dependency in the same plan.
    const auto contradict = djmeta::review_batch_plan({source, cue});
    require(contradict.blocked == 2 && !contradict.ready_to_apply &&
            has_reason(contradict.decisions[0],
                "CUE_ASSOCIATION_CONTRADICTS_NO_EXTERNAL_CUE") &&
            has_reason(contradict.decisions[1],
                "CUE_REFERENCE_PLAN_UNQUALIFIED"),
            "contradictory audio/CUE association must block both sides");

    const djmeta::BatchApproval approval{contradict.plan_fingerprint, true};
    const auto still_blocked = djmeta::review_batch_plan({source, cue}, &approval);
    require(still_blocked.blocked == 2 && !still_blocked.ready_to_apply,
            "blanket overwrite approval cannot repair CUE identity contradiction");

    source.cue_links = djmeta::CueLinkState::Verified;
    const auto qualified = djmeta::review_batch_plan({source, cue});
    require(qualified.ready_to_apply && qualified.blocked == 0,
            "mutually qualified CUE and audio evidence must retain a clean preview");

    // A selected tag-only audio row still has a real physical identity:
    // the associated CUE must not bypass its negative cue assertion.
    source.action = djmeta::FileAction::None;
    const auto tag_only = djmeta::review_batch_plan({source, cue});
    require(tag_only.decisions[1].status == djmeta::PlanStatus::Ready,
            "verified tag-only audio can still anchor an associated CUE plan");
    source.cue_links = djmeta::CueLinkState::NoExternalCue;
    const auto tag_only_conflict = djmeta::review_batch_plan({source, cue});
    require(tag_only_conflict.blocked == 2 &&
            has_reason(tag_only_conflict.decisions[0],
                "CUE_ASSOCIATION_CONTRADICTS_NO_EXTERNAL_CUE") &&
            has_reason(tag_only_conflict.decisions[1],
                "CUE_REFERENCE_PLAN_UNQUALIFIED"),
            "tag-only audio and its contradictory CUE must both be blocked");
}

void test_inactive_cue_relationships_and_tag_only_audio_guard() {
    auto tag_only = audio("cue-tag-only");
    tag_only.action = djmeta::FileAction::None;
    tag_only.target_path.clear();
    tag_only.target_key.clear();

    tag_only.cue_links = djmeta::CueLinkState::Unchecked;
    auto result = djmeta::review_batch_plan({tag_only});
    require(result.blocked == 1 && !result.ready_to_apply &&
            has_reason(result.decisions[0], "CUE_DEPENDENCY_UNQUALIFIED"),
            "metadata-only edit must not skip unchecked CUE dependency");

    tag_only.cue_links = djmeta::CueLinkState::Unresolved;
    result = djmeta::review_batch_plan({tag_only});
    require(result.blocked == 1 &&
            has_reason(result.decisions[0], "CUE_DEPENDENCY_UNQUALIFIED"),
            "metadata-only edit must reject unresolved external CUE links");

    tag_only.cue_links = djmeta::CueLinkState::NoExternalCue;
    djmeta::FilePlanItem cue;
    cue.physical_id = "inactive-cue";
    cue.role = djmeta::FileRole::ExternalCue;
    cue.action = djmeta::FileAction::None;
    cue.associated_audio_id = tag_only.physical_id;
    cue.source_path = "Z:/Music/Downloads/cue-tag-only.cue";
    cue.source_key = "z:/music/downloads/cue-tag-only.cue";
    cue.source_guard = "source-observation-" + cue.source_key;
    cue.ruleset_revision = tag_only.ruleset_revision;
    cue.cue_links = djmeta::CueLinkState::Verified;
    cue.cue_source_fingerprint = "unchanged-cue-source";
    cue.cue_postimage_fingerprint = "unchanged-cue-source";

    result = djmeta::review_batch_plan({tag_only, cue});
    require(result.blocked >= 1 && !result.ready_to_apply &&
            has_reason(result.decisions[0],
                "CUE_ASSOCIATION_CONTRADICTS_NO_EXTERNAL_CUE"),
            "an inactive linked CUE still contradicts a no-external-CUE assertion");

    tag_only.cue_links = djmeta::CueLinkState::Verified;
    result = djmeta::review_batch_plan({tag_only, cue});
    require(result.ready_to_apply && result.blocked == 0,
            "tag-only audio with verified inactive CUE does not need a FILE rewrite");

    tag_only.action = djmeta::FileAction::Move;
    tag_only.target_path = "Z:/Music/Singles/cue-tag-only.mp3";
    tag_only.target_key = "z:/music/singles/cue-tag-only.mp3";
    tag_only.target_presence = djmeta::TargetPresence::Missing;
    result = djmeta::review_batch_plan({tag_only, cue});
    require(result.blocked == 1 && !result.ready_to_apply &&
            has_reason(result.decisions[0], "VERIFIED_CUE_NOT_IN_PLAN"),
            "inactive CUE listing cannot qualify a real audio move");
}

void test_multiple_external_cue_carriers_per_audio_block_batch() {
    auto track=audio("same-physical-audio");
    track.cue_links=djmeta::CueLinkState::Verified;
    auto first=audio("cue-one");
    first.role=djmeta::FileRole::ExternalCue;
    first.associated_audio_id=track.physical_id;
    first.cue_links=djmeta::CueLinkState::Verified;
    first.cue_source_fingerprint="first-before";
    first.cue_postimage_fingerprint="first-after";
    first.cue_references_will_change=true;
    first.metadata_fingerprint.clear();
    first.planned_metadata_fingerprint.clear();
    auto second=first;
    second.physical_id="cue-two";
    second.source_path="Z:/Music/Downloads/cue-two.cue";
    second.source_key="z:/music/downloads/cue-two.cue";
    second.source_guard="source-observation-cue-two";
    second.target_path="Z:/Music/Singles/cue-two.cue";
    second.target_key="z:/music/singles/cue-two.cue";
    second.cue_source_fingerprint="second-before";
    second.cue_postimage_fingerprint="second-after";
    const std::vector<djmeta::FilePlanItem> batch{track,first,second};
    const auto conflict=djmeta::review_batch_plan(batch);
    require(conflict.blocked==3 && !conflict.ready_to_apply,
            "two independently writable CUEs cannot share one physical audio");
    for(const auto& decision:conflict.decisions)
        require(has_reason(decision,"MULTIPLE_EXTERNAL_CUES_FOR_AUDIO") &&
                !decision.cue_bytes_write_needed &&
                !decision.metadata_write_needed &&
                !decision.file_operation_needed,
                "all conflicted audio/CUE participants must fail closed");

    // Even a static CUE carrier is a relationship that requires explicit
    // disambiguation; file-action None is NOT permission to ignore it.
    auto inactive=batch;
    inactive[2].action=djmeta::FileAction::None;
    inactive[2].cue_references_will_change=false;
    inactive[2].cue_postimage_fingerprint=inactive[2].cue_source_fingerprint;
    const auto with_inactive=djmeta::review_batch_plan(inactive);
    require(with_inactive.blocked==3 &&
            has_reason(with_inactive.decisions[0],
                "MULTIPLE_EXTERNAL_CUES_FOR_AUDIO") &&
            has_reason(with_inactive.decisions[2],
                "MULTIPLE_EXTERNAL_CUES_FOR_AUDIO"),
            "inactive second CUE still conflicts with audio in file operation");

    // Overwrite approval is not an ambiguity override.
    const djmeta::BatchApproval unsafe_consent{conflict.plan_fingerprint,true,{}};
    const auto consented=djmeta::review_batch_plan(batch,&unsafe_consent);
    require(!consented.ready_to_apply && consented.blocked==3,
            "even exact batch-wide overwrite consent cannot approve ambiguous CUE graph");

    // One CUE carrying two distinct FILE identities is a valid many-to-one
    // relation; only two DIFFERENT CUE carriers for a single source conflict.
    auto other=audio("second-track");
    other.cue_links=djmeta::CueLinkState::Verified;
    first.associated_audio_id.clear();
    first.associated_audio_ids={track.physical_id,other.physical_id};
    const auto valid=djmeta::review_batch_plan({track,other,first});
    require(valid.ready_to_apply && valid.blocked==0,
            "one external Multi-FILE CUE may reference two separate audio files");

    // Distinct CUEs each protecting a distinct audio are valid together.
    auto other_cue=second;
    other_cue.associated_audio_id=other.physical_id;
    const auto independent=djmeta::review_batch_plan(
        {track,other,first,other_cue});
    require(!independent.ready_to_apply &&
            has_reason(independent.decisions[1],
                "MULTIPLE_EXTERNAL_CUES_FOR_AUDIO"),
            "multi-file CUE plus another CUE for one of its files remains ambiguous");
    first.associated_audio_ids={track.physical_id};
    first.associated_audio_id.clear();
    const auto independent_clean=djmeta::review_batch_plan(
        {track,other,first,other_cue});
    require(independent_clean.ready_to_apply,
            "independent CUE/audio pairs remain valid in one batch");
}

void test_cue_rewrite_only_on_external_cue_items() {
    auto audio_item = audio("wrong-cue-role");
    audio_item.action = djmeta::FileAction::None;
    audio_item.cue_links = djmeta::CueLinkState::NoExternalCue;
    audio_item.cue_references_will_change = true;
    auto result = djmeta::review_batch_plan({audio_item});
    require(result.blocked == 1 && !result.ready_to_apply &&
            has_reason(result.decisions[0], "CUE_REWRITE_ON_NON_CUE_ITEM"),
            "audio row must never act as a CUE FILE-reference rewrite");

    auto companion = audio_item;
    companion.role = djmeta::FileRole::Companion;
    companion.physical_id = "cover-art";
    companion.source_path = "Z:/Music/Downloads/cover.jpg";
    companion.source_key = "z:/music/downloads/cover.jpg";
    companion.source_guard = "source-observation-" + companion.source_key;
    companion.associated_audio_id = "wrong-cue-role";
    companion.companion_policy_qualified = true;
    companion.cue_links = djmeta::CueLinkState::Unchecked;
    audio_item.cue_references_will_change = false;
    const auto mixed = djmeta::review_batch_plan({audio_item, companion});
    require(mixed.blocked == 1 &&
            has_reason(mixed.decisions[1], "CUE_REWRITE_ON_NON_CUE_ITEM"),
            "companion file cannot smuggle CUE rewrites through a qualified sidecar policy");

    // A verified external CUE is permitted to preview just a FILE-reference
    // rewrite without renaming/moving the physical cue file itself.
    auto cue = companion;
    cue.role = djmeta::FileRole::ExternalCue;
    cue.cue_links = djmeta::CueLinkState::Verified;
    cue.cue_source_fingerprint = "verified-cue-input";
    cue.cue_postimage_fingerprint = "verified-cue-output";
    cue.companion_policy_qualified = false;
    cue.physical_id = "cue-reference-change";
    cue.source_path = "Z:/Music/Downloads/track.cue";
    cue.source_key = "z:/music/downloads/track.cue";
    cue.source_guard = "source-observation-" + cue.source_key;
    audio_item.cue_links = djmeta::CueLinkState::Verified;
    const auto valid = djmeta::review_batch_plan({audio_item, cue});
    require(valid.ready_to_apply && valid.blocked == 0 &&
            valid.decisions[0].status == djmeta::PlanStatus::Ready &&
            valid.decisions[1].status == djmeta::PlanStatus::Ready,
            "qualified external CUE-only rewrite must remain a valid read-only plan");
}

void test_companion_files_need_explicit_or_qualified_policy() {
    auto a = audio("A");
    djmeta::FilePlanItem art;
    art.physical_id = "art-A";
    art.source_path = "Z:/Music/Downloads/cover.jpg";
    art.source_key = "z:/music/downloads/cover.jpg";
    art.source_guard = "source-observation-" + art.source_key;
    art.target_path = "Z:/Music/Singles/cover.jpg";
    art.target_key = "z:/music/singles/cover.jpg";
    art.action = djmeta::FileAction::Move;
    art.role = djmeta::FileRole::Companion;
    art.associated_audio_id = a.physical_id;
    art.ruleset_revision = a.ruleset_revision;
    art.target_presence = djmeta::TargetPresence::Missing;

    const auto unapproved = djmeta::review_batch_plan({a, art});
    require(has_reason(unapproved.decisions[1], "COMPANION_NOT_AUTHORIZED"),
            "legacy moveOtherFiles must not authorize unrelated sidecars");
    art.manual_override = true;
    const auto approved = djmeta::review_batch_plan({a, art});
    require(approved.ready_to_apply, "explicitly selected artwork may be planned");
    art.manual_override = false;
    art.companion_policy_qualified = true;
    const auto by_policy = djmeta::review_batch_plan({a, art});
    require(by_policy.ready_to_apply, "qualified companion policy should be usable");
}

void test_unknown_plan_enum_values_fail_closed() {
    const auto base = audio("unknown-enum");
    auto case_item = base;
    case_item.action = static_cast<djmeta::FileAction>(42);
    auto outcome = djmeta::review_batch_plan({case_item});
    require(outcome.blocked == 1 && !outcome.ready_to_apply &&
            has_reason(outcome.decisions[0], "UNKNOWN_PLAN_ENUM_VALUE"),
            "unknown action must not become an executable file plan");

    case_item = base;
    case_item.role = static_cast<djmeta::FileRole>(42);
    outcome = djmeta::review_batch_plan({case_item});
    require(outcome.blocked == 1 && !outcome.ready_to_apply &&
            has_reason(outcome.decisions[0], "UNKNOWN_PLAN_ENUM_VALUE"),
            "unknown file role must not bypass audio/CUE qualifications");

    case_item = base;
    case_item.target_presence = static_cast<djmeta::TargetPresence>(42);
    outcome = djmeta::review_batch_plan({case_item});
    require(outcome.blocked == 1 && !outcome.ready_to_apply &&
            has_reason(outcome.decisions[0], "UNKNOWN_PLAN_ENUM_VALUE"),
            "unknown target presence must not bypass filesystem inspection");

    case_item = base;
    case_item.cue_links = static_cast<djmeta::CueLinkState>(42);
    outcome = djmeta::review_batch_plan({case_item});
    require(outcome.blocked == 1 && !outcome.ready_to_apply &&
            has_reason(outcome.decisions[0], "UNKNOWN_PLAN_ENUM_VALUE"),
            "unknown cue state must never count as qualified references");

    case_item = base;
    case_item.action = djmeta::FileAction::None;
    case_item.cue_links = static_cast<djmeta::CueLinkState>(42);
    outcome = djmeta::review_batch_plan({case_item});
    require(outcome.blocked == 1 && !outcome.ready_to_apply &&
            has_reason(outcome.decisions[0], "UNKNOWN_PLAN_ENUM_VALUE"),
            "tag-only early exit must still validate unknown enum values");

    const djmeta::BatchApproval approval{outcome.plan_fingerprint, true};
    const auto approved = djmeta::review_batch_plan({case_item}, &approval);
    require(approved.blocked == 1 && !approved.ready_to_apply,
            "overwrite approval must never bypass unknown enum values");

    // Valid examples preserve the previous successful planning contract.
    outcome = djmeta::review_batch_plan({base});
    require(outcome.ready_to_apply && outcome.blocked == 0,
            "known valid enum values must retain ready preview status");
}

void test_metadata_only_requires_physical_identity() {
    auto item = audio("tag-only-identity");
    item.action = djmeta::FileAction::None;
    item.target_path.clear();
    item.target_key.clear();
    const auto valid = djmeta::review_batch_plan({item});
    require(valid.ready_to_apply &&
            valid.decisions[0].status == djmeta::PlanStatus::Ready,
            "qualified metadata-only source remains an unchanged file plan");

    for (int missing = 0; missing < 7; ++missing) {
        auto invalid = item;
        switch (missing) {
            case 0: invalid.physical_id.clear(); break;
            case 1: invalid.source_path.clear(); break;
            case 2: invalid.source_key.clear(); break;
            case 3: invalid.ruleset_revision.clear(); break;
            case 4: invalid.source_guard.clear(); break;
            case 5: invalid.metadata_fingerprint.clear(); break;
            default: invalid.planned_metadata_fingerprint.clear(); break;
        }
        const auto blocked = djmeta::review_batch_plan({invalid});
        require(blocked.blocked == 1 && !blocked.ready_to_apply &&
                blocked.decisions[0].status == djmeta::PlanStatus::Blocked &&
                has_reason(blocked.decisions[0], missing < 4
                    ? "MISSING_SOURCE_OR_RULESET_IDENTITY"
                    : missing == 4 ? "SOURCE_NOT_INSPECTED"
                    : "MISSING_METADATA_FINGERPRINT"),
                "metadata-only source missing physical identity must fail closed");
        const djmeta::BatchApproval approve{blocked.plan_fingerprint, true};
        require(djmeta::review_batch_plan({invalid}, &approve).blocked == 1,
                "overwrite approval cannot resurrect unqualified tag-only source");
    }
}

void test_same_canonical_key_but_different_raw_path_is_not_noop() {
    auto item = audio("CaseTest");
    item.action = djmeta::FileAction::Rename;
    item.target_key = item.source_key;
    item.target_path = "Z:/Music/Downloads/casetest.mp3";
    auto result = djmeta::review_batch_plan({item});
    require(result.blocked == 1 && !result.ready_to_apply &&
            has_reason(result.decisions[0], "SAME_SOURCE_TARGET_KEY_DIFFERENT_PATH"),
            "case-only rename needs a qualified plan, not silent no-op");
    const djmeta::BatchApproval approval{result.plan_fingerprint, true};
    result = djmeta::review_batch_plan({item}, &approval);
    require(result.blocked == 1 && !result.ready_to_apply,
            "batch-wide approval must not bypass same-key alias safeguard");

    item.action = djmeta::FileAction::Move;
    item.target_path = "Z:\\\\Music\\\\Downloads\\\\CaseTest.mp3";
    result = djmeta::review_batch_plan({item});
    require(result.blocked == 1 &&
            has_reason(result.decisions[0], "SAME_SOURCE_TARGET_KEY_DIFFERENT_PATH"),
            "unqualified path-alias representation is not an identical raw path");

    item.target_path = item.source_path;
    result = djmeta::review_batch_plan({item});
    require(result.ready_to_apply &&
            result.decisions[0].status == djmeta::PlanStatus::Ready,
            "exact same source and target path remains a no-op");
}

void test_embedded_nul_in_plan_identity_fails_closed() {
    const auto sample = audio("nul-input");
    const auto inject_nul = [](std::string& field) {
        field.insert(field.size() / 2, 1, '\0');
    };
    for (int field_index = 0; field_index < 15; ++field_index) {
        auto item = sample;
        std::string* fields[]{
            &item.physical_id, &item.source_path, &item.source_key,
            &item.source_guard, &item.target_path, &item.target_key, &item.associated_audio_id,
            &item.metadata_fingerprint, &item.planned_metadata_fingerprint,
            &item.ruleset_revision, &item.routing_profile,
            &item.naming_expression, &item.target_guard,
            &item.cue_source_fingerprint, &item.cue_postimage_fingerprint
        };
        inject_nul(*fields[field_index]);
        const auto review = djmeta::review_batch_plan({item});
        require(review.blocked == 1 && !review.ready_to_apply &&
                has_reason(review.decisions[0], "EMBEDDED_NUL_IN_PLAN_IDENTITY"),
                "zero-terminated native identity must not accept embedded NUL");
        const djmeta::BatchApproval approval{review.plan_fingerprint, true};
        const auto approved = djmeta::review_batch_plan({item}, &approval);
        require(approved.blocked == 1 && !approved.ready_to_apply,
                "blanket overwrite approval cannot override embedded NUL");
    }

    auto tag_only = sample;
    tag_only.action = djmeta::FileAction::None;
    tag_only.target_key.clear();
    tag_only.target_path.clear();
    inject_nul(tag_only.source_path);
    const auto tag_review = djmeta::review_batch_plan({tag_only});
    require(tag_review.blocked == 1 && !tag_review.ready_to_apply &&
            has_reason(tag_review.decisions[0], "EMBEDDED_NUL_IN_PLAN_IDENTITY"),
            "tag-only early-exit must not bypass embedded NUL guard");

    auto noop = sample;
    noop.target_path = noop.source_path;
    noop.target_key = noop.source_key;
    inject_nul(noop.source_key);
    const auto no_op_review = djmeta::review_batch_plan({noop});
    require(no_op_review.blocked == 1 && !no_op_review.ready_to_apply &&
            has_reason(no_op_review.decisions[0], "EMBEDDED_NUL_IN_PLAN_IDENTITY"),
            "identical source and destination must not hide malformed identity");

    const auto normal = djmeta::review_batch_plan({sample});
    require(normal.ready_to_apply, "valid non-NUL source remains plannable");
}

void test_frozen_approved_snapshot_is_detached_and_non_executing() {
    std::vector<djmeta::FilePlanItem> mutable_batch{audio("Freeze-A"), audio("Freeze-B")};
    const auto original = djmeta::review_batch_plan(mutable_batch);
    require(original.ready_to_apply, "valid snapshot candidate must pass preflight");
    const djmeta::BatchApproval approval{original.plan_fingerprint, false};
    const auto frozen = djmeta::freeze_reviewed_batch_preview(mutable_batch, approval);
    require(frozen.items.size() == 2 &&
            frozen.decisions.size() == 2 &&
            frozen.plan_fingerprint == original.plan_fingerprint,
            "frozen preview retains exact reviewed identity and decision count");
    const auto first_source = frozen.items[0].source_path;
    mutable_batch[0].source_path = "Z:/Music/Other/edited-after-freeze.mp3";
    require(frozen.items[0].source_path == first_source &&
            frozen.plan_fingerprint == original.plan_fingerprint,
            "frozen plan must own its inputs independent of UI mutation");
    try {
        (void)djmeta::freeze_reviewed_batch_preview(mutable_batch, approval);
        require(false, "stale mutable UI plan must not reuse old approval");
    } catch (const std::invalid_argument&) {}

    const djmeta::BatchApproval unconfirmed{"", true};
    try {
        (void)djmeta::freeze_reviewed_batch_preview({audio("Freeze-C")}, unconfirmed);
        require(false, "explicit reviewed fingerprint required even without overwrite");
    } catch (const std::invalid_argument&) {}

    auto existing = audio("Overwrite");
    existing.target_presence = djmeta::TargetPresence::Existing;
    existing.target_guard = "observed-target-sha256";
    existing.target_physical_id = "observed-existing-physical-id";
    const auto overwrite_review = djmeta::review_batch_plan({existing});
    const djmeta::BatchApproval not_approved{overwrite_review.plan_fingerprint, false};
    try {
        (void)djmeta::freeze_reviewed_batch_preview({existing}, not_approved);
        require(false, "existing destination without specific approval was frozen");
    } catch (const std::invalid_argument&) {}
    const djmeta::BatchApproval individually_approved{
        overwrite_review.plan_fingerprint, false, {"Overwrite"}};
    const auto allowed = djmeta::freeze_reviewed_batch_preview(
        {existing}, individually_approved);
    require(allowed.decisions[0].will_replace_existing_target &&
            allowed.plan_fingerprint == overwrite_review.plan_fingerprint,
            "exactly inspected destination consent must survive snapshot");

    auto unresolved = audio("NoCueEvidence");
    unresolved.cue_links = djmeta::CueLinkState::Unchecked;
    const auto invalid = djmeta::review_batch_plan({unresolved});
    try {
        (void)djmeta::freeze_reviewed_batch_preview(
            {unresolved}, djmeta::BatchApproval{invalid.plan_fingerprint, true});
        require(false, "CUE-unqualified plan cannot freeze under blanket approval");
    } catch (const std::invalid_argument&) {}

    auto nul = audio("Nul");
    nul.source_path.insert(4, 1, '\0');
    const auto malformed = djmeta::review_batch_plan({nul});
    try {
        (void)djmeta::freeze_reviewed_batch_preview(
            {nul}, djmeta::BatchApproval{malformed.plan_fingerprint, true});
        require(false, "malformed source path must fail frozen batch qualification");
    } catch (const std::invalid_argument&) {}

    try {
        (void)djmeta::freeze_reviewed_batch_preview({}, djmeta::BatchApproval{
            djmeta::review_batch_plan({}).plan_fingerprint, true});
        require(false, "empty batch must never produce an executable intent");
    } catch (const std::invalid_argument&) {}
}


void test_source_observation_approval_binding() {
    auto a = audio("observed-A");
    auto b = audio("observed-B");
    const auto batch = std::vector<djmeta::FilePlanItem>{a, b};
    const auto preview = djmeta::review_batch_plan(batch);
    require(preview.ready_to_apply, "inspected sources must have a ready preview");

    const djmeta::BatchApproval approval{preview.plan_fingerprint, true};
    b.source_guard = "changed-source-observation";
    const auto stale = djmeta::review_batch_plan({a, b}, &approval);
    require(stale.blocked == 2 && !stale.ready_to_apply &&
            has_reason(stale.decisions[0], "STALE_BATCH_APPROVAL") &&
            has_reason(stale.decisions[1], "STALE_BATCH_APPROVAL"),
            "changed source identity must invalidate the entire approval");

    auto missing = audio("unprobed");
    missing.source_guard.clear();
    auto result = djmeta::review_batch_plan({missing});
    require(result.blocked == 1 && !result.ready_to_apply &&
            has_reason(result.decisions[0], "SOURCE_NOT_INSPECTED"),
            "missing host source probe must block file changes");
    missing.action = djmeta::FileAction::None;
    missing.target_path.clear();
    missing.target_key.clear();
    result = djmeta::review_batch_plan({missing});
    require(result.blocked == 1 &&
            has_reason(result.decisions[0], "SOURCE_NOT_INSPECTED"),
            "tag-only rows must not bypass host source probe");

    const auto valid = djmeta::review_batch_plan({audio("qualified")});
    require(valid.ready_to_apply, "qualified unrelated source remains plannable");
}

void test_mixed_ruleset_revisions_fail_closed() {
    auto a = audio("revision-A");
    auto b = audio("revision-B");
    b.action = djmeta::FileAction::None;
    b.target_path.clear();
    b.target_key.clear();
    b.ruleset_revision = "other-revision";
    const auto review = djmeta::review_batch_plan({a, b});
    require(review.blocked == 2 && !review.ready_to_apply &&
            has_reason(review.decisions[0], "MIXED_RULESET_REVISIONS") &&
            has_reason(review.decisions[1], "MIXED_RULESET_REVISIONS"),
            "mixed rule revisions must block the whole batch, including tag-only rows");
    const djmeta::BatchApproval approval{review.plan_fingerprint, true};
    const auto overridden = djmeta::review_batch_plan({a, b}, &approval);
    require(overridden.blocked == 2 && !overridden.ready_to_apply,
            "bulk overwrite approval cannot authorize mixed revisions");
    try {
        (void)djmeta::freeze_reviewed_batch_preview({a, b}, approval);
        require(false, "mixed ruleset plan must never be frozen");
    } catch (const std::invalid_argument&) {}

    b.ruleset_revision = a.ruleset_revision;
    const auto coherent = djmeta::review_batch_plan({a, b});
    require(coherent.ready_to_apply && coherent.blocked == 0,
            "matching revisions preserve mixed file/tag-only plans");
}

void test_exact_noop_write_intents() {
    // A selected source with identical physical metadata, no CUE rewrite,
    // and no actual destination change must never trigger any writer.
    auto none = audio("Noop");
    none.action = djmeta::FileAction::None;
    none.target_path.clear();
    none.target_key.clear();
    none.target_presence = djmeta::TargetPresence::Unchecked;
    none.planned_metadata_fingerprint = none.metadata_fingerprint;
    auto reviewed = djmeta::review_batch_plan({none});
    require(reviewed.ready_to_apply &&
            reviewed.decisions[0].status == djmeta::PlanStatus::Unchanged &&
            !reviewed.decisions[0].metadata_write_needed &&
            !reviewed.decisions[0].cue_bytes_write_needed &&
            !reviewed.decisions[0].file_operation_needed,
            "selected but unmodified metadata must be a full no-op");

    // A real tag-only change needs a tag write, never a file operation.
    auto tag = none;
    tag.planned_metadata_fingerprint = "approved-different-tags";
    reviewed = djmeta::review_batch_plan({tag});
    require(reviewed.ready_to_apply &&
            reviewed.decisions[0].status == djmeta::PlanStatus::Ready &&
            reviewed.decisions[0].metadata_write_needed &&
            !reviewed.decisions[0].file_operation_needed &&
            !reviewed.decisions[0].cue_bytes_write_needed,
            "tag-only changes must not be misreported as unchanged");

    // Metadata still matters if an otherwise redundant move targets self.
    tag.action = djmeta::FileAction::Move;
    tag.target_key = tag.source_key;
    tag.target_path = tag.source_path;
    tag.target_presence = djmeta::TargetPresence::Unchecked;
    reviewed = djmeta::review_batch_plan({tag});
    require(reviewed.ready_to_apply &&
            reviewed.decisions[0].status == djmeta::PlanStatus::Ready &&
            reviewed.decisions[0].metadata_write_needed &&
            !reviewed.decisions[0].file_operation_needed,
            "self-move must not turn a real tag change into a no-op");
    tag.planned_metadata_fingerprint = tag.metadata_fingerprint;
    reviewed = djmeta::review_batch_plan({tag});
    require(reviewed.ready_to_apply &&
            reviewed.decisions[0].status == djmeta::PlanStatus::Unchanged &&
            !reviewed.decisions[0].metadata_write_needed &&
            !reviewed.decisions[0].file_operation_needed,
            "self-move with identical tags must perform zero writes");

    // A move with identical tags needs only a file operation.
    tag.target_path = "Z:/Music/Singles/Noop.mp3";
    tag.target_key = "z:/music/singles/Noop.mp3";
    tag.target_presence = djmeta::TargetPresence::Missing;
    reviewed = djmeta::review_batch_plan({tag});
    require(reviewed.ready_to_apply &&
            reviewed.decisions[0].status == djmeta::PlanStatus::Ready &&
            !reviewed.decisions[0].metadata_write_needed &&
            reviewed.decisions[0].file_operation_needed,
            "file move must not force unchanged metadata to be rewritten");

    // Both independently requested actions should be declared when needed.
    tag.planned_metadata_fingerprint = "new-tags-after-move";
    reviewed = djmeta::review_batch_plan({tag});
    require(reviewed.ready_to_apply &&
            reviewed.decisions[0].metadata_write_needed &&
            reviewed.decisions[0].file_operation_needed,
            "real tag and file location edits must be represented independently");

    tag.target_presence = djmeta::TargetPresence::Unchecked;
    reviewed = djmeta::review_batch_plan({tag});
    require(reviewed.blocked == 1 &&
            !reviewed.decisions[0].file_operation_needed &&
            !reviewed.decisions[0].metadata_write_needed,
            "blocked batch must never expose any write intent");

    // A CUE postimage with identical physical bytes cannot justify an edit.
    auto audio_row = audio("CUE-Noop");
    audio_row.action = djmeta::FileAction::None;
    audio_row.target_path.clear();
    audio_row.target_key.clear();
    audio_row.planned_metadata_fingerprint = audio_row.metadata_fingerprint;
    audio_row.cue_links = djmeta::CueLinkState::Verified;
    djmeta::FilePlanItem cue;
    cue.role = djmeta::FileRole::ExternalCue;
    cue.physical_id = "cue-noop";
    cue.source_path = "Z:/Music/Downloads/CUE-Noop.cue";
    cue.source_key = "z:/music/downloads/CUE-Noop.cue";
    cue.source_guard = "host-observed-cue-version";
    cue.ruleset_revision = audio_row.ruleset_revision;
    cue.associated_audio_id = audio_row.physical_id;
    cue.cue_links = djmeta::CueLinkState::Verified;
    cue.cue_references_will_change = true;
    cue.cue_source_fingerprint = "cue-byte-unchanged";
    cue.cue_postimage_fingerprint = cue.cue_source_fingerprint;
    reviewed = djmeta::review_batch_plan({audio_row, cue});
    require(reviewed.ready_to_apply &&
            reviewed.decisions[1].status == djmeta::PlanStatus::Unchanged &&
            !reviewed.decisions[1].cue_bytes_write_needed,
            "byte-identical external cue cannot be rewritten");

    cue.cue_postimage_fingerprint = "cue-byte-changed";
    reviewed = djmeta::review_batch_plan({audio_row, cue});
    require(reviewed.ready_to_apply &&
            reviewed.decisions[1].cue_bytes_write_needed &&
            !reviewed.decisions[1].file_operation_needed,
            "real reference postimage change can require CUE-only writer");

    cue.cue_references_will_change = false;
    reviewed = djmeta::review_batch_plan({audio_row, cue});
    require(reviewed.blocked == 1 &&
            has_reason(reviewed.decisions[1], "UNDECLARED_CUE_BYTE_CHANGE") &&
            !reviewed.decisions[1].cue_bytes_write_needed,
            "undeclared CUE postimage mutation must fail closed");
}

void test_absent_target_cannot_retain_existing_guard() {
    for (const auto presence : {
             djmeta::TargetPresence::Missing,
             djmeta::TargetPresence::Unchecked}) {
        for (const bool metadata_only : {false, true}) {
            auto candidate = audio("stale-absence");
            candidate.target_presence = presence;
            candidate.target_guard = "old-existing-target-guard";
            if (metadata_only) {
                candidate.action = djmeta::FileAction::None;
                candidate.target_path.clear();
                candidate.target_key.clear();
            }
            const std::vector<djmeta::FilePlanItem> batch{candidate};
            const auto reviewed = djmeta::review_batch_plan(batch);
            require(!reviewed.ready_to_apply && reviewed.blocked == 1 &&
                    reviewed.decisions[0].status == djmeta::PlanStatus::Blocked &&
                    has_reason(reviewed.decisions[0],
                               "TARGET_GUARD_WITHOUT_EXISTING_FILE") &&
                    !reviewed.decisions[0].metadata_write_needed &&
                    !reviewed.decisions[0].file_operation_needed,
                    "absent or unchecked target must reject stale existing-target guard");
            const djmeta::BatchApproval approve{reviewed.plan_fingerprint, true};
            require(djmeta::review_batch_plan(batch, &approve).blocked == 1,
                    "blanket overwrite approval cannot legalize contradictory target evidence");
            try {
                (void)djmeta::freeze_reviewed_batch_preview(batch, approve);
                require(false, "contradictory target observation must not freeze");
            } catch (const std::invalid_argument&) {}
        }
    }
    require(djmeta::review_batch_plan({audio("clean-absent-target")}).ready_to_apply,
            "ordinary missing target with no residual guard stays qualified");
}

void test_noop_and_missing_ids() {
    auto a = audio("A");
    a.target_key = a.source_key;
    a.target_path = a.source_path;
    a.target_presence = djmeta::TargetPresence::Unchecked;
    auto result = djmeta::review_batch_plan({a});
    require(result.ready_to_apply &&
            result.decisions[0].status == djmeta::PlanStatus::Ready,
            "rename/move-to-self requires no file action");

    a = audio("A");
    a.physical_id.clear();
    result = djmeta::review_batch_plan({a});
    require(has_reason(result.decisions[0], "MISSING_SOURCE_OR_RULESET_IDENTITY"),
            "missing physical identity must fail closed");

    result = djmeta::review_batch_plan({});
    require(!result.ready_to_apply, "empty plan must not be actionable");
}

} // namespace

int main() {
    test_ready_and_immutable();
    test_source_observation_approval_binding();
    test_mixed_ruleset_revisions_fail_closed();
    test_batch_overwrite_once_and_target_guard();
    test_existing_target_physical_identity_and_hardlink_alias_gates();
    test_large_batch_one_confirmation();
    test_individual_overwrite_approval_selection_is_exact();
    test_many_individual_overwrite_approvals_are_indexed();
    test_missing_target_guard_and_no_inspection();
    test_absent_target_cannot_retain_existing_guard();
    test_new_target_after_clean_review_requires_new_consent();
    test_intra_batch_conflicts_cannot_be_overridden();
    test_stale_approval_rejects_tag_only_and_noop_rows();
    test_duplicate_subsong_source_and_self_copy();
    test_duplicate_selected_tag_only_source_cannot_escape_preflight();
    test_external_cue_dependencies();
    test_multiple_external_cue_carriers_per_audio_block_batch();
    test_multifile_cue_planned_audio_dependencies();
    test_contradictory_audio_cue_evidence_rejected();
    test_inactive_cue_relationships_and_tag_only_audio_guard();
    test_cue_rewrite_only_on_external_cue_items();
    test_metadata_only_requires_physical_identity();
    test_same_canonical_key_but_different_raw_path_is_not_noop();
    test_embedded_nul_in_plan_identity_fails_closed();
    test_frozen_approved_snapshot_is_detached_and_non_executing();
    test_noop_and_missing_ids();
    test_exact_noop_write_intents();
    test_unknown_plan_enum_values_fail_closed();
    test_companion_files_need_explicit_or_qualified_policy();
    std::cout << "PASS: deterministic read-only batch plan preflight tests\n";
    return 0;
}
