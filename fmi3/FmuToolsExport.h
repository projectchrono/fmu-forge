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
// Classes for exporting FMUs (FMI 3.0)
// =============================================================================

#ifndef FMUTOOLS_FMI3_EXPORT_H
#define FMUTOOLS_FMI3_EXPORT_H

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
#include <sstream>

#include "FmuToolsUnitDefinitions.h"
#include "fmi3/FmuToolsVariable.h"
#include "fmi3/fmi3_headers/fmi3Functions.h"

//// RADU TODO - do we want/need custom type variants?
#ifndef FMITYPESPLATFORM_CUSTOM
    #include "fmi3/TypesVariantsDefault.h"
#else
    #include "fmi3/TypesVariantsCustom.h"
#endif

// =============================================================================

namespace fmu_forge {
namespace fmi3 {

/// @addtogroup fmu_forge_fmi3
/// @{

#ifdef __cplusplus
extern "C" {
#endif

bool FMI3_Export createModelDescription(const std::string& path, std::string& err_msg);

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
    // using StartType = FmuVariableStartType;

    //// TODO: can we remove it and keep only the constructor with dimensions with default argument?
    FmuVariableExport(const VarbindType& varbind,
                      const std::string& name,
                      FmuVariable::Type type,
                      CausalityType causality = CausalityType::local,
                      VariabilityType variability = VariabilityType::continuous,
                      InitialType initial = InitialType::automatic);

    FmuVariableExport(const VarbindType& varbind,
                      const std::string& name,
                      FmuVariable::Type type,
                      const DimensionsArrayType& dimensions,
                      CausalityType causality = CausalityType::local,
                      VariabilityType variability = VariabilityType::continuous,
                      InitialType initial = InitialType::automatic);

    FmuVariableExport(const FmuVariableExport& other);

    /// Copy assignment operator.
    FmuVariableExport& operator=(const FmuVariableExport& other);

    void Bind(VarbindType varbind) { m_varbind = varbind; }

    /// Set the value of this FMU variable (for all cases, except variables of type fmi3String).
    /// 'values' is expected to have a size of at least 'nValues'.
    template <typename fmi3VarType,
              typename = typename std::enable_if<!std::is_same<fmi3VarType, fmi3String>::value>::type>
    void SetValue(const fmi3VarType* values, size_t nValues) const {
        if (is_pointer_variant(m_varbind)) {
            assert((nValues == 0 || (IsScalar() && nValues == 1) || m_dimensions.size() > 0) &&
                   ("Requested to get the value of " + std::to_string(nValues) +
                    " coefficients for variable with valueReference: " + std::to_string(m_valueReference) +
                    " but it seems that it is a scalar.")
                       .c_str());

            fmi3VarType* varptr_this = varns::get<fmi3VarType*>(m_varbind);

            // try to fetch the dimension of the variable
            if (nValues == 0) {
                bool success = GetSize(nValues);
                if (!success)
                    throw std::runtime_error(
                        "SetValue has been called with 'nValues==0', but the variable dimensions are given by other "
                        "variables and cannot be determined automatically.");
            }

            for (size_t size = 0; size < nValues; size++) {
                varptr_this[size] = values[size];
            }
        } else {
            // TODO: consider multi-dimensional variables
            varns::get<FunGetSet<fmi3VarType>>(m_varbind).second(*values);
        }
    }

    /// Set the value of this FMU variable of type fmi3String.
    /// Currently only single values are supported.
    void SetValue(const fmi3String* val, size_t nValues) const;

    /// Set the value of this FMU variable of type fmi3String.
    /// Currently only single values are supported.
    void SetValue(const fmi3Binary* val, size_t nValues, const size_t* valueSize_ptr) const;

    /// Get the value and dimensions of this FMU variable (for all cases, except variables of type fmi3String).
    /// The 'dimensions' argument is optional and will be filled with the sizes along different dimensions.
    /// When empty, the variable is scalar.
    template <typename fmi3VarType,
              typename = typename std::enable_if<!std::is_same<fmi3VarType, fmi3String>::value>::type>
    void GetValue(fmi3VarType* varptr_ext, size_t nValues) const {
        if (is_pointer_variant(m_varbind)) {
            assert(((IsScalar() && nValues == 1) || m_dimensions.size() > 0) &&
                   ("Requested to get the value of " + std::to_string(nValues) +
                    " coefficients for variable with valueReference: " + std::to_string(m_valueReference) +
                    " but it seems that it is a scalar.")
                       .c_str());

            fmi3VarType* varptr_this = varns::get<fmi3VarType*>(m_varbind);

            // try to fetch the dimension of the variable
            if (nValues == 0) {
                bool success = GetSize(nValues);
                if (!success)
                    throw std::runtime_error(
                        "GetValue has been called with 'nValues==0', but the variable dimensions are given by other "
                        "variables and cannot be determined automatically.");
            }

            // copy the values from the binded variable to the external variable provided by the user
            for (size_t size = 0; size < nValues; size++) {
                varptr_ext[size] = varptr_this[size];
            }
        } else {
            // TODO: consider multi-dimensional variables
            *varptr_ext = varns::get<FunGetSet<fmi3VarType>>(m_varbind).first();
        }
    }

    /// Get the *location* of the fmi3String variable.
    /// WARNING: fmi3String content is NOT copied. Only the pointer to the fmi3String is returned.
    /// The user must then copy the content considering that the fmi3String is null-terminated.
    /// FMU developers using non-standard FMI variable types should re-implement this method.
    virtual void GetValue(fmi3String* varptr, size_t nValues) const;

    /// Get the *location* and *size* of the fmi3Binary variable.
    /// WARNING: fmi3Binary content is NOT copied. Only the pointer to the fmi3Binary is returned.
    /// 'varptr_ext' and 'valueSize_ptr' are expected to point to two pre-allocated spaces of size equal to the number
    /// of Dimensions of the FMU variable (or 1 if scalar) and not to the size of the variable itself. FMU developers
    /// using non-standard FMI variable types should re-implement this method.
    virtual void GetValue(fmi3Binary* varptr_ext, size_t nValues, size_t* valueSize_ptr) const;

    /// Force the exposure of the start value in the modelDescription (if allowed).
    void ExposeStartValue(bool val) const { m_expose_start = val; }

    /// Check if the start value should be exposed in the modelDescription.
    bool IsStartValueExposed() const { return m_expose_start || m_required_start; }

  protected:
    bool m_allowed_start = true;          ///< specification of start value permitted
    bool m_required_start = false;        ///< specification of start value required
    mutable bool m_expose_start = false;  ///< force exposure of start value

    VarbindType m_varbind;  ///< value of this variable

    /// Convert the start value to a string.
    /// In case of arrays it concatenates the values with space delimitations.
    /// However, in the case of Binary or String data (that basically are arrays of arrays of chars|bytes),
    /// the argument is not used to tell the size of the array (since it is already known), but to select which of the
    /// elements contained in the array should be picked.
    std::string GetStartValAsString(size_t size_id = 0) const;

    friend class FmuComponentBase;
};

// =============================================================================

/// Returns a string representation of the variable value (built-in type pointer case).
template <typename T>
void variant_to_string(const T* varb, size_t size, std::stringstream& ss) {
    for (size_t s = 0; s < size; ++s) {
        ss << *varb;
        if (s + 1 < size)
            ss << " ";
    }
}

/// Returns a string representation of the variable value (setter|getter case).
template <typename T>
void variant_to_string(const FunGetSet<T> varb, size_t size, std::stringstream& ss) {
    ss << varb.first();
}

/// Returns a string representation of the variable type (std::string pointer case).
void variant_to_string(const std::string* varb, size_t id, std::stringstream& ss);
void variant_to_string(const std::vector<fmi3Byte>* varb, size_t id, std::stringstream& ss);

void variant_to_string(const FunGetSet<std::string> varb, size_t size, std::stringstream& ss);

// =============================================================================

/// Base class for an FMU component (used for export).
/// (1) This class provides support for:
/// - defining FMU variables (cauality, variability, start value, etc)
/// - defining FMU model structure (outputs, derivatives, variable dependencies, etc)
/// (2) This class defines the virtual methods that an FMU must implement
class FmuComponentBase {
  public:
    FmuComponentBase(FmuType fmiInterfaceType,
                     fmi3String instanceName,
                     fmi3String instantiationToken,
                     fmi3String resourcePath,
                     fmi3Boolean visible,
                     fmi3Boolean loggingOn,
                     fmi3InstanceEnvironment instanceEnvironment,
                     fmi3LogMessageCallback logMessage,
                     const std::unordered_map<std::string, bool>& logCategories_init,
                     const std::unordered_set<std::string>& logCategories_debug_init);

