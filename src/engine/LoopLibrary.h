// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
//
// Loop files on disk and the links a project keeps to them (decision D36 / D53).
//
// A project does not hold the audio, only a link per deck: the file's path, its path relative to the
// home folder (when it lies inside it), its size and a fingerprint (FNV-1a hash of its bytes). When a
// project is opened the file is found again by path; if it has moved or been renamed, the home folder
// is searched for a .wav with the same size and fingerprint. A file with the same name but different
// audio is never taken for the original unless it sits at the stored path (then it is loaded and
// flagged as changed).
//
// JUCE-free (std::filesystem) so the headless tests can use it. Never called from the audio thread.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace usp
{
    struct LoopLink
    {
        std::string path;        // absolute path when the link was made
        std::string rel;         // path relative to the home folder, or empty when the file is outside it
        uint64_t size = 0;       // bytes
        uint64_t fingerprint = 0;
        bool valid() const { return ! path.empty(); }
    };

    enum class LinkState
    {
        found,      // the file is where it was and is unchanged
        moved,      // not where it was, found elsewhere under the home folder by size + fingerprint
        changed,    // a file is at the stored path but its contents differ from when the link was made
        missing     // nothing found
    };

    struct LinkResolution
    {
        LinkState state = LinkState::missing;
        std::string path;                 // where the file is (empty when missing)
        std::vector<uint8_t> bytes;       // its contents (empty when missing)
    };

    constexpr size_t kMaxLoopFileBytes = (size_t) 1 << 30;     // refuse files over 1 GiB
    constexpr size_t kMaxSearchEntries = 50000;                // directory entries examined when searching

    // Reads a whole file. False if it cannot be opened/read or is larger than maxBytes.
    bool readFileBytes (const std::string& path, std::vector<uint8_t>& bytes, size_t maxBytes = kMaxLoopFileBytes);

    // Writes through a temporary file and renames it, so a failed write never leaves a half file. Creates the
    // folder when needed.
    bool writeFileBytes (const std::string& path, const std::vector<uint8_t>& bytes);

    LoopLink makeLink (const std::string& path, const std::string& homeFolder, const std::vector<uint8_t>& bytes);

    // Finds the file a link refers to. See the header comment for the order.
    LinkResolution resolveLink (const LoopLink& link, const std::string& homeFolder);

    // The file name part of a path.
    std::string fileNameOf (const std::string& path);
}
