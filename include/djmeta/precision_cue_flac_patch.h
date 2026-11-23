#pragma once
// Pure, bounded FLAC Vorbis CUESHEET postimage planning. No filesystem writes.
// Changes only ONE existing textual Vorbis comment plus a FLAC PADDING block
// to maintain the EXACT metadata-region length and audio byte offset.
// All other metadata (including ReplayGain and unknown blocks) stays intact.
// Native binary FLAC Type-5 CUESHEET blocks are preserve-only / not editable.
#include "djmeta/external_cue.h"
#include "djmeta/precision_cue_embedded_probe.h"
#include "djmeta/precision_cue_sha256.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace djmeta {

enum class PrecisionCueFlacPatchState {
    Rejected, ReviewRequired, NoOp, PreviewReady
};
struct PrecisionCueFlacPatch {
    PrecisionCueFlacPatchState state = PrecisionCueFlacPatchState::Rejected;
    std::string diagnostic = "FLAC_CUE_PATCH_REJECTED";
    std::string postimage_metadata; // "fLaC" + blocks, same bytes count.
    std::array<std::uint8_t,32> source_sha256{};
    std::array<std::uint8_t,32> postimage_sha256{};
    std::size_t metadata_bytes = 0;
    unsigned untouched_comment_count = 0;
    unsigned untouched_block_count = 0;
};

namespace precision_cue_flac_patch_detail {
struct Block {
    std::size_t head = 0, data = 0;
    std::uint32_t size = 0;
    unsigned type = 0;
};
inline void put_le32(std::string& to, std::uint32_t value) {
    for (unsigned i=0; i<4; ++i)
        to.push_back(static_cast<char>((value>>(i*8))&255));
}
inline std::uint32_t le32(std::string_view bytes, std::size_t i) {
    std::uint32_t n=0;
    for (unsigned k=0; k<4; ++k)
        n |= std::uint32_t(static_cast<unsigned char>(bytes[i+k]))<<(8*k);
    return n;
}
inline void write_flac_block_header(std::string& out, char old_type_flag,
                                    std::uint32_t length) {
    out.push_back(old_type_flag);
    out.push_back(static_cast<char>((length>>16)&255));
    out.push_back(static_cast<char>((length>>8)&255));
    out.push_back(static_cast<char>(length&255));
}
} // namespace precision_cue_flac_patch_detail

