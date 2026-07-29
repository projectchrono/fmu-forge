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
/// - an optional authority, empty or "localhost", as in `file://localhost/abs/path`;
/// - a Windows drive letter in either the standard `file:///C:/path` or the non-standard `file://C:/path` form;
/// - percent-encoded octets, which importers produce for paths containing spaces;
/// - a plain filesystem path with no scheme at all, absolute or relative, POSIX or Windows.
///
/// A string carrying no "file:" scheme is a path already and is returned unchanged. In particular a literal '%' in a
/// filename is never mistaken for a percent-encoded octet, and a Windows UNC path is never mistaken for a URI
/// authority. Returns an empty string only for empty input.
inline std::string ResourceLocationToPath(const std::string& location) {
    if (location.rfind("file:", 0) != 0)
        return location;

    std::string path = location.substr(5);

    // An authority component follows a "//" and ends at the next '/'. Suppressed for "file://C:/path", where what
    // looks like an authority is really a drive letter.
    bool drive_follows = path.size() >= 4 && std::isalpha(static_cast<unsigned char>(path[2])) && path[3] == ':';
    if (path.rfind("//", 0) == 0 && !drive_follows) {
        auto slash = path.find('/', 2);
        path.erase(0, slash == std::string::npos ? path.size() : slash);
    }

    path = PercentDecode(path);

    // Collapse the leading run of slashes to a single one. This is what makes the standard three-slash form and the
    // malformed four-slash form resolve to the same absolute path.
    auto first = path.find_first_not_of('/');
    if (first != std::string::npos && first > 1)
        path.erase(0, first - 1);

    // "/C:/path" -> "C:/path"
    if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) && path[2] == ':')
        path.erase(0, 1);

    return path;
}

/// @} fmu_forge

}  // namespace fmu_forge

#endif
