#include "djmeta/batch_plan.h"
#include "djmeta/normalizer.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace djmeta {
namespace {

std::string decimal(int value) { return std::to_string(value); }
std::string yesno(bool value) { return value ? "1" : "0"; }

std::string plan_fingerprint(const std::vector<FilePlanItem>& items) {
    MetadataDocument document;
    document.fields.push_back({"PLAN_CONTRACT", {"djmeta-batch-v2"}});
    for (const FilePlanItem& item : items) {
        // The existing SHA-256 metadata fingerprint uses explicit lengths for
        // every field/value and includes field ordering. Never concatenate
        // unescaped strings to construct approval identity.
        document.fields.push_back({"ITEM", {
            item.physical_id,
            item.source_path, item.source_key, item.source_guard,
            item.target_path, item.target_key,
            decimal(static_cast<int>(item.action)),
            decimal(static_cast<int>(item.role)),
            item.associated_audio_id,
            item.metadata_fingerprint, item.planned_metadata_fingerprint,
            item.ruleset_revision,
            item.routing_profile, item.naming_expression,
            yesno(item.manual_override),
            yesno(item.companion_policy_qualified),
            decimal(static_cast<int>(item.target_presence)),
            item.target_guard, item.target_physical_id,
            decimal(static_cast<int>(item.cue_links)),
            item.cue_source_fingerprint, item.cue_postimage_fingerprint,
            yesno(item.cue_references_will_change),
            std::to_string(item.associated_audio_ids.size())
        }});
        for (const auto& id : item.associated_audio_ids)
            document.fields.back().values.push_back(id);
    }
    return fingerprint(document);
}

void block(ItemDecision& decision, const char* reason) {
    decision.reasons.emplace_back(reason);
    decision.status = PlanStatus::Blocked;
}

// Unrecognized serialized/host-provided enum values must never inherit
// execution semantics from a default branch. The planner fails closed for
// every selected row, including a tag-only/no-file-action item.
bool valid_action(FileAction value) {
    switch (value) {
    case FileAction::None:
    case FileAction::Rename:
    case FileAction::Move:
    case FileAction::Copy: return true;
    default: return false;
    }
}
bool valid_role(FileRole value) {
    switch (value) {
    case FileRole::Audio:
    case FileRole::ExternalCue:
    case FileRole::Companion: return true;
    default: return false;
    }
}
bool valid_presence(TargetPresence value) {
    switch (value) {
    case TargetPresence::Unchecked:
    case TargetPresence::Missing:
    case TargetPresence::Existing: return true;
    default: return false;
    }
}
bool valid_cue_state(CueLinkState value) {
    switch (value) {
    case CueLinkState::Unchecked:
    case CueLinkState::NoExternalCue:
    case CueLinkState::Verified:
    case CueLinkState::Unresolved: return true;
    default: return false;
    }
}

// Native host file APIs and legacy metadata libraries may use zero-
// terminated strings. Never let an embedded zero truncate an approved
// source/target path, key, rule identity or projected content guard.
bool contains_embedded_nul(const FilePlanItem& item) {
    const std::string* fields[] = {
        &item.physical_id, &item.source_path, &item.source_key,
        &item.source_guard, &item.target_path, &item.target_key, &item.associated_audio_id,
        &item.metadata_fingerprint, &item.planned_metadata_fingerprint,
        &item.ruleset_revision, &item.routing_profile,
        &item.naming_expression, &item.target_guard,
        &item.target_physical_id,
        &item.cue_source_fingerprint, &item.cue_postimage_fingerprint
    };
    for (const auto* field : fields)
        if (field->find('\0') != std::string::npos) return true;
    for (const auto& id : item.associated_audio_ids)
        if (id.find('\0') != std::string::npos) return true;
    return false;
}

std::vector<std::string> cue_audio_dependencies(const FilePlanItem& item) {
    if (!item.associated_audio_ids.empty()) return item.associated_audio_ids;
    return item.associated_audio_id.empty()
        ? std::vector<std::string>{}
        : std::vector<std::string>{item.associated_audio_id};
}

bool valid_cue_participants(const FilePlanItem& item) {
    if (!item.associated_audio_ids.empty() &&
        item.role != FileRole::ExternalCue) return false;
    if (item.role != FileRole::ExternalCue) return true;
    const auto ids = cue_audio_dependencies(item);
    if (ids.empty()) return false;
    std::set<std::string> unique;
    for (const auto& id : ids)
        if (id.empty() || !unique.insert(id).second) return false;
    if (!item.associated_audio_id.empty() &&
        unique.count(item.associated_audio_id) == 0) return false;
    return true;
}

bool active_file_action(const FilePlanItem& item) {
    return item.action != FileAction::None;
}

bool active_item(const FilePlanItem& item) {
    return active_file_action(item) || item.cue_references_will_change;
}

} // namespace

