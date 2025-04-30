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
// Definitions for the types platform (FMI 2.0)
// =============================================================================

#ifndef FMUTOLS_FMI2_TYPESVARIANTS_H
#define FMUTOLS_FMI2_TYPESVARIANTS_H

#include <string>

#include "variant/variant_guard.hpp"
#include "fmi2/fmi2_headers/fmi2TypesPlatform.h"

namespace fmu_forge {
namespace fmi2 {

/// @addtogroup fmu_forge_fmi2
/// @{

#define FMITYPESPLATFORM_DEFAULT

// Note: variants do not allow repeated types.
// Since in the FMI standard both fmi2Boolean and fmi2Integer point to a int type, by default neither
// If the user defines their own custom types, it is required to define new valid variants in order to
// cover all possible available types.

/// List of pointers to all (unique) types used to define VariableTypes.
/// No duplicated entries are allowed.
using FmuVariableBindType = varns::variant<                                     //
    fmi2Real*,                                                                  //
    fmi2Integer*,                                                               //
    std::string*,                                                               //
    std::pair<std::function<fmi2Real()>, std::function<void(fmi2Real)>>,        //
    std::pair<std::function<fmi2Integer()>, std::function<void(fmi2Integer)>>,  //
    std::pair<std::function<std::string()>, std::function<void(std::string)>>   //
    >;

using FmuVariableStartType = varns::variant<  //
    fmi2Real,                                 //
    fmi2Integer,                              //
    std::string                               //
    >;

/// @} fmu_forge_fmi2

}  // namespace fmi2
}  // namespace fmu_forge

#endif
