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
// Classes for exporting FMUs (FMI 2.0)
// =============================================================================

#ifndef FMUTOOLS_FMI2_EXPORT_H
#define FMUTOOLS_FMI2_EXPORT_H

#include <algorithm>
#include <cassert>
#include <vector>
#include <array>
#include <map>
#include <iostream>
#include <string>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <list>

#include "FmuToolsUnitDefinitions.h"
#include "fmi2/FmuToolsVariable.h"
#include "fmi2/fmi2_headers/fmi2Functions.h"

#ifndef FMITYPESPLATFORM_CUSTOM
    #include "fmi2/TypesVariantsDefault.h"
#else
    #include "fmi2/TypesVariantsCustom.h"
#endif

// =============================================================================

namespace fmu_forge {
namespace fmi2 {

/// @addtogroup fmu_forge_fmi2
/// @{

#ifdef __cplusplus
extern "C" {
#endif

bool FMI2_Export createModelDescription(const std::string& path, std::string& err_msg);

#ifdef __cplusplus
}
#endif

// =============================================================================

#define MAKE_GETSET_PAIR(returnType, codeGet, codeSet)                          \
    std::make_pair(std::function<returnType()>([this]() -> returnType codeGet), \
                   std::function<void(returnType)>([this](returnType val) codeSet))

template <class T>
using FunGetSet = std::pair<std::function<T(void)>, std::function<void(T)>>;

bool is_pointer_variant(const FmuVariableBindType& myVariant);

// =============================================================================

/// Implementation of an FMU variable for export (generation of model description XML).
class FmuVariableExport : public FmuVariable {
  public:
    using VarbindType = FmuVariableBindType;
    using StartType = FmuVariableStartType;

    FmuVariableExport(const VarbindType& varbind,
                      const std::string& name,
                      FmuVariable::Type type,
                      CausalityType causality = CausalityType::local,
                      VariabilityType variability = VariabilityType::continuous,
                      InitialType initial = InitialType::none);

    FmuVariableExport(const FmuVariableExport& other);

    /// Copy assignment operator.
    FmuVariableExport& operator=(const FmuVariableExport& other);

    void Bind(VarbindType varbind) { m_varbind = varbind; }

    /// Set the value of this FMU variable (for all cases, except variables of type fmi2String).
    template <typename T, typename = typename std::enable_if<!std::is_same<T, fmi2String>::value>::type>
    void SetValue(const T& val) const {
        if (is_pointer_variant(m_varbind))
            *varns::get<T*>(m_varbind) = val;
        else
            varns::get<FunGetSet<T>>(m_varbind).second(val);
    }

    /// Set the value of this FMU variable of type fmi2String.
    void SetValue(const fmi2String& val) const;

    /// Get the value of this FMU variable (for all cases, except variables of type fmi2String).
    template <typename T, typename = typename std::enable_if<!std::is_same<T, fmi2String>::value>::type>
    void GetValue(T* varptr) const {
        *varptr =
            is_pointer_variant(m_varbind) ? *varns::get<T*>(m_varbind) : varns::get<FunGetSet<T>>(m_varbind).first();
    }

    /// Get the value of this FMU variable of type fmi2String.
    void GetValue(fmi2String* varptr) const;

    /// Set the start value for this FMU variable (for all cases, except variables of type fmi2String).
    template <typename T, typename = typename std::enable_if<!std::is_same<T, fmi2String>::value>::type>
    void SetStartVal(T start) {
        if (!m_allowed_start)
            return;
        m_has_start = true;
        m_start = start;
    }

    /// Set the start value for this FMU variable of type fmi2String.
    void SetStartVal(fmi2String start);

    void ExposeCurrentValueAsStart();

  protected:
    bool m_allowed_start = true;    ///< specification of start value permitted
    bool m_required_start = false;  ///< specification of start value required

    VarbindType m_varbind;  ///< value of this variable
    StartType m_start;      ///< start value for this variable

    //// TODO: in C++17 should be possible to either use constexpr or use lambda with 'overload' keyword
    template <typename T>
    void setStartFromVar(FunGetSet<T> funPair);

    template <typename T>
    void setStartFromVar(T* var_ptr);

    std::string GetStartValAsString() const;