inline PrecisionCueFlacPatch preview_precision_cue_flac_vorbis_patch(
    std::string_view source_metadata,
    std::string_view proposed_cue_utf8,
    PrecisionCueEmbeddedState raw_cue_state) {
    using namespace precision_cue_flac_patch_detail;
    PrecisionCueFlacPatch result;
    const auto fail=[&](PrecisionCueFlacPatchState state,const char* reason) {
        result.state=state;
        result.diagnostic=reason;
        result.postimage_metadata.clear();
        return result;
    };
    if (raw_cue_state != PrecisionCueEmbeddedState::TextualPresent)
        return fail(PrecisionCueFlacPatchState::ReviewRequired,
                    "FLAC_NATIVE_OR_CONFLICTING_CUE_PRESERVE_ONLY");
    if (source_metadata.size() < 42 || source_metadata.size() > 4u*1024u*1024u ||
        source_metadata.substr(0,4)!="fLaC")
        return fail(PrecisionCueFlacPatchState::Rejected,
                    "FLAC_METADATA_PREFIX_UNQUALIFIED");
    const auto proposed=inspect_external_cue(proposed_cue_utf8);
    if (proposed.status!=CueSyntaxStatus::Parsed ||
        proposed.encoding!=CueTextEncoding::Utf8 ||
        proposed_cue_utf8.size()>1024*1024)
        return fail(PrecisionCueFlacPatchState::Rejected,
                    "FLAC_PROPOSED_CUE_SYNTAX_UNQUALIFIED");

    std::vector<Block> blocks;
    std::size_t cursor=4;
    bool last=false;
    int comment_index=-1, padding_index=-1;
    unsigned type5=0;
    for (unsigned i=0; i<128 && !last; ++i) {
        if (source_metadata.size()-cursor < 4)
            return fail(PrecisionCueFlacPatchState::Rejected,
                        "FLAC_METADATA_BLOCK_TRUNCATED");
        const auto f=static_cast<unsigned char>(source_metadata[cursor]);
        Block block;
        block.head=cursor;
        block.data=cursor+4;
        block.type=f & 127u;
        last=(f & 128u)!=0;
        block.size =
            (std::uint32_t(static_cast<unsigned char>(source_metadata[cursor+1]))<<16) |
            (std::uint32_t(static_cast<unsigned char>(source_metadata[cursor+2]))<<8) |
            (std::uint32_t(static_cast<unsigned char>(source_metadata[cursor+3])));
        if (block.type==127 || block.size>4u*1024u*1024u ||
            block.size>source_metadata.size()-block.data)
            return fail(PrecisionCueFlacPatchState::Rejected,
                        "FLAC_BLOCK_LENGTH_OR_TYPE_INVALID");
        if (i==0 && (block.type!=0 || block.size!=34))
            return fail(PrecisionCueFlacPatchState::Rejected,
                        "FLAC_STREAMINFO_NOT_CANONICAL");
        if (block.type==4) {
            if (comment_index!=-1)
                return fail(PrecisionCueFlacPatchState::ReviewRequired,
                            "FLAC_MULTIPLE_VORBIS_BLOCKS");
            comment_index=static_cast<int>(blocks.size());
        } else if (block.type==1) {
            // All FLAC padding bytes must be zero. Only one is needed to
            // safely compensate for variable-length CUESHEET text.
            for (std::size_t k=0; k<block.size; ++k)
                if (source_metadata[block.data+k]!='\0')
                    return fail(PrecisionCueFlacPatchState::Rejected,
                                "FLAC_NONZERO_PADDING");
            if (padding_index!=-1)
                return fail(PrecisionCueFlacPatchState::ReviewRequired,
                            "FLAC_MULTIPLE_PADDING_BLOCKS");
            padding_index=static_cast<int>(blocks.size());
        } else if (block.type==5) ++type5;
        else ++result.untouched_block_count;
        blocks.push_back(block);
        cursor=block.data+block.size;
    }
    if (!last || cursor!=source_metadata.size())
        return fail(PrecisionCueFlacPatchState::Rejected,
                    "FLAC_METADATA_REGION_INVALID");
    if (type5!=0 || comment_index<0)
        return fail(PrecisionCueFlacPatchState::ReviewRequired,
                    "FLAC_BINARY_TYPE5_OR_MISSING_VORBIS");
    const Block& comment=blocks[static_cast<std::size_t>(comment_index)];
    const auto bytes=source_metadata.substr(comment.data,comment.size);
    if (bytes.size()<8) return fail(PrecisionCueFlacPatchState::Rejected,
                                     "FLAC_VORBIS_HEADER_TRUNCATED");
    const auto vendor=le32(bytes,0);
    if (vendor>bytes.size()-4 || bytes.size()-4-vendor<4)
        return fail(PrecisionCueFlacPatchState::Rejected,
                    "FLAC_VORBIS_VENDOR_INVALID");
    std::size_t at=4+vendor;
    const auto n=le32(bytes,at);
    at+=4;
    if (n>65536)
        return fail(PrecisionCueFlacPatchState::Rejected,
                    "FLAC_VORBIS_COUNT_INVALID");

    std::string new_vorbis=std::string(bytes.substr(0,at));
    unsigned cue_count=0;
    std::string old_cue;
    for (std::uint32_t i=0; i<n; ++i) {
        if (bytes.size()-at<4)
            return fail(PrecisionCueFlacPatchState::Rejected,
                        "FLAC_VORBIS_FIELD_LENGTH_TRUNCATED");
        const auto length=le32(bytes,at);
        const auto pos=at;
        at+=4;
        if (length>bytes.size()-at)
            return fail(PrecisionCueFlacPatchState::Rejected,
                        "FLAC_VORBIS_FIELD_OUT_OF_RANGE");
        const auto field=bytes.substr(at,length);
        const auto eq=field.find('=');
        if (eq==std::string_view::npos || eq==0)
            return fail(PrecisionCueFlacPatchState::Rejected,
                        "FLAC_VORBIS_FIELD_INVALID");
        if (precision_cue_embedded_detail::cue_field(field.substr(0,eq))) {
            ++cue_count;
            if (cue_count>1)
                return fail(PrecisionCueFlacPatchState::ReviewRequired,
                            "FLAC_DUPLICATE_CUESHEET_VALUES");
            old_cue=std::string(field.substr(eq+1));
            const auto new_length=eq+1+proposed_cue_utf8.size();
            if (new_length>4u*1024u*1024u)
                return fail(PrecisionCueFlacPatchState::ReviewRequired,
                            "FLAC_VORBIS_CUE_TOO_LARGE");
            put_le32(new_vorbis,static_cast<std::uint32_t>(new_length));
            new_vorbis.append(field.substr(0,eq+1));
            new_vorbis.append(proposed_cue_utf8);
        } else {
            new_vorbis.append(bytes.substr(pos,4+length));
            ++result.untouched_comment_count;
        }
        at+=length;
    }
    if (at!=bytes.size())
        return fail(PrecisionCueFlacPatchState::Rejected,
                    "FLAC_TRAILING_VORBIS_DATA");
    if (cue_count!=1)
        return fail(PrecisionCueFlacPatchState::ReviewRequired,
                    "FLAC_UNIQUE_CUESHEET_NOT_FOUND");
    const auto existing=inspect_external_cue(old_cue);
    if (existing.status!=CueSyntaxStatus::Parsed ||
        existing.encoding!=CueTextEncoding::Utf8 ||
        existing.references.size()!=proposed.references.size() ||
        existing.references.empty())
        return fail(PrecisionCueFlacPatchState::ReviewRequired,
                    "FLAC_EXISTING_CUE_UNQUALIFIED");
    for (std::size_t i=0; i<existing.references.size(); ++i) {
        if (existing.references[i].filename!=proposed.references[i].filename ||
            existing.references[i].file_type!=proposed.references[i].file_type)
            return fail(PrecisionCueFlacPatchState::ReviewRequired,
                        "FLAC_CUE_FILE_REFERENCE_CHANGED");
    }
    result.source_sha256=precision_cue_sha256(source_metadata);
    result.metadata_bytes=source_metadata.size();
    if (old_cue==proposed_cue_utf8) {
        result.postimage_sha256=result.source_sha256;
        result.state=PrecisionCueFlacPatchState::NoOp;
        result.diagnostic="FLAC_TEXTUAL_CUE_NOOP";
        return result;
    }
    const auto delta=static_cast<std::int64_t>(new_vorbis.size())-
                     static_cast<std::int64_t>(comment.size);
    if (new_vorbis.size()>4u*1024u*1024u || new_vorbis.size()>0xffffffu)
        return fail(PrecisionCueFlacPatchState::ReviewRequired,
                    "FLAC_NEW_VORBIS_BLOCK_TOO_LARGE");
    if (delta!=0 && padding_index<0)
        return fail(PrecisionCueFlacPatchState::ReviewRequired,
                    "FLAC_NO_PADDING_TO_PRESERVE_AUDIO_OFFSETS");
    std::uint32_t new_padding=0;
    if (padding_index>=0) {
        const auto old_padding=blocks[static_cast<std::size_t>(padding_index)].size;
        if (delta>static_cast<std::int64_t>(old_padding) ||
            -delta>static_cast<std::int64_t>(0xffffffu-old_padding))
            return fail(PrecisionCueFlacPatchState::ReviewRequired,
                        "FLAC_INSUFFICIENT_PADDING");
        new_padding=static_cast<std::uint32_t>(
            static_cast<std::int64_t>(old_padding)-delta);
    }

    result.postimage_metadata="fLaC";
    for (std::size_t i=0; i<blocks.size(); ++i) {
        const auto& block=blocks[i];
        const auto flag=source_metadata[block.head];
        if (static_cast<int>(i)==comment_index) {
            write_flac_block_header(result.postimage_metadata,flag,
                static_cast<std::uint32_t>(new_vorbis.size()));
            result.postimage_metadata+=new_vorbis;
        } else if (static_cast<int>(i)==padding_index) {
            write_flac_block_header(result.postimage_metadata,flag,new_padding);
            result.postimage_metadata.append(new_padding,'\0');
        } else {
            result.postimage_metadata.append(
                source_metadata.substr(block.head,4+block.size));
        }
    }
    if (result.postimage_metadata.size()!=source_metadata.size())
        return fail(PrecisionCueFlacPatchState::Rejected,
                    "FLAC_POSTIMAGE_METADATA_LENGTH_CHANGED");
    result.postimage_sha256=precision_cue_sha256(result.postimage_metadata);
    result.state=PrecisionCueFlacPatchState::PreviewReady;
    result.diagnostic="FLAC_VORBIS_CUE_POSTIMAGE_PREVIEW_ONLY";
    return result;
}
} // namespace djmeta