    virtual ~FmuComponentBase() {}

    const std::set<FmuVariableExport>& GetVariables() const { return m_variables; }

    /// Enable/disable the logging for a specific log category.
    virtual void SetDebugLogging(std::string cat, bool val);

    /// Create the modelDescription.xml file in the given location \a path.
    void ExportModelDescription(std::string path);

    double GetTime() const { return m_time; }

    template <class T>
    fmi3Status fmi3GetVariable(const fmi3ValueReference vrs[], size_t nvr, T values[], size_t nValues) {
        //// when multiple variables are requested it might be better to iterate through scalarVariables just once
        //// and check if they match any of the nvr requested variables
        size_t values_idx = 0;
        for (size_t s = 0; s < nvr; ++s) {
            auto it = this->findByValref(vrs[s]);

            if (it == this->m_variables.end()) {
                // requested a variable that does not exist
                auto msg =
                    "fmi3GetVariable: variable with value reference " + std::to_string(vrs[s]) + " does NOT exist.\n";
                sendToLog(msg, fmi3Status::fmi3Error, "logStatusError");
                return fmi3Status::fmi3Error;
            } else {
                size_t var_size = GetVariableSize(*it);
                it->GetValue(&values[values_idx], var_size);
                values_idx += var_size;
            }
        }

        fmi3Status status = (values_idx == nValues) ? fmi3Status::fmi3OK : fmi3Status::fmi3Error;
        return status;
    }