    friend class FmuComponentBase;
};

// -----------------------------------------------------------------------------

template <typename T>
void FmuVariableExport::setStartFromVar(FunGetSet<T> funPair) {
    if (m_allowed_start)
        m_has_start = true;
    else
        return;

    m_start = funPair.first();
}

template <typename T>
void FmuVariableExport::setStartFromVar(T* var_ptr) {
    if (m_allowed_start)
        m_has_start = true;
    else
        return;

    m_start = *var_ptr;
}

// =============================================================================

/// Base class for an FMU component (used for export).
/// (1) This class provides support for:
/// - defining FMU variables (cauality, variability, start value, etc)
/// - defining FMU model structure (outputs, derivatives, variable dependencies, etc)
/// (2) This class defines the virtual methods that an FMU must implement
class FmuComponentBase {
  public:
    FmuComponentBase(fmi2String instanceName,
                     fmi2Type fmuType,
                     fmi2String fmuGUID,
                     fmi2String fmuResourceLocation,
                     const fmi2CallbackFunctions* functions,
                     fmi2Boolean visible,
                     fmi2Boolean loggingOn,
                     const std::unordered_map<std::string, bool>& logCategories_init,
                     const std::unordered_set<std::string>& logCategories_debug_init);

    virtual ~FmuComponentBase() {}

    const std::set<FmuVariableExport>& GetScalarVariables() const { return m_variables; }

    /// Enable/disable the logging for a specific log category.
    virtual void SetDebugLogging(std::string cat, bool val);

    /// Create the modelDescription.xml file in the given location \a path.
    void ExportModelDescription(std::string path);

    double GetTime() const { return m_time; }

    template <class T>
    fmi2Status fmi2GetVariable(const fmi2ValueReference vr[], size_t nvr, T value[], FmuVariable::Type vartype) {
        //// TODO
        //// when multiple variables are requested it might be better to iterate through scalarVariables just once
        //// and check if they match any of the nvr requested variables
        for (size_t s = 0; s < nvr; ++s) {
            auto it = this->findByValrefType(vr[s], vartype);

            if (it == this->m_variables.end()) {
                // requested a variable that does not exist
                auto msg = "fmi2GetVariable: variable of type " + FmuVariable::Type_toString(vartype) +
                           " with value reference " + std::to_string(vr[s]) + " does NOT exist.\n";
                sendToLog(msg, fmi2Status::fmi2Error, "logStatusError");
                return fmi2Status::fmi2Error;
            } else {
                it->GetValue(&value[s]);
            }
        }

        return fmi2Status::fmi2OK;
    }

    template <class T>
    fmi2Status fmi2SetVariable(const fmi2ValueReference vr[], size_t nvr, const T value[], FmuVariable::Type vartype) {
        for (size_t s = 0; s < nvr; ++s) {
            std::set<FmuVariableExport>::iterator it = this->findByValrefType(vr[s], vartype);

            if (it == this->m_variables.end()) {
                // requested a variable that does not exist
                auto msg = "fmi2SetVariable: variable of type " + FmuVariable::Type_toString(vartype) +
                           " with value reference " + std::to_string(vr[s]) + " does NOT exist.\n";
                sendToLog(msg, fmi2Status::fmi2Error, "logStatusError");
                return fmi2Status::fmi2Error;
            } else if (!it->IsSetAllowed(this->m_fmuMachineState)) {
                // requested variable cannot be set in the current FMU state
                auto msg = "fmi2SetVariable: variable of type " + FmuVariable::Type_toString(vartype) +
                           " with value reference " + std::to_string(vr[s]) +
                           " NOT ALLOWED to be set in current state.\n";
                sendToLog(msg, fmi2Status::fmi2Error, "logStatusError");
                return fmi2Status::fmi2Error;
            } else {
                it->SetValue(value[s]);
            }
        }

        return fmi2Status::fmi2OK;
    }

    /// Adds a variable to the list of variables of the FMU.
    /// The start value is automatically grabbed from the variable itself.
    const FmuVariableExport& AddFmuVariable(
        const FmuVariableExport::VarbindType& varbind,
        std::string name,
        FmuVariable::Type scalartype = FmuVariable::Type::Real,
        std::string unitname = "",
        std::string description = "",
        FmuVariable::CausalityType causality = FmuVariable::CausalityType::local,
        FmuVariable::VariabilityType variability = FmuVariable::VariabilityType::continuous,
        FmuVariable::InitialType initial = FmuVariable::InitialType::none);

    /// Declare a state derivative variables, specifying the corresponding state and dependencies on other variables.
    /// Calls to this function must be made *after* all FMU variables were defined.
    void DeclareStateDerivative(const std::string& derivative_name,
                                const std::string& state_name,
                                const std::vector<std::string>& dependency_names);

