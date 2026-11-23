#include "djmeta/precision_cue_full_file_snapshot.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <string_view>

using namespace djmeta;
#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr,"Precision CUE full-file guard failed at %d: %s\n",\
        __LINE__,#condition); std::exit(1); } } while(false)

namespace {
struct TempArea {
    std::filesystem::path root;
    TempArea() {
        namespace fs=std::filesystem;
        std::random_device random;
        root=fs::canonical(fs::temp_directory_path()) /
            ("precision-cue-snapshot-" + std::to_string(random()) +
             "-" + std::to_string(random()));
        CHECK(fs::create_directory(root));
    }
    ~TempArea() {
        std::error_code ignored;
        std::filesystem::remove_all(root,ignored);
    }
};
void save(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path,std::ios::binary);
    CHECK(output.good());
    output.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    CHECK(output.good());
}
void all_read_only_source_integrity_fixtures() {
    namespace fs=std::filesystem;
    TempArea sandbox;
    const auto physical=fs::canonical(sandbox.root) / "Synthetic Mix.mp3";
    std::string prefix="ID3";
    prefix.append(2045,'P');
    prefix[121]='\0'; prefix[717]=static_cast<char>(255);
    std::string payload((64u*1024u)*3u+197u,'\0');
    for (std::size_t i=0;i<payload.size();++i)
        payload[i]=static_cast<char>((i*29u+43u)&255);
    const auto bytes=prefix+payload; // All synthetic; no real audio.
    save(physical,bytes);
    const auto original_time=fs::last_write_time(physical);
    const auto review=capture_precision_cue_physical_full_snapshot(physical,prefix);
    CHECK(review.has_value());
    CHECK(review->canonical_path==physical);
    CHECK(review->physical.bytes==bytes.size());
    CHECK(review->reviewed_prefix_size==prefix.size());
    CHECK(review->reviewed_prefix_sha256==precision_cue_sha256(prefix));
    CHECK(review->full_file_sha256==precision_cue_sha256(bytes));
    CHECK(revalidate_precision_cue_physical_full_snapshot(*review,prefix));
    CHECK(!revalidate_precision_cue_physical_full_snapshot(*review,"wrong"));
    CHECK(!capture_precision_cue_physical_full_snapshot(physical,"WRONG"));

    // Same-file, same-size, same-mtime mutation in the audio region: the
    // full-file SHA rejects changes that stat-only checks cannot detect.
    const std::size_t changed_at=prefix.size()+64000;
    {
        std::fstream output(physical,std::ios::in|std::ios::out|std::ios::binary);
        CHECK(output.good());
        output.seekp(static_cast<std::streamoff>(changed_at));
        output.put(static_cast<char>(bytes[changed_at]^0x5a));
        CHECK(output.good());
    }
    fs::last_write_time(physical,original_time);
    CHECK(fs::file_size(physical)==bytes.size());
    CHECK(!revalidate_precision_cue_physical_full_snapshot(*review,prefix));

    save(physical,bytes);
    fs::last_write_time(physical,original_time);
    CHECK(revalidate_precision_cue_physical_full_snapshot(*review,prefix));

    // Changing the reviewed ID3 bytes invalidates both the prefix and the
    // full-file fingerprint without accepting a transient equal-size edit.
    {
        std::fstream output(physical,std::ios::in|std::ios::out|std::ios::binary);
        CHECK(output.good());
        output.seekp(319);
        output.put('Q');
        CHECK(output.good());
    }
    fs::last_write_time(physical,original_time);
    CHECK(!capture_precision_cue_physical_full_snapshot(physical,prefix));
    CHECK(!revalidate_precision_cue_physical_full_snapshot(*review,prefix));

    // Same path, content, size and timestamp, but DIFFERENT OS file ID.
    // This must fail even if the full SHA is byte-identical.
    const auto old_path=sandbox.root/"original-synthetic.keep";
    fs::rename(physical,old_path);
    save(physical,bytes);
    fs::last_write_time(physical,original_time);
    CHECK(fs::file_size(physical)==review->physical.bytes);
    const auto replaced=capture_precision_cue_physical_full_snapshot(physical,prefix);
    CHECK(replaced.has_value());
    CHECK(replaced->full_file_sha256==review->full_file_sha256);
    CHECK(replaced->physical.file_id!=review->physical.file_id ||
          replaced->physical.volume_id!=review->physical.volume_id);
    CHECK(!revalidate_precision_cue_physical_full_snapshot(*review,prefix));

    // Refuse absent, relative, directory and unreviewed/inordinately large
    // prefixes; no file is ever opened in write mode by the production API.
    CHECK(!capture_precision_cue_physical_full_snapshot(
        sandbox.root/"missing.mp3",prefix));
    CHECK(!capture_precision_cue_physical_full_snapshot(
        "relative-source.mp3",prefix));
    CHECK(!capture_precision_cue_physical_full_snapshot(sandbox.root,prefix));
    CHECK(!capture_precision_cue_physical_full_snapshot(physical,""));
    CHECK(!capture_precision_cue_physical_full_snapshot(
        physical,std::string(4u*1024u*1024u+11u,'X')));
#ifndef _WIN32
    const auto alias=sandbox.root/"symbolic-source.mp3";
    fs::create_symlink(physical,alias);
    CHECK(!capture_precision_cue_physical_full_snapshot(alias,prefix));
#endif
}
}
int main() {
    all_read_only_source_integrity_fixtures();
    std::puts("PASS: full physical audio-byte SHA256 snapshot, stale and identity checks");
    return 0;
}
