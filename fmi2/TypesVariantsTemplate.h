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

#ifndef FMUTOOLS_FMI2_TYPESVARIANTS_H
#define FMUTOOLS_FMI2_TYPESVARIANTS_H

#include <string>

#include "variant/variant_guard.hpp"
#include "fmi2/fmi2TypesPlatformCustom.h"

namespace fmu_forge {
namespace fmi2 {

#define FMITYPESPLATFORM_DEFAULT

// variants does not allow repeated types;
// unfortunately, in the default implementation, both fmi2Boolean and fmi2Integer point to a int type
// that's why, by default, FmuVariable::PtrType and FmuVariable::StartType does not have both of them
// however, in case the user has its own custom types definition, it is required to define new valid variants
// in order to cover all possible available types

/// FMU_ACTION: update the following declarations according to "fmi2TypesPlatformCustom.h"
using FmuVariableBindType = varns::variant<fmi2Real*,
                                           fmi2Integer*,
                                           fmi2Boolean*,
                                           fmi2String*,
                                           std::function<fmi2Real()>,
                                           std::function<fmi2Integer()>,
                                           std::function<fmi2Boolean()>,
                                           std::function<fmi2String()>>;
using FmuVariableStartType = varns::variant<fmi2Real, fmi2Integer, fmi2Boolean, std::string>;

}  // namespace fmi2
}  // namespace fmu_forge

#endif