BatchPlanReview review_batch_plan(
    const std::vector<FilePlanItem>& items,
    const BatchApproval* approval) {

    BatchPlanReview review;
    review.plan_fingerprint = plan_fingerprint(items);
    review.decisions.resize(items.size());
    const bool stale_approval =
        approval != nullptr &&
        approval->reviewed_plan_fingerprint != review.plan_fingerprint;
    const bool blanket_overwrite =
        approval != nullptr && !stale_approval &&
        approval->approve_all_observed_overwrites;
    // Build once: linear searches for each existing target used to make
    // individually approved large batches O(N * approval_count).
    std::set<std::string> individually_approved;
    if (approval && !stale_approval)
        individually_approved.insert(
            approval->individually_approved_physical_ids.begin(),
            approval->individually_approved_physical_ids.end());

    // The preview must correspond to ONE immutable ruleset across audio,
    // CUE and companion rows, including rows with no physical file action.
    // Different revisions in a single approved batch cannot be reconciled
    // by blanket overwrite consent or by ignoring unchanged rows.
    std::string batch_revision;
    bool mixed_ruleset_revisions = false;
    for (const FilePlanItem& item : items) {
        if (item.ruleset_revision.empty()) continue;
        if (batch_revision.empty()) batch_revision = item.ruleset_revision;
        else if (item.ruleset_revision != batch_revision)
            mixed_ruleset_revisions = true;
    }

    std::map<std::string, std::size_t> source_key_counts;
    std::map<std::string, std::size_t> physical_id_counts;
    std::map<std::string, std::size_t> target_key_counts;
    std::map<std::string, std::size_t> existing_target_physical_counts;
    std::set<std::string> all_source_physical_ids;
    std::set<std::string> audio_ids;
    std::map<std::string, CueLinkState> audio_cue_states;
    std::set<std::string> all_source_keys;
    // Distinguish declared CUE relationships from CUE operations actually
    // included in the batch: an inactive CUE still contradicts a claimed
    // "NoExternalCue", but cannot satisfy an active audio move dependency.
    std::map<std::string, std::size_t> cue_counts;
    std::map<std::string, std::size_t> all_cue_counts;

    for (const FilePlanItem& item : items) {
        if (!item.source_key.empty()) all_source_keys.insert(item.source_key);
        if (!item.physical_id.empty())
            all_source_physical_ids.insert(item.physical_id);
        if (item.role == FileRole::ExternalCue)
            for (const auto& id : cue_audio_dependencies(item))
                ++all_cue_counts[id];
        if (item.role == FileRole::Audio && !item.physical_id.empty()) {
            audio_ids.insert(item.physical_id);
            // Preserve the host-qualified relationship state of every audio
            // item, even when selected only for metadata updates.
            audio_cue_states[item.physical_id] = item.cue_links;
        }
        // Every selected row owns a physical source identity, including
        // tag-only rows. Otherwise an active file operation could coexist
        // with an undisclosed second selection of the same audio/subsong.
        if (!item.source_key.empty()) ++source_key_counts[item.source_key];
        if (!item.physical_id.empty()) ++physical_id_counts[item.physical_id];
        if (!active_item(item)) continue;
        if (active_file_action(item) && !item.target_key.empty())
            ++target_key_counts[item.target_key];
        if (active_file_action(item) &&
            item.target_presence == TargetPresence::Existing &&
            !item.target_physical_id.empty())
            ++existing_target_physical_counts[item.target_physical_id];
        if (item.role == FileRole::ExternalCue)
            for (const auto& id : cue_audio_dependencies(item))
                ++cue_counts[id];
    }

    for (std::size_t index = 0; index < items.size(); ++index) {
        const FilePlanItem& item = items[index];
        ItemDecision& decision = review.decisions[index];
        decision.status = PlanStatus::Ready;

        if (!valid_action(item.action) || !valid_role(item.role) ||
            !valid_presence(item.target_presence) || !valid_cue_state(item.cue_links)) {
            block(decision, "UNKNOWN_PLAN_ENUM_VALUE");
            if (stale_approval) block(decision, "STALE_BATCH_APPROVAL");
            ++review.blocked;
            continue;
        }

        if (contains_embedded_nul(item))
            block(decision, "EMBEDDED_NUL_IN_PLAN_IDENTITY");
        if (!valid_cue_participants(item))
            block(decision, "CUE_AUDIO_RELATIONSHIP_AMBIGUOUS");
        // A selected audio source must never be attached to multiple
        // independently managed external CUE carriers. Count even inactive
        // CUE rows: a second CUE cannot be ignored just because its own
        // file path does not change in this preview.
        if (item.role == FileRole::Audio && !item.physical_id.empty() &&
            all_cue_counts[item.physical_id] > 1)
            block(decision, "MULTIPLE_EXTERNAL_CUES_FOR_AUDIO");
        if (item.role == FileRole::ExternalCue)
            for (const auto& id : cue_audio_dependencies(item))
                if (all_cue_counts[id] > 1) {
                    block(decision, "MULTIPLE_EXTERNAL_CUES_FOR_AUDIO");
                    break;
                }
        if (mixed_ruleset_revisions)
            block(decision, "MIXED_RULESET_REVISIONS");
        if (item.source_guard.empty())
            block(decision, "SOURCE_NOT_INSPECTED");
        if (item.target_presence != TargetPresence::Existing &&
            !item.target_physical_id.empty())
            block(decision, "TARGET_ID_WITHOUT_EXISTING_FILE");

        if (item.cue_references_will_change && item.role != FileRole::ExternalCue)
            block(decision, "CUE_REWRITE_ON_NON_CUE_ITEM");
        // Enforce uniqueness before the tag-only/no-action early exit.
        if (!item.source_key.empty() && source_key_counts[item.source_key] > 1)
            block(decision, "DUPLICATE_PHYSICAL_SOURCE");
        if (!item.physical_id.empty() && physical_id_counts[item.physical_id] > 1)
            block(decision, "DUPLICATE_PHYSICAL_ID");

        // Even a metadata-only selection is an intended operation on a
        // physical source. It must carry enough identity to qualify an
        // eventual tag writer; "no file action" cannot skip this preflight.
        if (item.physical_id.empty() || item.source_path.empty() ||
            item.source_key.empty() || item.ruleset_revision.empty())
            block(decision, "MISSING_SOURCE_OR_RULESET_IDENTITY");
        if (item.role == FileRole::Audio &&
            (item.metadata_fingerprint.empty() ||
             item.planned_metadata_fingerprint.empty()))
            block(decision, "MISSING_METADATA_FINGERPRINT");
        // A tag writer must not run merely because a row was selected or a
        // filename changed. Identical captured/postimage fingerprints mean
        // there are no approved physical metadata changes to write.
        decision.metadata_write_needed = item.role == FileRole::Audio &&
            !item.metadata_fingerprint.empty() &&
            !item.planned_metadata_fingerprint.empty() &&
            item.metadata_fingerprint != item.planned_metadata_fingerprint;
        decision.cue_bytes_write_needed = item.role == FileRole::ExternalCue &&
            item.cue_references_will_change &&
            !item.cue_source_fingerprint.empty() &&
            !item.cue_postimage_fingerprint.empty() &&
            item.cue_source_fingerprint != item.cue_postimage_fingerprint;
        if (item.role == FileRole::ExternalCue &&
            !item.cue_references_will_change &&
            !item.cue_source_fingerprint.empty() &&
            !item.cue_postimage_fingerprint.empty() &&
            item.cue_source_fingerprint != item.cue_postimage_fingerprint)
            block(decision, "UNDECLARED_CUE_BYTE_CHANGE");
        // Physical metadata changes must not skip external-CUE dependency
        // inspection merely because no file rename/move was requested.
        if (item.role == FileRole::Audio) {
            if (item.cue_links == CueLinkState::Unchecked ||
                item.cue_links == CueLinkState::Unresolved)
                block(decision, "CUE_DEPENDENCY_UNQUALIFIED");
            if (item.cue_links == CueLinkState::NoExternalCue &&
                all_cue_counts[item.physical_id] != 0)
                block(decision, "CUE_ASSOCIATION_CONTRADICTS_NO_EXTERNAL_CUE");
        }

        if (!active_item(item)) {
            if (decision.status == PlanStatus::Blocked) {
                if (stale_approval) block(decision, "STALE_BATCH_APPROVAL");
                decision.metadata_write_needed = false;
                decision.cue_bytes_write_needed = false;
                decision.file_operation_needed = false;
                ++review.blocked;
                continue;
            }
            // A stale plan approval must invalidate the *entire* batch,
            // including tag-only and otherwise unchanged file rows. Do not
            // silently skip approval identity checking via this early exit.
            if (stale_approval) {
                block(decision, "STALE_BATCH_APPROVAL");
                decision.metadata_write_needed = false;
                decision.cue_bytes_write_needed = false;
                decision.file_operation_needed = false;
                ++review.blocked;
            } else {
                decision.status = decision.metadata_write_needed
                    ? PlanStatus::Ready : PlanStatus::Unchanged;
            }
            if (decision.status == PlanStatus::Blocked) {
                decision.metadata_write_needed = false;
                decision.cue_bytes_write_needed = false;
            }
            continue;
        }

        if (item.role == FileRole::Audio) {
            if (item.cue_links == CueLinkState::Verified &&
                cue_counts[item.physical_id] == 0)
                block(decision, "VERIFIED_CUE_NOT_IN_PLAN");
        } else if (item.role == FileRole::Companion) {
            if (item.associated_audio_id.empty() ||
                audio_ids.count(item.associated_audio_id) == 0 ||
                (!item.companion_policy_qualified && !item.manual_override))
                block(decision, "COMPANION_NOT_AUTHORIZED");
        } else if (item.role == FileRole::ExternalCue) {
            const auto related = cue_audio_dependencies(item);
            bool all_audio_verified = !related.empty();
            for (const auto& id : related)
                if (audio_ids.count(id) == 0 ||
                    audio_cue_states[id] != CueLinkState::Verified)
                    all_audio_verified = false;
            if (!valid_cue_participants(item) || !all_audio_verified ||
                item.cue_links != CueLinkState::Verified ||
                item.cue_source_fingerprint.empty() ||
                item.cue_postimage_fingerprint.empty()) {
                block(decision, "CUE_REFERENCE_PLAN_UNQUALIFIED");
            }
        }

        if (active_file_action(item)) {
            decision.file_operation_needed =
                !(item.action != FileAction::Copy &&
                  item.source_key == item.target_key &&
                  item.source_path == item.target_path);
            if (item.target_path.empty() || item.target_key.empty())
                block(decision, "MISSING_TARGET_PATH_OR_IDENTITY");
            if (!item.target_key.empty() && target_key_counts[item.target_key] > 1)
                block(decision, "MULTIPLE_SOURCES_SAME_TARGET");
            if (!item.target_key.empty() && item.target_key != item.source_key &&
                all_source_keys.count(item.target_key) != 0) {
                block(decision, "TARGET_IS_BATCH_SOURCE");
            }
            if (!item.target_key.empty() && item.target_key == item.source_key) {
                if (item.action == FileAction::Copy)
                    block(decision, "COPY_SOURCE_EQUALS_TARGET");
                else if (item.target_path != item.source_path) {
                    // Same canonical filesystem identity but different raw
                    // path: could be case-only rename, reparse-point alias or
                    // separator normalization. An actual rename is required
                    // for case-only changes; do not report it Unchanged.
                    // A future executor needs a separately qualified strategy.
                    block(decision, "SAME_SOURCE_TARGET_KEY_DIFFERENT_PATH");
                } else if (!item.cue_references_will_change) {
                    // No actual file operation; tag-only changes remain
                    // executable while byte-identical plans are true no-ops.
                    decision.file_operation_needed = false;
                    decision.status = decision.reasons.empty()
                        ? (decision.metadata_write_needed
                            ? PlanStatus::Ready : PlanStatus::Unchanged)
                        : PlanStatus::Blocked;
                    if (stale_approval) block(decision, "STALE_BATCH_APPROVAL");
                    if (decision.status == PlanStatus::Blocked) {
                        decision.metadata_write_needed = false;
                        decision.cue_bytes_write_needed = false;
                        ++review.blocked;
                    }
                    continue;
                }
            }

            if (item.target_presence == TargetPresence::Unchecked)
                block(decision, "TARGET_NOT_INSPECTED");
            else if (item.target_presence == TargetPresence::Existing) {
                if (item.target_physical_id.empty())
                    block(decision, "EXISTING_TARGET_PHYSICAL_ID_NOT_INSPECTED");
                else {
                    if (all_source_physical_ids.count(item.target_physical_id))
                        block(decision, "TARGET_IS_SELECTED_PHYSICAL_SOURCE");
                    if (existing_target_physical_counts[item.target_physical_id] > 1)
                        block(decision, "MULTIPLE_TARGETS_SAME_PHYSICAL_FILE");
                }
                if (item.target_guard.empty()) {
                    block(decision, "EXISTING_TARGET_NOT_GUARDED");
                } else if (!item.target_physical_id.empty()) {
                    ++review.requires_overwrite;
                    const bool approved_one =
                        individually_approved.count(item.physical_id) != 0;
                    const bool overwrite_allowed =
                        approved_one || blanket_overwrite;
                    if (overwrite_allowed) {
                        decision.will_replace_existing_target = true;
                    } else if (decision.status != PlanStatus::Blocked) {
                        decision.status = PlanStatus::NeedsOverwriteApproval;
                        decision.reasons.push_back("EXISTING_TARGET_REQUIRES_APPROVAL");
                    }
                }
            }
        }

        if (stale_approval)
            block(decision, "STALE_BATCH_APPROVAL");
        if (!decision.reasons.empty() &&
            decision.status != PlanStatus::NeedsOverwriteApproval)
            decision.status = PlanStatus::Blocked;
        if (decision.status == PlanStatus::Ready &&
            !decision.metadata_write_needed &&
            !decision.cue_bytes_write_needed &&
            !decision.file_operation_needed)
            decision.status = PlanStatus::Unchanged;
        if (decision.status == PlanStatus::Blocked) {
            decision.will_replace_existing_target = false;
            decision.metadata_write_needed = false;
            decision.cue_bytes_write_needed = false;
            decision.file_operation_needed = false;
            ++review.blocked;
        }
    }

    review.ready_to_apply = !items.empty() && review.blocked == 0 &&
        std::all_of(review.decisions.begin(), review.decisions.end(),
            [](const ItemDecision& item) {
                return item.status == PlanStatus::Ready ||
                       item.status == PlanStatus::Unchanged;
            });
    return review;
}

FrozenBatchPreview freeze_reviewed_batch_preview(
    const std::vector<FilePlanItem>& items,
    const BatchApproval& approval) {

    // Recalculate from input and the exact submitted approval; a UI cannot
    // freeze a stale reference to a previously Ready review result.
    if (approval.reviewed_plan_fingerprint.empty())
        throw std::invalid_argument("batch freeze requires explicit reviewed fingerprint");
    const BatchPlanReview reviewed = review_batch_plan(items, &approval);
    if (reviewed.plan_fingerprint != approval.reviewed_plan_fingerprint ||
        !reviewed.ready_to_apply ||
        reviewed.decisions.size() != items.size())
        throw std::invalid_argument("batch preview is stale, incomplete or unapproved");
    for (const ItemDecision& decision : reviewed.decisions) {
        if (decision.status != PlanStatus::Ready &&
            decision.status != PlanStatus::Unchanged)
            throw std::invalid_argument("batch preview contains unresolved item");
    }
    return FrozenBatchPreview{
        reviewed.plan_fingerprint,
        items,
        reviewed.decisions
    };
}

} // namespace djmeta
