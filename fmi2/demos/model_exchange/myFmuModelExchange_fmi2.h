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
// Example FMU for model exchange (FMI 2.0 standard)
// Illustrates the FMU exporting capabilities in fmu-forge (FmuToolsExport.h)
// =============================================================================

#pragma once

#include <vector>
#include <array>
#include <fstream>

// #define FMI2_FUNCTION_PREFIX MyModel_
#include "fmi2/FmuToolsExport.h"

class myFmuComponent : public fmu_forge::fmi2::FmuComponentBase {
  public:
    myFmuComponent(fmi2String instanceName,
                   fmi2Type fmuType,
                   fmi2String fmuGUID,
                   fmi2String fmuResourceLocation,
                   const fmi2CallbackFunctions* functions,
                   fmi2Boolean visible,
                   fmi2Boolean loggingOn);

    ~myFmuComponent() {}

  private:
    // FMU implementation overrides
    virtual bool is_cosimulation_available() const override { return false; }
    virtual bool is_modelexchange_available() const override { return true; }

    virtual fmi2Status enterInitializationModeIMPL() override;
    virtual fmi2Status exitInitializationModeIMPL() override;

    virtual fmi2Status getContinuousStatesIMPL(fmi2Real x[], size_t nx) override;
    virtual fmi2Status setContinuousStatesIMPL(const fmi2Real x[], size_t nx) override;
    virtual fmi2Status getDerivativesIMPL(fmi2Real derivatives[], size_t nx) override;

    // Problem-specific functions
    typedef std::array<double, 4> vec4;

    double calcX_dd(double theta, double theta_d);
    double calcTheta_dd(double theta, double theta_d);
    vec4 calcRHS(double t, const vec4& q);

    void calcAccelerations();

    // Problem parameters
    double len = 0.5;
    double m = 1.0;
    double M = 1.0;
    const double g = 9.81;

    fmi2Boolean approximateOn = fmi2False;
    std::string filename;

    // Problem states
    vec4 q;
    double x_dd;
    double theta_dd;
};
