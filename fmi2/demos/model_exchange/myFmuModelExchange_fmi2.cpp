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

#define NOMINMAX

#include <cassert>
#include <map>
#include <algorithm>

#define _USE_MATH_DEFINES
#include <math.h>

// #define FMI2_FUNCTION_PREFIX MyModel_
#include "myFmuModelExchange_fmi2.h"

using namespace fmu_forge;
using namespace fmu_forge::fmi2;

// -----------------------------------------------------------------------------

// Implement function declared in FmuToolsExport.h to create an instance of this FMU.
FmuComponentBase* fmi2::fmi2InstantiateIMPL(fmi2String instanceName,
                                            fmi2Type fmuType,
                                            fmi2String fmuGUID,
                                            fmi2String fmuResourceLocation,
                                            const fmi2CallbackFunctions* functions,
                                            fmi2Boolean visible,
                                            fmi2Boolean loggingOn) {
    return new myFmuComponent(instanceName, fmuType, fmuGUID, fmuResourceLocation, functions, visible, loggingOn);
}

// -----------------------------------------------------------------------------

// During construction of the FMU component:
// - list the log categories that this FMU should handle
// - specify a flag to enable/disable logging
// - specify which log categories are to be considered as debug
myFmuComponent::myFmuComponent(fmi2String instanceName,
                               fmi2Type fmuType,
                               fmi2String fmuGUID,
                               fmi2String fmuResourceLocation,
                               const fmi2CallbackFunctions* functions,
                               fmi2Boolean visible,
                               fmi2Boolean loggingOn)
    : FmuComponentBase(
          instanceName,
          fmuType,
          fmuGUID,
          fmuResourceLocation,
          functions,
          visible,
          loggingOn,
          {{"logEvents", true},
           {"logSingularLinearSystems", true},
           {"logNonlinearSystems", true},
           {"logStatusWarning", true},
           {"logStatusError", true},
           {"logStatusPending", true},
           {"logDynamicStateSelection", true},
           {"logStatusDiscard", true},
           {"logStatusFatal", true},
           {"logAll", true}},
          {"logStatusWarning", "logStatusDiscard", "logStatusError", "logStatusFatal", "logStatusPending"}),
      x_dd(0),
      theta_dd(0) {
    initializeType(fmuType);

    // Define new units if needed
    UnitDefinition UD_J("J");
    UD_J.kg = 1;
    UD_J.m = 2;
    UD_J.s = -2;
    addUnitDefinition(UD_J);

    // Set initial conditions for underlying ODE
    q = {0, 3.14159265358979323846 / 4, 0, 0};

    // Declare relevant variables
    auto& fmu_len = AddFmuVariable(&len, "len", FmuVariable::Type::Real, "m", "pendulum length",
                                   FmuVariable::CausalityType::parameter, FmuVariable::VariabilityType::fixed);
    auto& fmu_m = AddFmuVariable(&m, "m", FmuVariable::Type::Real, "kg", "pendulum mass",
                                 FmuVariable::CausalityType::parameter, FmuVariable::VariabilityType::fixed);
    auto& fmu_M = AddFmuVariable(&M, "M", FmuVariable::Type::Real, "kg", "cart mass",
                                 FmuVariable::CausalityType::parameter, FmuVariable::VariabilityType::fixed);

    AddFmuVariable(&approximateOn, "approximateOn", FmuVariable::Type::Boolean, "1", "use approximated model",
                   FmuVariable::CausalityType::parameter, FmuVariable::VariabilityType::fixed);

    AddFmuVariable(&q[0], "x", FmuVariable::Type::Real, "m", "cart position",                       //
                   FmuVariable::CausalityType::output, FmuVariable::VariabilityType::continuous,    //
                   FmuVariable::InitialType::exact);                                                //
    AddFmuVariable(&q[2], "der(x)", FmuVariable::Type::Real, "m/s", "derivative of cart position",  //
                   FmuVariable::CausalityType::local, FmuVariable::VariabilityType::continuous,     //
                   FmuVariable::InitialType::calculated);                                           //

    AddFmuVariable(&q[1], "theta", FmuVariable::Type::Real, "rad", "pendulum angle",                       //
                   FmuVariable::CausalityType::output, FmuVariable::VariabilityType::continuous,           //
                   FmuVariable::InitialType::exact);                                                       //
    AddFmuVariable(&q[3], "der(theta)", FmuVariable::Type::Real, "rad/s", "derivative of pendulum angle",  //
                   FmuVariable::CausalityType::local, FmuVariable::VariabilityType::continuous,            //
                   FmuVariable::InitialType::calculated);                                                  //

    AddFmuVariable(&q[2], "v", FmuVariable::Type::Real, "m/s", "cart velocity",                   //
                   FmuVariable::CausalityType::output, FmuVariable::VariabilityType::continuous,  //
                   FmuVariable::InitialType::exact);                                              //
    AddFmuVariable(&x_dd, "der(v)", FmuVariable::Type::Real, "m/s2", "cart linear acceleration",  //
                   FmuVariable::CausalityType::local, FmuVariable::VariabilityType::continuous,   //
                   FmuVariable::InitialType::calculated);                                         //

    AddFmuVariable(&q[3], "omg", FmuVariable::Type::Real, "rad/s", "pendulum angular velocity",                //
                   FmuVariable::CausalityType::output, FmuVariable::VariabilityType::continuous,               //
                   FmuVariable::InitialType::exact);                                                           //
    AddFmuVariable(&theta_dd, "der(omg)", FmuVariable::Type::Real, "rad/s2", "pendulum angular acceleration",  //
                   FmuVariable::CausalityType::local, FmuVariable::VariabilityType::continuous,                //
                   FmuVariable::InitialType::calculated);                                                      //

    /// One can also pass std::functions to get/set the value of the variable if queried
    // AddFmuVariable(std::make_pair(
    //     std::function<fmi2Real()>([this]() -> fmi2Real {
    //         return (0.5*(this->m*this->len*this->len/3)*(this->q_t[2]*this->q_t[2]));}),
    //     std::function<void(fmi2Real)>([this](fmi2Real) {})),
    //     "kineticEnergy",    FmuVariable::Type::Real, "J",    "kinetic energy");

    // same result is achieved through helper function 'MAKE_GETSET_PAIR'
    AddFmuVariable(
        MAKE_GETSET_PAIR(fmi2Real,
                         { return (0.5 * (this->m * this->len * this->len / 3) * (this->theta_dd * this->theta_dd)); }, {}),
        "kineticEnergy", FmuVariable::Type::Real, "J", "kinetic energy");

    // Set name of file expected to be present in the FMU resources directory
    filename = "myData.txt";
    AddFmuVariable(&filename, "filename", FmuVariable::Type::String, "kg", "additional mass on cart",  //
                   FmuVariable::CausalityType::parameter, FmuVariable::VariabilityType::fixed);        //

    // Specify state derivatives
    DeclareStateDerivative("der(x)", "x", {"v"});
    DeclareStateDerivative("der(theta)", "theta", {"omg"});
    DeclareStateDerivative("der(v)", "v", {"theta", "omg", "len", "m", "M"});
    DeclareStateDerivative("der(omg)", "omg", {"theta", "omg", "len", "m", "M"});

    // Variable dependencies must be specified for:
    // - variables with causality 'output' for which 'initial' is 'approx' or 'calculated'
    // - variables with causality 'calculatedParameter'
    DeclareVariableDependencies("der(x)", {"v"});
    DeclareVariableDependencies("der(theta)", {"omg"});
    DeclareVariableDependencies("der(v)", {"theta", "omg", "len", "m", "M"});
    DeclareVariableDependencies("der(omg)", {"theta", "omg", "len", "m", "M"});

    // Specify functions to calculate FMU outputs (at end of step)
    AddPostStepFunction([this]() { this->calcAccelerations(); });

    // Log location of resources directory
    sendToLog("Resources directory location: " + std::string(fmuResourceLocation) + ".\n", fmi2Status::fmi2OK, "logAll");
}