    fmi3Status fmi3GetVariable(const fmi3ValueReference vrs[],
                               size_t nvr,
                               size_t valueSizes[],
                               fmi3Binary values[],
                               size_t nValues) {
        //// when multiple variables are requested it might be better to iterate through scalarVariables just once
        //// and check if they match any of the nvr requested variables
        size_t values_idx = 0;
        for (size_t s = 0; s < nvr; ++s) {
            auto it = this->findByValref(vrs[s]);

            if (it == this->m_variables.end()) {
                // requested a variable that does not exist
                auto msg =
                    "fmi3GetVariable: variable with value reference " + std::to_string(vrs[s]) + " does NOT exist.\n";
                sendToLog(msg, fmi3Status::fmi3Error, "logStatusError");
                return fmi3Status::fmi3Error;
            } else {
                size_t var_size = GetVariableSize(*it);
                it->GetValue(&values[values_idx], var_size, &valueSizes[values_idx]);
                values_idx += var_size;
            }
        }

        fmi3Status status = (values_idx == nValues) ? fmi3Status::fmi3OK : fmi3Status::fmi3Error;
        return status;
    }

    template <class T>
    fmi3Status fmi3SetVariable(const fmi3ValueReference vrs[], size_t nvr, const T values[], size_t nValues) {
        size_t values_idx = 0;
        for (size_t s = 0; s < nvr; ++s) {
            std::set<FmuVariableExport>::iterator it = this->findByValref(vrs[s]);

            if (it == this->m_variables.end()) {
                // requested a variable that does not exist
                auto msg =
                    "fmi3SetVariable: variable with value reference " + std::to_string(vrs[s]) + " does NOT exist.\n";
                sendToLog(msg, fmi3Status::fmi3Error, "logStatusError");
                return fmi3Status::fmi3Error;
            } else if (!it->IsSetAllowed(this->m_fmuMachineState)) {
                // requested variable cannot be set in the current FMU state
                auto msg = "fmi3SetVariable: variable with value reference " + std::to_string(vrs[s]) +
                           " NOT ALLOWED to be set in current state.\n";
                sendToLog(msg, fmi3Status::fmi3Error, "logStatusError");
                return fmi3Status::fmi3Error;
            } else {
                size_t var_size = GetVariableSize(*it);
                it->SetValue(&values[values_idx], var_size);
                values_idx += var_size;
            }
        }

        fmi3Status status = (values_idx == nValues) ? fmi3Status::fmi3OK : fmi3Status::fmi3Error;
        return status;
    }

