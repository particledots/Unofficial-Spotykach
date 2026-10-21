// Unofficial Spotykach - own code, GNU AGPLv3 (see LICENSE).
// Not an official Synthux Academy product; not affiliated with Synthux Academy.
#include "LoopLibrary.h"
#include "LoopFile.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace usp
{
    namespace
    {
        bool isWavName (const fs::path& p)
        {
            auto e = p.extension().string();
            std::transform (e.begin(), e.end(), e.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
            return e == ".wav";
        }

        bool sameFileAs (const std::string& a, const std::string& b)
        {
            std::error_code ec;
            if (a == b) return true;
            return fs::equivalent (fs::path (a), fs::path (b), ec) && ! ec;
        }

        // True if the file at `path` has the stored size and fingerprint. Fills `bytes` when it exists (matching or not).
        bool matches (const std::string& path, const LoopLink& link, std::vector<uint8_t>& bytes, bool& existed)
        {
            std::error_code ec;
            existed = fs::is_regular_file (fs::path (path), ec) && ! ec;
            if (! existed) return false;
            if (! readFileBytes (path, bytes)) { existed = false; return false; }
            return bytes.size() == link.size && fnv1a64 (bytes.data(), bytes.size()) == link.fingerprint;
        }
    }

    std::string fileNameOf (const std::string& path) { return fs::path (path).filename().string(); }

    bool readFileBytes (const std::string& path, std::vector<uint8_t>& bytes, size_t maxBytes)
    {
        bytes.clear();
        std::ifstream f (path, std::ios::binary | std::ios::ate);
        if (! f) return false;
        const std::streamoff len = f.tellg();
        if (len < 0 || (uint64_t) len > (uint64_t) maxBytes) return false;
        bytes.resize ((size_t) len);
        f.seekg (0);
        if (len > 0 && ! f.read (reinterpret_cast<char*> (bytes.data()), len)) { bytes.clear(); return false; }
        return true;
    }

    bool writeFileBytes (const std::string& path, const std::vector<uint8_t>& bytes)
    {
        std::error_code ec;
        const fs::path target (path);
        if (target.has_parent_path()) fs::create_directories (target.parent_path(), ec);
        const fs::path tmp = target.string() + ".part";
        {
            std::ofstream f (tmp, std::ios::binary | std::ios::trunc);
            if (! f) return false;
            if (! bytes.empty()) f.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
            f.flush();
            if (! f) { f.close(); fs::remove (tmp, ec); return false; }
        }
        fs::rename (tmp, target, ec);
        if (ec) { fs::remove (tmp, ec); return false; }
        return true;
    }

    LoopLink makeLink (const std::string& path, const std::string& homeFolder, const std::vector<uint8_t>& bytes)
    {
        LoopLink l;
        l.path = path;
        l.size = bytes.size();
        l.fingerprint = fnv1a64 (bytes.data(), bytes.size());
        if (! homeFolder.empty())
        {
            std::error_code ec;
            const auto rel = fs::path (path).lexically_relative (fs::path (homeFolder));
            const auto s = rel.generic_string();
            if (! rel.empty() && s.rfind ("..", 0) != 0 && ! ec) l.rel = s;
        }
        return l;
    }

    LinkResolution resolveLink (const LoopLink& link, const std::string& homeFolder)
    {
        LinkResolution res;
        if (! link.valid()) return res;

        std::vector<std::string> tried;
        std::string changedPath;
        std::vector<uint8_t> changedBytes;
        auto tryPath = [&] (const std::string& p) -> bool
        {
            for (auto& t : tried) if (sameFileAs (t, p)) return false;
            tried.push_back (p);
            std::vector<uint8_t> b;
            bool existed = false;
            if (matches (p, link, b, existed))
            {
                res.state = LinkState::found;
                res.path = p;
                res.bytes = std::move (b);
                return true;
            }
            if (existed && changedPath.empty()) { changedPath = p; changedBytes = std::move (b); }
            return false;
        };

        if (tryPath (link.path)) return res;
        if (! homeFolder.empty() && ! link.rel.empty())
        {
            // The same place relative to this computer's home folder (a project moved to another Mac).
            const std::string p = (fs::path (homeFolder) / fs::path (link.rel)).string();
            if (tryPath (p))
            {
                res.state = LinkState::found;
                return res;
            }
        }

        // Search the home folder for a .wav with the same size and fingerprint.
        if (! homeFolder.empty())
        {
            std::error_code ec;
            fs::recursive_directory_iterator it (fs::path (homeFolder), fs::directory_options::skip_permission_denied, ec), end;
            size_t examined = 0;
            for (; ! ec && it != end; it.increment (ec))
            {
                if (++examined > kMaxSearchEntries) break;
                std::error_code e2;
                if (! it->is_regular_file (e2) || e2 || ! isWavName (it->path())) continue;
                const auto sz = it->file_size (e2);
                if (e2 || (uint64_t) sz != link.size) continue;
                const std::string p = it->path().string();
                bool dup = false;
                for (auto& t : tried) if (sameFileAs (t, p)) { dup = true; break; }
                if (dup) continue;
                tried.push_back (p);
                std::vector<uint8_t> b;
                if (! readFileBytes (p, b)) continue;
                if (fnv1a64 (b.data(), b.size()) == link.fingerprint)
                {
                    res.state = LinkState::moved;
                    res.path = p;
                    res.bytes = std::move (b);
                    return res;
                }
            }
        }

        if (! changedPath.empty())
        {
            res.state = LinkState::changed;
            res.path = changedPath;
            res.bytes = std::move (changedBytes);
            return res;
        }
        res.state = LinkState::missing;
        return res;
    }
}
