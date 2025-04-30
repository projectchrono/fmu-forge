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
// FMI unit definitions
// =============================================================================

#ifndef FMUTOOLS_UNITDEFINITIONS_H
#define FMUTOOLS_UNITDEFINITIONS_H

#include <string>

namespace fmu_forge {

/// @addtogroup fmu_forge
/// @{

/// Definition of an FMI unit.
/// A unit definition consists of the exponents of the seven SI base units kg, m, s, A, K, mol, cd, and the exponent of
/// the SI derived unit rad.
struct UnitDefinition {
    UnitDefinition(const std::string& _name = "1") : name(_name) {}

    UnitDefinition(const std::string& _name, int _kg, int _m, int _s, int _A, int _K, int _mol, int _cd, int _rad)
        : name(_name), kg(_kg), m(_m), s(_s), A(_A), K(_K), mol(_mol), cd(_cd), rad(_rad) {}

    virtual ~UnitDefinition() {}

    std::string name;
    int kg = 0;
    int m = 0;
    int s = 0;
    int A = 0;
    int K = 0;
    int mol = 0;
    int cd = 0;
    int rad = 0;

    struct Hash {
        size_t operator()(const UnitDefinition& p) const { return std::hash<std::string>()(p.name); }
    };

    bool operator==(const UnitDefinition& other) const { return name == other.name; }
};

extern const std::unordered_set<UnitDefinition, UnitDefinition::Hash> common_unitdefinitions;

// Base SI units            |name|kg, m, s, A, K,mol,cd,rad

static const UnitDefinition UD_kg("kg", 1, 0, 0, 0, 0, 0, 0, 0);
static const UnitDefinition UD_m("m", 0, 1, 0, 0, 0, 0, 0, 0);
static const UnitDefinition UD_s("s", 0, 0, 1, 0, 0, 0, 0, 0);
static const UnitDefinition UD_A("A", 0, 0, 0, 1, 0, 0, 0, 0);
static const UnitDefinition UD_K("K", 0, 0, 0, 0, 1, 0, 0, 0);
static const UnitDefinition UD_mol("mol", 0, 0, 0, 0, 0, 1, 0, 0);
static const UnitDefinition UD_cd("cd", 0, 0, 0, 0, 0, 0, 1, 0);
static const UnitDefinition UD_rad("rad", 0, 0, 0, 0, 0, 0, 0, 1);

// Derived units            |name|kg, m, s, A, K,mol,cd,rad

static const UnitDefinition UD_m_s("m/s", 0, 1, -1, 0, 0, 0, 0, 0);
static const UnitDefinition UD_m_s2("m/s2", 0, 1, -2, 0, 0, 0, 0, 0);
static const UnitDefinition UD_rad_s("rad/s", 0, 0, -1, 0, 0, 0, 0, 1);
static const UnitDefinition UD_rad_s2("rad/s2", 0, 0, -2, 0, 0, 0, 0, 1);

static const UnitDefinition UD_N("N", 1, 1, -2, 0, 0, 0, 0, 0);
static const UnitDefinition UD_Nm("Nm", 1, 2, -2, 0, 0, 0, 0, 0);
static const UnitDefinition UD_N_m2("N/m2", 1, -1, -2, 0, 0, 0, 0, 0);

/// @} fmu_forge

}  // namespace fmu_forge

#endif