    /// Declare variable dependencies.
    /// Calls to this function must be made *after* all FMU variables were defined.
    void DeclareVariableDependencies(const std::string& variable_name,
                                     const std::vector<std::string>& dependency_names);

    bool RebindVariable(FmuVariableExport::VarbindType varbind, std::string name);

    /// Add a function to be executed before doStep (co-simulation FMU) or before getDerivatives (model exchange FMU).
    /// Such functions can be used to implement FMU-specific pre-processing of input variables.
    void AddPreStepFunction(std::function<void(void)> function) { m_preStepCallbacks.push_back(function); }

    /// Add a function to be executed after doStep (co-simulation FMU) or after getDerivatives (model exchange FMU).
    /// Such functions can be used to implement FMU-specific post-processing to prepare output variables.
    void AddPostStepFunction(std::function<void(void)> function) { m_postStepCallbacks.push_back(function); }

  protected:
    // This section declares the virtual methods that a concrete FMU must implement.
    // - some of these functions have a default implementation
    // - some are required for all FMUs (pure virtual)
    // - others are required only for specific FMU types (e.g., doStepIMPL must be implemented for CS, but not for ME)

    virtual bool is_cosimulation_available() const = 0;
    virtual bool is_modelexchange_available() const = 0;

    virtual void preModelDescriptionExport() {}
    virtual void postModelDescriptionExport() {}

    virtual fmi2Status doStepIMPL(fmi2Real currentCommunicationPoint,
                                  fmi2Real communicationStepSize,
                                  fmi2Boolean noSetFMUStatePriorToCurrentPoint) {
        if (is_cosimulation_available())
            throw std::runtime_error("An FMU for co-simulation must implement doStepIMPL");

        return fmi2OK;
    }

    virtual fmi2Status newDiscreteStatesIMPL(fmi2EventInfo* fmi2eventInfo) {
        fmi2eventInfo->newDiscreteStatesNeeded = fmi2False;
        fmi2eventInfo->terminateSimulation = fmi2False;
        fmi2eventInfo->nominalsOfContinuousStatesChanged = fmi2False;
        fmi2eventInfo->valuesOfContinuousStatesChanged = fmi2False;
        fmi2eventInfo->nextEventTimeDefined = fmi2False;
        fmi2eventInfo->nextEventTime = 0;

        return fmi2Status::fmi2OK;
    }

    virtual fmi2Status completedIntegratorStepIMPL(fmi2Boolean noSetFMUStatePriorToCurrentPoint,
                                                   fmi2Boolean* enterEventMode,
                                                   fmi2Boolean* terminateSimulation) {
        *enterEventMode = fmi2False;
        *terminateSimulation = fmi2False;

        return fmi2Status::fmi2OK;
    }

    virtual fmi2Status setTimeIMPL(fmi2Real time) { return fmi2Status::fmi2OK; }

    virtual fmi2Status getContinuousStatesIMPL(fmi2Real x[], size_t nx) {
        if (is_modelexchange_available())
            throw std::runtime_error("An FMU for model exchange must implement getContinuousStatesIMPL");

        return fmi2OK;
    }

    virtual fmi2Status setContinuousStatesIMPL(const fmi2Real x[], size_t nx) {
        if (is_modelexchange_available())
            throw std::runtime_error("An FMU for model exchange must implement setContinuousStatesIMPL");

        return fmi2OK;
    }

    virtual fmi2Status getDerivativesIMPL(fmi2Real derivatives[], size_t nx) {
        if (is_modelexchange_available())
            throw std::runtime_error("An FMU for model exchange must implement getDerivativesIMPL");

        return fmi2OK;
    }

    virtual fmi2Status enterInitializationModeIMPL() { return fmi2Status::fmi2OK; }
    virtual fmi2Status exitInitializationModeIMPL() { return fmi2Status::fmi2OK; }

  public:
    void SetDefaultExperiment(fmi2Boolean toleranceDefined,
                              fmi2Real tolerance,
                              fmi2Real startTime,
                              fmi2Boolean stopTimeDefined,
                              fmi2Real stopTime);

    fmi2Status EnterInitializationMode();

    fmi2Status ExitInitializationMode();

    // Co-Simulation FMI functions.
    // These functions are used to implement the actual co-simulation functions imposed by the FMI2 standard.
    // In turn, they call overrides of virtual methods provided by a concrete FMU.

    fmi2Status DoStep(fmi2Real currentCommunicationPoint,
                      fmi2Real communicationStepSize,
                      fmi2Boolean noSetFMUStatePriorToCurrentPoint);