    fmi3Status fmi3SetVariable(const fmi3ValueReference vrs[],
                               size_t nvr,
                               const size_t valueSizes[],
                               const fmi3Binary values[],
                               size_t nValues) {
        size_t values_idx = 0;
        for (size_t s = 0; s < nvr; ++s) {
            std::set<FmuVariableExport>::iterator it = this->findByValref(vrs[s]);

            if (it == this->m_variables.end()) {
                // requested a variable that does not exist
                auto msg =
                    "fmi3SetVariable: variable with value reference " + std::to_string(vrs[s]) + " does NOT exist.\n";
                sendToLog(msg, fmi3Status::fmi3Error, "logStatusError");
                return fmi3Status::fmi3Error;
            } else if (!it->IsSetAllowed(this->m_fmuMachineState)) {
                // requested variable cannot be set in the current FMU state
                auto msg = "fmi3SetVariable: variable with value reference " + std::to_string(vrs[s]) +
                           " NOT ALLOWED to be set in current state.\n";
                sendToLog(msg, fmi3Status::fmi3Error, "logStatusError");
                return fmi3Status::fmi3Error;
            } else {
                size_t var_size = GetVariableSize(*it);
                it->SetValue(&values[values_idx], var_size, &valueSizes[values_idx]);
                values_idx += var_size;
            }
        }

        fmi3Status status = (values_idx == nValues) ? fmi3Status::fmi3OK : fmi3Status::fmi3Error;
        return status;
    }

    /// Adds a scalar variable to the list of variables of the FMU.
    /// The start value is automatically grabbed from the variable itself.
    const FmuVariableExport& AddFmuVariable(
        const FmuVariableExport::VarbindType& varbind,
        std::string name,
        FmuVariable::Type scalartype = FmuVariable::Type::Float64,
        std::string unitname = "",
        std::string description = "",
        FmuVariable::CausalityType causality = FmuVariable::CausalityType::local,
        FmuVariable::VariabilityType variability = FmuVariable::VariabilityType::continuous,
        FmuVariable::InitialType initial = FmuVariable::InitialType::automatic) {
        return AddFmuVariable(varbind, name, scalartype, FmuVariableExport::DimensionsArrayType(), unitname,
                              description, causality, variability, initial);
    }

    /// Adds an array variable to the list of variables of the FMU.
    /// The start value is automatically grabbed from the variable itself.
    const FmuVariableExport& AddFmuVariable(
        const FmuVariableExport::VarbindType& varbind,
        std::string name,
        FmuVariable::Type scalartype = FmuVariable::Type::Float64,
        const FmuVariableExport::DimensionsArrayType& dimensions = FmuVariableExport::DimensionsArrayType(),
        std::string unitname = "",
        std::string description = "",
        FmuVariable::CausalityType causality = FmuVariable::CausalityType::local,
        FmuVariable::VariabilityType variability = FmuVariable::VariabilityType::continuous,
        FmuVariable::InitialType initial = FmuVariable::InitialType::automatic);

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

    virtual fmi3Status updateDiscreteStatesIMPL(fmi3Boolean* discreteStatesNeedUpdate,
                                                fmi3Boolean* terminateSimulation,
                                                fmi3Boolean* nominalsOfContinuousStatesChanged,
                                                fmi3Boolean* valuesOfContinuousStatesChanged,
                                                fmi3Boolean* nextEventTimeDefined,
                                                fmi3Float64* nextEventTime) {
        *discreteStatesNeedUpdate = fmi3False;
        *terminateSimulation = fmi3False;
        *nominalsOfContinuousStatesChanged = fmi3False;
        *valuesOfContinuousStatesChanged = fmi3False;
        *nextEventTimeDefined = fmi3False;
        *nextEventTime = 0;

        return fmi3Status::fmi3OK;
    }

    virtual fmi3Status doStepIMPL(fmi3Float64 currentCommunicationPoint,
                                  fmi3Float64 communicationStepSize,
                                  fmi3Boolean noSetFMUStatePriorToCurrentPoint,
                                  fmi3Boolean* eventHandlingNeeded,
                                  fmi3Boolean* terminateSimulation,
                                  fmi3Boolean* earlyReturn,
                                  fmi3Float64* lastSuccessfulTime) {
        if (is_cosimulation_available())
            throw std::runtime_error("An FMU for co-simulation must implement doStepIMPL");

        return fmi3Status::fmi3OK;
    }

    virtual fmi3Status completedIntegratorStepIMPL(fmi3Boolean noSetFMUStatePriorToCurrentPoint,
                                                   fmi3Boolean* enterEventMode,
                                                   fmi3Boolean* terminateSimulation) {
        *enterEventMode = fmi3False;
        *terminateSimulation = fmi3False;

        return fmi3Status::fmi3OK;
    }

