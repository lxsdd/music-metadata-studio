#include "stdafx.h"

#include "metadata_adapter.h"
#include "preview.h"
#include "rules_runtime.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace djmeta_foobar {
namespace {

std::string display_value(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (unsigned char c : value) {
        if (c == '\r' || c == '\n' || c == '\t' || c == 0) out.push_back(' ');
        else out.push_back(static_cast<char>(c));
    }
    return out;
}

void append_rules(std::string& out, const std::vector<std::string>& ids) {
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i) out += ", ";
        out += ids[i];
    }
}

} // namespace

void show_normalization_preview(const metadb_handle_list& handles) {
    try {
        const loaded_rules_text loaded = load_rules_text();
        const djmeta::Ruleset ruleset = djmeta::parse_ruleset_json(loaded.json);

        struct analyzed_item {
            metadb_handle_ptr handle;
            djmeta::AnalysisResult result;
        };

        std::vector<analyzed_item> analyzed;
        analyzed.reserve(static_cast<std::size_t>(handles.get_count()));

        // Phase 1: analyze an exact metadata snapshot for every selected item.
        for (t_size item_index = 0; item_index < handles.get_count(); ++item_index) {
            const metadb_handle_ptr& handle = handles[item_index];
            const auto container = handle->get_info_ref();
            const file_info& info = container->info();
            const djmeta::MetadataDocument input = metadata_from_file_info(info);

            analyzed_item item;
            item.handle = handle;
            item.result = djmeta::Engine{}.analyze(input, ruleset.rules, ruleset.revision);
            analyzed.push_back(std::move(item));
        }

        // Phase 2: stale-input firewall. A large multi-selection can take long enough for
        // metadata to change while earlier entries are being analyzed. Re-read every item
        // after the complete analysis and reject the whole preview if any fingerprint moved.
        for (const analyzed_item& item : analyzed) {
            const auto current_container = item.handle->get_info_ref();
            const djmeta::MetadataDocument current =
                metadata_from_file_info(current_container->info());
            if (djmeta::fingerprint(current) != item.result.input_fingerprint) {
                throw std::runtime_error(
                    "Metadata changed during analysis. "
                    "The preview is stale. Please run the analysis again.");
            }
        }

        std::string detail;
        std::size_t proposal_count = 0;
        std::size_t safe_count = 0;
        std::size_t confident_count = 0;
        std::size_t review_count = 0;
        constexpr std::size_t kDetailLimit = 80;

        // Phase 3: only validated snapshots are rendered.
        for (const analyzed_item& item : analyzed) {
            for (const djmeta::Proposal& proposal : item.result.proposals) {
                ++proposal_count;
                switch (proposal.safety) {
                case djmeta::SafetyClass::Safe: ++safe_count; break;
                case djmeta::SafetyClass::Confident: ++confident_count; break;
                case djmeta::SafetyClass::Review: ++review_count; break;
                }

                if (proposal_count > kDetailLimit) continue;

                detail += "\n";
                detail += item.handle->get_path();
                detail += " [";
                detail += std::to_string(item.handle->get_subsong_index());
                detail += "]\n  [";
                detail += djmeta::to_string(proposal.safety);
                detail += "] ";
                detail += proposal.field;
                detail += ": \"";
                detail += display_value(proposal.original_value);
                detail += "\" -> \"";
                detail += display_value(proposal.proposed_value);
                detail += "\"\n  Rules: ";
                append_rules(detail, proposal.rule_ids);
                detail += "\n";
            }
        }

        std::string message;
        message += "Ruleset: ";
        message += loaded.source_label;
        message += "\nRevision: ";
        message += ruleset.revision;
        message += "\nSelected tracks: ";
        message += std::to_string(handles.get_count());
        message += "\nProposals: ";
        message += std::to_string(proposal_count);
        message += " (SAFE ";
        message += std::to_string(safe_count);
        message += ", CONFIDENT ";
        message += std::to_string(confident_count);
        message += ", REVIEW ";
        message += std::to_string(review_count);
        message += ")\n";

        if (proposal_count == 0) {
            message += "\nNo normalization proposals. Nothing was changed.";
        } else {
            message += detail;
            if (proposal_count > kDetailLimit) {
                message += "\n... ";
                message += std::to_string(proposal_count - kDetailLimit);
                message += " additional proposals are not displayed in this early preview.";
            }
            message += "\n\nPreview only: No tags were written.";
        }

        popup_message::g_show(message.c_str(), "Music Metadata Studio");
    } catch (const std::exception& error) {
        std::string message =
            "Unable to create normalization preview. Nothing was changed.\n\n";
        message += error.what();
        popup_message::g_show(message.c_str(), "Music Metadata Studio");
    }
}

} // namespace djmeta_foobar