    // Model Exchange FMI functions.
    // These functions are used to implement the actual model exchange functions imposed by the FMI2 standard.
    // In turn, they call overrides of virtual methods provided by a concrete FMU.

    fmi2Status NewDiscreteStates(fmi2EventInfo* fmi2eventInfo);
    fmi2Status CompletedIntegratorStep(fmi2Boolean noSetFMUStatePriorToCurrentPoint,
                                       fmi2Boolean* enterEventMode,
                                       fmi2Boolean* terminateSimulation);
    fmi2Status SetTime(const fmi2Real time);
    fmi2Status GetContinuousStates(fmi2Real x[], size_t nx);
    fmi2Status SetContinuousStates(const fmi2Real x[], size_t nx);
    fmi2Status GetDerivatives(fmi2Real derivatives[], size_t nx);

  protected:
    /// Add a declaration of a state derivative.
    virtual void addDerivative(const std::string& derivative_name,
                               const std::string& state_name,
                               const std::vector<std::string>& dependency_names);

    /// Check if the variable with specified name is a state derivative.
    /// If true, return the name of the corresponding state variable. Otherwise, return an empty string.
    std::string isDerivative(const std::string& name);

    /// Add a declaration of dependency of "variable_name" on the variables in the "dependency_names" list.
    virtual void addDependencies(const std::string& variable_name, const std::vector<std::string>& dependency_names);

    void initializeType(fmi2Type fmuType);

    void addUnitDefinition(const UnitDefinition& unit_definition);

    void clearUnitDefinitions() { m_unitDefinitions.clear(); }

    /// Send message to the logger function.
    /// The message will be sent if at least one of the following applies:
    /// - \a msg_cat has been enabled by `SetDebugLogging(msg_cat, true)`
    /// - the FMU has been instantiated with `loggingOn` set as `fmi2True` and \a msg_cat has been labelled as a
    /// debugging category;
    /// FMUs generated by this library provides a Description in which it is reported if the category
    /// is of debug.
    void sendToLog(std::string msg, fmi2Status status, std::string msg_cat);

    std::set<FmuVariableExport>::iterator findByValrefType(fmi2ValueReference vr, FmuVariable::Type vartype);
    std::set<FmuVariableExport>::iterator findByName(const std::string& name);

    void executePreStepCallbacks();
    void executePostStepCallbacks();

    std::string m_instanceName;
    std::string m_fmuGUID;
    std::string m_resources_location;

    bool m_visible;

    const std::unordered_set<std::string> m_logCategories_debug;  ///< list of log categories considered to be of debug
    bool m_debug_logging_enabled = true;                          ///< enable logging?

    // DefaultExperiment
    fmi2Real m_startTime = 0;
    fmi2Real m_stopTime = 1;
    fmi2Real m_tolerance = -1;
    fmi2Boolean m_toleranceDefined = 0;
    fmi2Boolean m_stopTimeDefined = 0;

    fmi2Real m_stepSize = 1e-3;
    fmi2Real m_time = 0;

    const std::string m_modelIdentifier;

    fmi2Type m_fmuType;

    std::map<FmuVariable::Type, unsigned int> m_valueReferenceCounter;

    std::set<FmuVariableExport> m_variables;
    std::unordered_map<std::string, UnitDefinition> m_unitDefinitions;
    std::unordered_map<std::string, std::pair<std::string, std::vector<std::string>>> m_derivatives;
    std::unordered_map<std::string, std::vector<std::string>> m_variableDependencies;

    std::list<std::function<void(void)>> m_preStepCallbacks;
    std::list<std::function<void(void)>> m_postStepCallbacks;

    fmi2CallbackFunctions m_callbackFunctions;
    FmuMachineState m_fmuMachineState;

    std::unordered_map<std::string, bool> m_logCategories_enabled;
};

// -----------------------------------------------------------------------------

/// Function to create an instance of a particular FMU.
/// This function must be implemented by a concrete FMU and should return a new object of the concrete FMU type.
FmuComponentBase* fmi2InstantiateIMPL(fmi2String instanceName,
                                      fmi2Type fmuType,
                                      fmi2String fmuGUID,
                                      fmi2String fmuResourceLocation,
                                      const fmi2CallbackFunctions* functions,
                                      fmi2Boolean visible,
                                      fmi2Boolean loggingOn);

/// @} fmu_forge_fmi2

}  // namespace fmi2
}  // namespace fmu_forge

#endif