    virtual fmi3Status setTimeIMPL(fmi3Float64 time) { return fmi3Status::fmi3OK; }

    virtual fmi3Status getContinuousStatesIMPL(fmi3Float64 continuousStates[], size_t nContinuousStates) {
        if (is_modelexchange_available())
            throw std::runtime_error("An FMU for model exchange must implement getContinuousStatesIMPL");

        return fmi3Status::fmi3OK;
    }

    virtual fmi3Status setContinuousStatesIMPL(const fmi3Float64 continuousStates[], size_t nContinuousStates) {
        if (is_modelexchange_available())
            throw std::runtime_error("An FMU for model exchange must implement setContinuousStatesIMPL");

        return fmi3Status::fmi3OK;
    }

    virtual fmi3Status getDerivativesIMPL(fmi3Float64 derivatives[], size_t nContinuousStates) {
        if (is_modelexchange_available())
            throw std::runtime_error("An FMU for model exchange must implement getDerivativesIMPL");

        return fmi3Status::fmi3OK;
    }

    virtual fmi3Status enterInitializationModeIMPL() { return fmi3Status::fmi3OK; }
    virtual fmi3Status exitInitializationModeIMPL() { return fmi3Status::fmi3OK; }

  public:
    void SetIntermediateUpdateCallback(fmi3IntermediateUpdateCallback intermediateUpdate) {
        m_intermediateUpdate = intermediateUpdate;
    }

    fmi3Status EnterInitializationMode(fmi3Boolean toleranceDefined,
                                       fmi3Float64 tolerance,
                                       fmi3Float64 startTime,
                                       fmi3Boolean stopTimeDefined,
                                       fmi3Float64 stopTime);

    fmi3Status ExitInitializationMode();

    // Common FMI function.
    // These functions are used to implement the actual common functions imposed by the FMI3 standard.
    // In turn, they call overrides of virtual methods provided by a concrete FMU.
    fmi3Status UpdateDiscreteStates(fmi3Boolean* discreteStatesNeedUpdate,
                                    fmi3Boolean* terminateSimulation,
                                    fmi3Boolean* nominalsOfContinuousStatesChanged,
                                    fmi3Boolean* valuesOfContinuousStatesChanged,
                                    fmi3Boolean* nextEventTimeDefined,
                                    fmi3Float64* nextEventTime);

    // Co-Simulation FMI functions.
    // These functions are used to implement the actual co-simulation functions imposed by the FMI3 standard.
    // In turn, they call overrides of virtual methods provided by a concrete FMU.

    fmi3Status DoStep(fmi3Float64 currentCommunicationPoint,
                      fmi3Float64 communicationStepSize,
                      fmi3Boolean noSetFMUStatePriorToCurrentPoint,
                      fmi3Boolean* eventHandlingNeeded,
                      fmi3Boolean* terminateSimulation,
                      fmi3Boolean* earlyReturn,
                      fmi3Float64* lastSuccessfulTime);

    // Model Exchange FMI functions.
    // These functions are used to implement the actual model exchange functions imposed by the FMI3 standard.
    // In turn, they call overrides of virtual methods provided by a concrete FMU.

    fmi3Status CompletedIntegratorStep(fmi3Boolean noSetFMUStatePriorToCurrentPoint,
                                       fmi3Boolean* enterEventMode,
                                       fmi3Boolean* terminateSimulation);
    fmi3Status SetTime(const fmi3Float64 time);
    fmi3Status GetContinuousStates(fmi3Float64 continuousStates[], size_t nContinuousStates);
    fmi3Status SetContinuousStates(const fmi3Float64 continuousStates[], size_t nContinuousStates);
    fmi3Status GetDerivatives(fmi3Float64 derivatives[], size_t nContinuousStates);

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

    void initializeType(FmuType fmuType);

    void addUnitDefinition(const UnitDefinition& unit_definition);

    void clearUnitDefinitions() { m_unitDefinitions.clear(); }

    bool IsInitialized() const {
        return m_fmuMachineState == FmuMachineState::eventMode ||
               m_fmuMachineState == FmuMachineState::continuousTimeMode ||
               m_fmuMachineState == FmuMachineState::stepMode ||
               m_fmuMachineState == FmuMachineState::clockActivationMode;
    }