// -----------------------------------------------------------------------------

// A function added as a post-step callback can be used to prepare (post-process)
// other output or local FMI variables (here, the cart and pendulum accelerations)
void myFmuComponent::calcAccelerations() {
    x_dd = calcX_dd(q[1], q[3]);
    theta_dd = calcTheta_dd(q[1], q[3]);
}

// -----------------------------------------------------------------------------

std::array<double, 4> operator*(double a, const std::array<double, 4>& v) {
    std::array<double, 4> temp;
    temp[0] = v[0] * a;
    temp[1] = v[1] * a;
    temp[2] = v[2] * a;
    temp[3] = v[3] * a;
    return temp;
}

std::array<double, 4> operator+(const std::array<double, 4>& a, const std::array<double, 4>& b) {
    std::array<double, 4> temp;
    temp[0] = a[0] + b[0];
    temp[1] = a[1] + b[1];
    temp[2] = a[2] + b[2];
    temp[3] = a[3] + b[3];
    return temp;
}

double myFmuComponent::calcX_dd(double theta, double theta_d) {
    if (approximateOn)
        return (m * theta * (len * theta_d * theta_d + g)) / M;

    double s = std::sin(theta);
    double c = std::cos(theta);
    return (m * s * (len * theta_d * theta_d + g * c)) / (M + m * s * s);
}

