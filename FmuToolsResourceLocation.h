// =============================================================================
// fmu-forge
//
// Copyright (c) 2024 Project Chrono (projectchrono.org)
// Copyright (c) 2024 Digital Dynamics Lab, University of Parma, Italy
// Copyright (c) 2024 Simulation Based Engineering Lab, University of Wisconsin-Madison, USA
// All rights reserved.
//
// Use of this source code is governed by a BSD-style license that can be found
// in the LICENSE file at the top level of the distribution.
//
// =============================================================================
// Decoding of the FMU resource location provided at instantiation
// =============================================================================

#ifndef FMUTOOLS_RESOURCELOCATION_H
#define FMUTOOLS_RESOURCELOCATION_H

#include <cctype>
#include <string>

namespace fmu_forge {

/// @addtogroup fmu_forge
/// @{

/// True for an ASCII letter. Used instead of std::isalpha, whose result is locale-dependent: in some
/// locales a byte above 0x7F classifies as alphabetic, which would let a non-ASCII byte be mistaken
/// for a Windows drive letter.
inline bool IsAsciiAlpha(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

/// True if the location carries the "file:" scheme. URI schemes are case-insensitive (RFC 3986,
/// section 3.1), so "FILE:", "File:" and "file:" are all accepted.
inline bool HasFileScheme(const std::string& location) {
    static const char scheme[] = "file:";
    if (location.size() < 5)
        return false;
    for (size_t i = 0; i < 5; i++) {
        char c = location[i];
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        if (c != scheme[i])
            return false;
    }
    return true;
}

/// Join a directory and a file name, inserting a separator only if one is needed.
/// Lets a caller append a file name without knowing whether the directory ends in a separator, which importers and
/// resource locations are inconsistent about. Either argument being empty yields plain concatenation.
inline std::string JoinPath(const std::string& dir, const std::string& filename) {
    if (dir.empty() || filename.empty())
        return dir + filename;
    char last = dir.back();
    if (last == '/' || last == '\\')
        return dir + filename;
    return dir + "/" + filename;
}

/// Decode percent-encoded ("%20") octets in a URI path.
/// Any '%' not followed by two hex digits is passed through unchanged.
inline std::string PercentDecode(const std::string& in) {
    auto hex = [](char c) -> int {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        return -1;
    };

    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size(); i++) {
        int hi, lo;
        if (in[i] == '%' && i + 2 < in.size() && (hi = hex(in[i + 1])) >= 0 && (lo = hex(in[i + 2])) >= 0) {
            out.push_back(static_cast<char>(hi * 16 + lo));
            i += 2;
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

/// Convert an FMU resource location into a filesystem path.
///
/// The FMI 2.0 standard specifies `fmuResourceLocation` as a URI, while FMI 3.0 specifies `resourcePath` as a plain
/// path. Importers are also inconsistent in practice, so this function accepts all of the forms actually encountered:
/// - a "file:" URI with any number of leading slashes, so both the standard `file:///abs/path` and the malformed
///   `file:////abs/path` historically emitted by this library's own importer on POSIX;
/// - an optional authority: empty or "localhost" as in `file://localhost/abs/path` means this machine and is
///   dropped, while any other host is preserved as a UNC path (`file://server/share` -> `//server/share`);
/// - the scheme in any case, since URI schemes are case-insensitive (`FILE:///abs/path`);
/// - a Windows drive letter in either the standard `file:///C:/path` or the non-standard `file://C:/path` form;
/// - percent-encoded octets, which importers produce for paths containing spaces;
/// - a plain filesystem path with no scheme at all, absolute or relative, POSIX or Windows.
///
/// A string carrying no "file:" scheme is a path already and is returned unchanged. In particular a literal '%' in a
/// filename is never mistaken for a percent-encoded octet, and a Windows UNC path is never mistaken for a URI
/// authority. A "file:" URI always yields a rooted path; an empty string is returned only for empty input.
inline std::string ResourceLocationToPath(const std::string& location) {
    if (!HasFileScheme(location))
        return location;

    std::string path = location.substr(5);
    std::string authority;

    // An authority component follows a "//" and ends at the next '/'. Suppressed for "file://C:/path", where what
    // looks like an authority is really a drive letter.
    bool drive_follows = path.size() >= 4 && IsAsciiAlpha(path[2]) && path[3] == ':';
    if (path.rfind("//", 0) == 0 && !drive_follows) {
        auto slash = path.find('/', 2);
        if (slash == std::string::npos) {
            authority = path.substr(2);
            path.clear();
        } else {
            authority = path.substr(2, slash - 2);
            path.erase(0, slash);
        }
    }

    path = PercentDecode(path);

    // Collapse the leading run of slashes to exactly one. This is what makes the standard three-slash form and the
    // malformed four-slash form resolve to the same absolute path. An empty or all-slash path denotes the root.
    auto first = path.find_first_not_of('/');
    if (first == std::string::npos)
        path = "/";
    else if (first > 1)
        path.erase(0, first - 1);

    // "/C:/path" -> "C:/path"
    if (path.size() >= 3 && path[0] == '/' && IsAsciiAlpha(path[1]) && path[2] == ':')
        path.erase(0, 1);

    // A non-local authority names the host holding the file (RFC 8089); the local spelling of that is a UNC path. An
    // empty authority and "localhost" both mean this machine and are dropped. Preserving the host matters: dropping it
    // silently turns a remote location into a different, possibly existing, local path.
    if (!authority.empty()) {
        std::string lowered;
        lowered.reserve(authority.size());
        for (char c : authority)
            lowered.push_back((c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c);
        if (lowered != "localhost")
            path = "//" + authority + path;
    }

    return path;
}

/// @} fmu_forge

}  // namespace fmu_forge

#endif
