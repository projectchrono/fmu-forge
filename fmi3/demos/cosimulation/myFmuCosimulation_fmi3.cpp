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
// Example FMU for co-simulation (FMI 3.0 standard)
// Illustrates the FMU exporting capabilities in fmu-forge (FmuToolsExport.h)
// =============================================================================

#define NOMINMAX

#include <cassert>
#include <map>
#include <algorithm>

#define _USE_MATH_DEFINES
#include <math.h>

// #define FMI3_FUNCTION_PREFIX MyModel_
#include "myFmuCosimulation_fmi3.h"

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
      theta_dd(0),
      A{{0.0, 0.1}, {1.0, 1.1}, {2.0, 2.1}},
      binvar{0xF, 0xE, 0xD, 0xC, 0xB, 0xA},
      binvararray{{0x1, 0x1, 0x1, 0xF}, {0x1, 0x1, 0xF, 0x1}, {0x1, 0xF, 0x1, 0x1}},
      stringvar("hello"),
      stringarrayinput{"arrivederci", "au_revoir"} {
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

    AddFmuVariable(&q[0], "x", FmuVariable::Type::Float64, "m", "cart position",                    //
                   FmuVariable::CausalityType::output, FmuVariable::VariabilityType::continuous,    //
                   FmuVariable::InitialType::exact);                                                //
    AddFmuVariable(&q[1], "theta", FmuVariable::Type::Float64, "rad", "pendulum angle",             //
                   FmuVariable::CausalityType::output, FmuVariable::VariabilityType::continuous,    //
                   FmuVariable::InitialType::exact);                                                //
    AddFmuVariable(&q[2], "x_t", FmuVariable::Type::Float64, "m/s", "cart velocity",                //
                   FmuVariable::CausalityType::output, FmuVariable::VariabilityType::continuous,    //
                   FmuVariable::InitialType::exact);                                                //
    AddFmuVariable(&q[3], "theta_t", FmuVariable::Type::Float64, "rad/s", "pendulum ang velocity",  //
                   FmuVariable::CausalityType::output, FmuVariable::VariabilityType::continuous,    //
                   FmuVariable::InitialType::exact);                                                //

    AddFmuVariable(&x_dd, "x_tt", FmuVariable::Type::Float64, "m/s2", "cart linear acceleration",
                   FmuVariable::CausalityType::output, FmuVariable::VariabilityType::continuous,
                   FmuVariable::InitialType::calculated);
    AddFmuVariable(&theta_dd, "theta_tt", FmuVariable::Type::Float64, "rad/s2", "pendulum angular acceleration",
                   FmuVariable::CausalityType::output, FmuVariable::VariabilityType::continuous,
                   FmuVariable::InitialType::calculated);

    AddFmuVariable(*A, "A", FmuVariable::Type::Float64, {{3, true}, {2, true}}, "1", "generic matrix",
                   FmuVariable::CausalityType::output, FmuVariable::VariabilityType::continuous,
                   FmuVariable::InitialType::exact);

    AddFmuVariable(&stringvar, "stringvar", FmuVariable::Type::String, "1", "string variable",
                   FmuVariable::CausalityType::parameter, FmuVariable::VariabilityType::fixed,
                   FmuVariable::InitialType::exact);

    AddFmuVariable(&stringarrayinput[0], "stringarrayinput", FmuVariable::Type::String,
                   {{stringarrayinput.size(), true}}, "1", "string array input", FmuVariable::CausalityType::parameter,
                   FmuVariable::VariabilityType::fixed, FmuVariable::InitialType::exact);

    AddFmuVariable(&binvar, "binvar", FmuVariable::Type::Binary, "1", "generic binary",
                   FmuVariable::CausalityType::parameter, FmuVariable::VariabilityType::fixed,
                   FmuVariable::InitialType::exact);

    AddFmuVariable(binvararray.data(), "binvararray", FmuVariable::Type::Binary, {{binvararray.size(), true}}, "1",
                   "generic binary array", FmuVariable::CausalityType::parameter, FmuVariable::VariabilityType::fixed,
                   FmuVariable::InitialType::exact);

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

    auto& fmu_approximateOn =
        AddFmuVariable(&approximateOn, "approximateOn", FmuVariable::Type::Boolean, "1", "use approximated model",
                       FmuVariable::CausalityType::parameter, FmuVariable::VariabilityType::fixed);

    // Set name of file expected to be present in the FMU resources directory
    filename = "myData.txt";
    AddFmuVariable(&filename, "filename", FmuVariable::Type::String, "1", "additional mass on cart",  //
                   FmuVariable::CausalityType::parameter, FmuVariable::VariabilityType::fixed);       //

    // Variable dependencies must be specified for:
    // - variables with causality 'output' for which 'initial' is 'approx' or 'calculated'
    // - variables with causality 'calculatedParameter'
    DeclareVariableDependencies("x_tt", {"len", "m", "M"});
    DeclareVariableDependencies("theta_tt", {"len", "m", "M"});

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

fmi3Status myFmuComponent::doStepIMPL(fmi3Float64 currentCommunicationPoint,
                                      fmi3Float64 communicationStepSize,
                                      fmi3Boolean noSetFMUStatePriorToCurrentPoint,
                                      fmi3Boolean* eventHandlingNeeded,
                                      fmi3Boolean* terminateSimulation,
                                      fmi3Boolean* earlyReturn,
                                      fmi3Float64* lastSuccessfulTime) {
    while (m_time < currentCommunicationPoint + communicationStepSize) {
        fmi3Float64 h = std::min((currentCommunicationPoint + communicationStepSize - m_time),
                                 std::min(communicationStepSize, m_stepSize));

        auto k1 = calcRHS(m_time, q);
        auto k2 = calcRHS(m_time + h / 2, q + 0.5 * h * k1);
        auto k3 = calcRHS(m_time + h / 2, q + 0.5 * h * k2);
        auto k4 = calcRHS(m_time + h, q + h * k3);

        q = q + (h / 6) * (k1 + 2.0 * k2 + 2.0 * k3 + k4);

        m_time = m_time + h;

        sendToLog("Step at time: " + std::to_string(m_time) + " succeeded.\n", fmi3Status::fmi3OK, "logAll");
    }

    *eventHandlingNeeded = fmi3False;
    *terminateSimulation = fmi3False;
    *earlyReturn = fmi3False;

    *lastSuccessfulTime = m_time;

    return fmi3Status::fmi3OK;
}