double myFmuComponent::calcTheta_dd(double theta, double theta_d) {
    if (approximateOn)
        return -(theta * (len * m * theta_d * theta_d + M * g + g * m)) / (len * M);

    double s = std::sin(theta);
    double c = std::cos(theta);
    return -(s * (len * m * c * theta_d * theta_d + M * g + g * m)) / (len * (M + m * s * s));
}

myFmuComponent::vec4 myFmuComponent::calcRHS(double t, const vec4& q) {
    vec4 rhs;
    rhs[0] = q[2];
    rhs[1] = q[3];
    rhs[2] = calcX_dd(q[1], q[3]);
    rhs[3] = calcTheta_dd(q[1], q[3]);
    return rhs;
}

// -----------------------------------------------------------------------------

fmi2Status myFmuComponent::enterInitializationModeIMPL() {
    return fmi2Status::fmi2OK;
}

fmi2Status myFmuComponent::exitInitializationModeIMPL() {
    std::string myfile_location = m_resources_location + "/" + filename;
    std::ifstream resfile(myfile_location);

    if (resfile.good()) {
        sendToLog("Successfully opened required file: " + myfile_location + ".\n", fmi2Status::fmi2OK, "logAll");
    } else {
        sendToLog("Unable to open required file: " + myfile_location + "; check if 'resources' folder is set",
                  fmi2Status::fmi2Fatal, "logStatusFatal");
        throw std::runtime_error("Unable to open required file: " + myfile_location);
    }

    double additional_mass = 0;
    if (resfile >> additional_mass) {
        M += additional_mass;
    } else {
        throw std::runtime_error("Expected number in: " + myfile_location);
    }

    resfile.close();
    sendToLog("Loaded additional cart mass " + std::to_string(additional_mass) + " from " + filename + ".\n",
              fmi2Status::fmi2OK, "logAll");

    return fmi2Status::fmi2OK;
}

fmi2Status myFmuComponent::getContinuousStatesIMPL(fmi2Real x[], size_t nx) {
    for (size_t i = 0; i < nx; i++) {
        x[i] = q[i];
    }

    return fmi2Status::fmi2OK;
}

fmi2Status myFmuComponent::setContinuousStatesIMPL(const fmi2Real x[], size_t nx) {
    for (size_t i = 0; i < nx; i++) {
        q[i] = x[i];
    }

    return fmi2Status::fmi2OK;
}

fmi2Status myFmuComponent::getDerivativesIMPL(fmi2Real derivatives[], size_t nx) {
    auto rhs = calcRHS(m_time, q);

    for (size_t i = 0; i < nx; i++) {
        derivatives[i] = rhs[i];
    }

    return fmi2Status::fmi2OK;
}
