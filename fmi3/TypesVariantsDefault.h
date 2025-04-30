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
// Definitions for the types platform (FMI 3.0)
// =============================================================================

#ifndef FMUTOLS_FMI3_TYPESVARIANTS_H
#define FMUTOLS_FMI3_TYPESVARIANTS_H

#include <string>

#include "variant/variant_guard.hpp"
#include "fmi3/fmi3_headers/fmi3PlatformTypes.h"

namespace fmu_forge {
namespace fmi3 {

/// @addtogroup fmu_forge_fmi3
/// @{

#define FMITYPESPLATFORM_DEFAULT

// TODO: DARIOM double check if it setter function for big objects can pass by (const?) reference instead of value

/// List of pointers to all (unique) types used to define VariableTypes.
/// No duplicated entries are allowed.
/// In general it does not coincide with the list VariableTypes defined in fmi3PlatformTypes since usually more
/// VariableTypes refer to the same underlying concrete type.
/// Additionally, the list also includes pairs of setter and getter functions in order to allow setting|getting values
/// from objects not having an actual address in memory. The types listed here will then be matched with any compatible
/// VariableType during the call to FmuComponentBase::AddFmuVariable().
/// Pointer types could either refer to single scalar variables or be pointers to an array.
///
/// Those types used to store fmi3String and fmi3Binary (by default, std::string and std::vector<fmi3Byte> respectively)
/// should be both considered as scalar variables instead of arrays by themselves.
/// The only slight difference is that functions handling fmi3Binary should consider that its size is not fixed.
///
using FmuVariableBindType = varns::variant<float*,                  // fmi3Float32
                                           double*,                 // fmi3Float64
                                           int8_t*,                 // fmi3Int8
                                           uint8_t*,                // fmi3UInt8
                                           int16_t*,                // fmi3Int16
                                           uint16_t*,               // fmi3UInt16
                                           int32_t*,                // fmi3Int32
                                           uint32_t*,               // fmi3UInt32
                                           int64_t*,                // fmi3Int64
                                           uint64_t*,               // fmi3UInt64
                                           bool*,                   // fmi3Boolean
                                           std::string*,            // fmi3String
                                           std::vector<fmi3Byte>*,  // fmi3Binary
                                           std::pair<std::function<float()>, std::function<void(float)>>,
                                           std::pair<std::function<double()>, std::function<void(double)>>,
                                           std::pair<std::function<int8_t()>, std::function<void(int8_t)>>,
                                           std::pair<std::function<uint8_t()>, std::function<void(uint8_t)>>,
                                           std::pair<std::function<int16_t()>, std::function<void(int16_t)>>,
                                           std::pair<std::function<uint16_t()>, std::function<void(uint16_t)>>,
                                           std::pair<std::function<int32_t()>, std::function<void(int32_t)>>,
                                           std::pair<std::function<uint32_t()>, std::function<void(uint32_t)>>,
                                           std::pair<std::function<int64_t()>, std::function<void(int64_t)>>,
                                           std::pair<std::function<uint64_t()>, std::function<void(uint64_t)>>,
                                           std::pair<std::function<bool()>, std::function<void(bool)>>,
                                           std::pair<std::function<char()>, std::function<void(char)>>,
                                           std::pair<std::function<std::string()>, std::function<void(std::string)>>>;

/// @} fmu_forge_fmi3

}  // namespace fmi3
}  // namespace fmu_forge

#endif