    bool IsFMUStateSettable() const {
        return !(m_fmuMachineState == FmuMachineState::configurationMode ||
                 m_fmuMachineState == FmuMachineState::reconfigurationMode ||
                 m_fmuMachineState == FmuMachineState::intermediateUpdateMode ||
                 m_fmuMachineState == FmuMachineState::clockUpdateMode);
    }

    /// Send message to the logger function.
    /// The message will be sent if at least one of the following applies:
    /// - a msg_cat was enabled by `SetDebugLogging(msg_cat, true)`
    /// - the FMU was instantiated with `loggingOn=fmi3True` and a msg_cat has been labelled as a debugging category;
    /// FMUs generated by this library provides a Description which reports if the category is debug.
    void sendToLog(std::string msg, fmi3Status status, std::string msg_cat);

    std::set<FmuVariableExport>::iterator findByValref(fmi3ValueReference vr);
    std::set<FmuVariableExport>::const_iterator findByValref(fmi3ValueReference vr) const;
    std::set<FmuVariableExport>::iterator findByName(const std::string& name);

    /// Get the current dimensions of a variable.
    std::vector<size_t> GetVariableDimensions(const FmuVariable& var) const;

    /// Get the current dimensions of a variable given its valueReference.
    /// If the dimension depends on another variables, these variables needs to be added first.
    std::vector<size_t> GetVariableDimensions(fmi3ValueReference valref) const;

    /// Get the current total size of a variable.
    size_t GetVariableSize(const FmuVariable& var) const;

    /// Get the current total size of a variable.
    size_t GetVariableSize(fmi3ValueReference valref) const { return GetVariableSize(*findByValref(valref)); }

    void executePreStepCallbacks();
    void executePostStepCallbacks();

    std::string m_instanceName;
    std::string m_instantiationToken;
    std::string m_resources_location;

    bool m_eventModeUsed = false;  // TODO: enable this feature

    bool m_visible;

    const std::unordered_set<std::string> m_logCategories_debug;  ///< list of log categories considered to be of debug
    bool m_debug_logging_enabled = true;                          ///< enable logging?

    // DefaultExperiment
    fmi3Float64 m_startTime = 0;
    fmi3Float64 m_stopTime = 1;
    fmi3Float64 m_tolerance = -1;
    fmi3Boolean m_toleranceDefined = fmi3False;
    fmi3Boolean m_stopTimeDefined = fmi3False;

    fmi3Float64 m_stepSize = 1e-3;
    fmi3Float64 m_time = 0;

    const std::string m_modelIdentifier;

    FmuType m_fmuType;

    unsigned int m_valrefCounter = 0;

    std::set<FmuVariableExport> m_variables;
    std::unordered_map<std::string, UnitDefinition> m_unitDefinitions;
    std::unordered_map<std::string, std::pair<std::string, std::vector<std::string>>> m_derivatives;
    std::unordered_map<std::string, std::vector<std::string>> m_variableDependencies;

    std::list<std::function<void(void)>> m_preStepCallbacks;
    std::list<std::function<void(void)>> m_postStepCallbacks;

    fmi3InstanceEnvironment m_instanceEnvironment;
    fmi3LogMessageCallback m_logMessage;
    fmi3IntermediateUpdateCallback m_intermediateUpdate;
    // The following callbacks are used only for ScheduledExecution
    ////fmi3LockPreemptionCallback m_lockPreemption;
    ////fmi3UnlockPreemptionCallback m_unlockPreemption;

    FmuMachineState m_fmuMachineState;

    std::unordered_map<std::string, bool> m_logCategories_enabled;

    bool expose_variable_start_values_whenever_possible = true;
};

// -----------------------------------------------------------------------------

/// Function to create an instance of a particular FMU for the specified FMI interface.
/// This function must be implemented by a concrete FMU and should return a new object of the concrete FMU type.
/// If instantiation failed (e.g., because the FMU does not support the requested interface), the FMU must call
/// logMessage with detailed information about the reason.
FmuComponentBase* fmi3InstantiateIMPL(FmuType fmiInterfaceType,
                                      fmi3String instanceName,
                                      fmi3String instantiationToken,
                                      fmi3String resourcePath,
                                      fmi3Boolean visible,
                                      fmi3Boolean loggingOn,
                                      fmi3InstanceEnvironment instanceEnvironment,
                                      fmi3LogMessageCallback logMessage);

/// @} fmu_forge_fmi3

}  // namespace fmi3
}  // namespace fmu_forge

#endif
