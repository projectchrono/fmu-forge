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
// Example FMU for model exchange (FMI 3.0 standard)
// Illustrates the FMU exporting capabilities in fmu-forge (FmuToolsExport.h)
// =============================================================================

#define NOMINMAX

#include <cassert>
#include <map>
#include <algorithm>

#define _USE_MATH_DEFINES
#include <math.h>

// #define FMI3_FUNCTION_PREFIX MyModel_
#include "myFmuModelExchange_fmi3.h"

using namespace fmu_forge;
using namespace fmu_forge::fmi3;

// -----------------------------------------------------------------------------

// Implement function declared in FmuToolsExport.h to create an instance of this FMU.
FmuComponentBase* fmi3::fmi3InstantiateIMPL(FmuType fmiInterfaceType,
                                            fmi3String instanceName,
                                            fmi3String instantiationToken,
                                            fmi3String resourcePath,
                                            fmi3Boolean visible,
                                            fmi3Boolean loggingOn,
                                            fmi3InstanceEnvironment instanceEnvironment,
                                            fmi3LogMessageCallback logMessage) {
    return new myFmuComponent(fmiInterfaceType, instanceName, instantiationToken, resourcePath, visible, loggingOn,
                              instanceEnvironment, logMessage);
}

// -----------------------------------------------------------------------------

// During construction of the FMU component:
// - list the log categories that this FMU should handle
// - specify a flag to enable/disable logging
// - specify which log categories are to be considered as debug
myFmuComponent::myFmuComponent(FmuType fmiInterfaceType,
                               fmi3String instanceName,
                               fmi3String instantiationToken,
                               fmi3String resourcePath,
                               fmi3Boolean visible,
                               fmi3Boolean loggingOn,
                               fmi3InstanceEnvironment instanceEnvironment,
                               fmi3LogMessageCallback logMessage)
    : FmuComponentBase(
          fmiInterfaceType,
          instanceName,
          instantiationToken,
          resourcePath,
          visible,
          loggingOn,
          instanceEnvironment,
          logMessage,
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
    initializeType(fmiInterfaceType);

    // Define new units if needed
    UnitDefinition UD_J("J");
    UD_J.kg = 1;
    UD_J.m = 2;
    UD_J.s = -2;
    addUnitDefinition(UD_J);

    // Set initial conditions for underlying ODE
    q = {0, 3.14159265358979323846 / 4, 0, 0};

    // Declare relevant variables
    auto& fmu_len = AddFmuVariable(&len, "len", FmuVariable::Type::Float64, "m", "pendulum length",
                                   FmuVariable::CausalityType::parameter, FmuVariable::VariabilityType::fixed);
    auto& fmu_m = AddFmuVariable(&m, "m", FmuVariable::Type::Float64, "kg", "pendulum mass",
                                 FmuVariable::CausalityType::parameter, FmuVariable::VariabilityType::fixed);
    auto& fmu_M = AddFmuVariable(&M, "M", FmuVariable::Type::Float64, "kg", "cart mass",
                                 FmuVariable::CausalityType::parameter, FmuVariable::VariabilityType::fixed);

    AddFmuVariable(&approximateOn, "approximateOn", FmuVariable::Type::Boolean, "1", "use approximated model",
                   FmuVariable::CausalityType::parameter, FmuVariable::VariabilityType::fixed);

    AddFmuVariable(&q[0], "x", FmuVariable::Type::Float64, "m", "cart position",                       //
                   FmuVariable::CausalityType::output, FmuVariable::VariabilityType::continuous,       //
                   FmuVariable::InitialType::exact);                                                   //
    AddFmuVariable(&q[2], "der(x)", FmuVariable::Type::Float64, "m/s", "derivative of cart position",  //
                   FmuVariable::CausalityType::local, FmuVariable::VariabilityType::continuous,        //
                   FmuVariable::InitialType::calculated);                                              //

    AddFmuVariable(&q[1], "theta", FmuVariable::Type::Float64, "rad", "pendulum angle",                       //
                   FmuVariable::CausalityType::output, FmuVariable::VariabilityType::continuous,              //
                   FmuVariable::InitialType::exact);                                                          //
    AddFmuVariable(&q[3], "der(theta)", FmuVariable::Type::Float64, "rad/s", "derivative of pendulum angle",  //
                   FmuVariable::CausalityType::local, FmuVariable::VariabilityType::continuous,               //
                   FmuVariable::InitialType::calculated);                                                     //

    AddFmuVariable(&q[2], "v", FmuVariable::Type::Float64, "m/s", "cart velocity",                   //
                   FmuVariable::CausalityType::output, FmuVariable::VariabilityType::continuous,     //
                   FmuVariable::InitialType::exact);                                                 //
    AddFmuVariable(&x_dd, "der(v)", FmuVariable::Type::Float64, "m/s2", "cart linear acceleration",  //
                   FmuVariable::CausalityType::local, FmuVariable::VariabilityType::continuous,      //
                   FmuVariable::InitialType::calculated);                                            //

    AddFmuVariable(&q[3], "omg", FmuVariable::Type::Float64, "rad/s", "pendulum angular velocity",                //
                   FmuVariable::CausalityType::output, FmuVariable::VariabilityType::continuous,                  //
                   FmuVariable::InitialType::exact);                                                              //
    AddFmuVariable(&theta_dd, "der(omg)", FmuVariable::Type::Float64, "rad/s2", "pendulum angular acceleration",  //
                   FmuVariable::CausalityType::local, FmuVariable::VariabilityType::continuous,                   //
                   FmuVariable::InitialType::calculated);                                                         //

    /// One can also pass std::functions to get/set the value of the variable if queried
    // AddFmuVariable(std::make_pair(
    //     std::function<fmi3Float64()>([this]() -> fmi3Float64 {
    //         return (0.5*(this->m*this->len*this->len/3)*(this->q_t[2]*this->q_t[2]));}),
    //     std::function<void(Float64)>([this](fmi3Float64) {})),
    //     "kineticEnergy",    FmuVariable::Type::Float64, "J",    "kinetic energy");

    // same result is achieved through helper function 'MAKE_GETSET_PAIR'
    AddFmuVariable(
        MAKE_GETSET_PAIR(fmi3Float64,
                         { return (0.5 * (this->m * this->len * this->len / 3) * (this->theta_dd * this->theta_dd)); },
                         {}),
        "kineticEnergy", FmuVariable::Type::Float64, "J", "kinetic energy");

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
    sendToLog("Resources directory location: " + std::string(resourcePath) + ".\n", fmi3Status::fmi3OK, "logAll");
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

fmi3Status myFmuComponent::enterInitializationModeIMPL() {
    return fmi3Status::fmi3OK;
}

fmi3Status myFmuComponent::exitInitializationModeIMPL() {
    std::string myfile_location = m_resources_location + "/" + filename;
    std::ifstream resfile(myfile_location);

    if (resfile.good()) {
        sendToLog("Successfully opened required file: " + myfile_location + ".\n", fmi3Status::fmi3OK, "logAll");
    } else {
        sendToLog("Unable to open required file: " + myfile_location + "; check if 'resources' folder is set",
                  fmi3Status::fmi3Fatal, "logStatusFatal");
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
              fmi3Status::fmi3OK, "logAll");

    return fmi3Status::fmi3OK;
}

fmi3Status myFmuComponent::getContinuousStatesIMPL(fmi3Float64 continuousStates[], size_t nContinuousStates) {
    for (size_t i = 0; i < nContinuousStates; i++) {
        continuousStates[i] = q[i];
    }

    return fmi3Status::fmi3OK;
}

fmi3Status myFmuComponent::setContinuousStatesIMPL(const fmi3Float64 continuousStates[], size_t nContinuousStates) {
    for (size_t i = 0; i < nContinuousStates; i++) {
        q[i] = continuousStates[i];
    }

    return fmi3Status::fmi3OK;
}

fmi3Status myFmuComponent::getDerivativesIMPL(fmi3Float64 derivatives[], size_t nContinuousStates) {
    auto rhs = calcRHS(m_time, q);

    for (size_t i = 0; i < nContinuousStates; i++) {
        derivatives[i] = rhs[i];
    }

    return fmi3Status::fmi3OK;
}
