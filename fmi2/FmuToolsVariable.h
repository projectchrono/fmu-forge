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
// Definition of the FMU variable base class and logging utilities (FMI 2.0)
// =============================================================================

#ifndef FMUTOOLS_FMI2_VARIABLE_H
#define FMUTOOLS_FMI2_VARIABLE_H

#include <stdexcept>
#include <string>
#include <cstdarg>
#include <typeindex>
#include <unordered_map>
#include <cassert>

#include "fmi2/fmi2_headers/fmi2FunctionTypes.h"
#include "fmi2/fmi2_headers/fmi2Functions.h"

// =============================================================================

namespace fmu_forge {
/// Namespace for FMI 2.0 classes
namespace fmi2 {

/// @addtogroup fmu_forge_fmi2
/// @{

/// Enumeration of supported FMU types (interfaces).
enum class FmuType {
    MODEL_EXCHANGE,  ///< FMU for model exchange
    COSIMULATION     ///< FMU for co-simulation
};

// =============================================================================

/// Enumeration of FMI machine states.
enum class FmuMachineState {
    anySettableState,    // custom element, used to do checks
    instantiated,        //
    initializationMode,  //
    stepCompleted,       // only CoSimulation
    stepInProgress,      // only CoSimulation
    stepFailed,          // only CoSimulation
    stepCanceled,        // only CoSimulation
    terminated,          //
    error,               //
    fatal,               //
    eventMode,           // only ModelExchange
    continuousTimeMode   // only ModelExchange
};

// =============================================================================

struct LoggingUtilities {
    static std::string fmi2Status_toString(fmi2Status status) {
        switch (status) {
            case fmi2Status::fmi2Discard:
                return "Discard";
                break;
            case fmi2Status::fmi2Error:
                return "Error";
                break;
            case fmi2Status::fmi2Fatal:
                return "Fatal";
                break;
            case fmi2Status::fmi2OK:
                return "OK";
                break;
            case fmi2Status::fmi2Pending:
                return "Pending";
                break;
            case fmi2Status::fmi2Warning:
                return "Warning";
                break;
            default:
                throw std::runtime_error("Wrong fmi2Status");
                break;
        }
    }

    static void logger_default(fmi2ComponentEnvironment c,
                               fmi2String instanceName,
                               fmi2Status status,
                               fmi2String category,
                               fmi2String message,
                               ...) {
        char msg[2024];
        va_list argp;
        va_start(argp, message);
        vsprintf(msg, message, argp);
        if (!instanceName)
            instanceName = "?";
        if (!category)
            category = "?";
        printf("[%s|%s] %s: %s", instanceName, fmi2Status_toString(status).c_str(), category, msg);
    }
};

// =============================================================================

/// Implementation of an FMU variable.
/// Objects of this type are created during:
/// - FMU export (and encoded in the model description XML)
/// - FMU import (retrieved from the model description XML)
/// The model description XML lists variables grouped by type, in increasing order of the Type enum values.
class FmuVariable {
  public:
    enum class Type { Real = 0, Integer = 1, Boolean = 2, String = 3, Unknown = 4 };

    enum class CausalityType { parameter, calculatedParameter, input, output, local, independent };

    enum class VariabilityType { constant, fixed, tunable, discrete, continuous };

    enum class InitialType { none, exact, approx, calculated };

    FmuVariable() : FmuVariable("", FmuVariable::Type::Real) {}

    FmuVariable(const std::string& name,
                FmuVariable::Type type,
                CausalityType causality = CausalityType::local,
                VariabilityType variability = VariabilityType::continuous,
                InitialType initial = InitialType::none)
        : m_name(name),
          m_valueReference(0),
          m_unitname("1"),
          m_type(type),
          m_causality(causality),
          m_variability(variability),
          m_initial(initial),
          m_description(""),
          m_has_start(false) {
        // Readibility replacements
        bool c_parameter = (m_causality == CausalityType::parameter);
        bool c_calculated = (m_causality == CausalityType::calculatedParameter);
        bool c_input = (m_causality == CausalityType::input);
        bool c_output = (m_causality == CausalityType::output);
        bool c_local = (m_causality == CausalityType::local);
        bool c_independent = (m_causality == CausalityType::independent);

        bool v_constant = (m_variability == VariabilityType::constant);
        bool v_fixed = (m_variability == VariabilityType::fixed);
        bool v_tunable = (m_variability == VariabilityType::tunable);
        bool v_discrete = (m_variability == VariabilityType::discrete);
        bool v_continuous = (m_variability == VariabilityType::continuous);

        bool i_none = (m_initial == InitialType::none);
        bool i_exact = (m_initial == InitialType::exact);
        bool i_approx = (m_initial == InitialType::approx);
        bool i_calculated = (m_initial == InitialType::calculated);

        // Set "initial" property if empty (see Table on page 51 of the FMI2.0.4 specification)
        // (A)
        if (((v_constant) && (c_output || c_local)) || ((v_fixed || v_tunable) && (c_parameter))) {
            if (i_none)
                m_initial = InitialType::exact;
            else if (!i_exact)
                throw std::runtime_error("initial not set properly.");
        }
        // (B)
        else if ((v_fixed || v_tunable) && (c_calculated || c_local)) {
            if (i_none)
                m_initial = InitialType::calculated;
            else if (!i_approx && !i_calculated)
                throw std::runtime_error("initial not set properly.");
        }
        // (C)
        else if ((v_discrete || v_continuous) && (c_output || c_local)) {
            if (i_none)
                m_initial = InitialType::calculated;
        }

        // From page 51 of FMI2.0.4 specification:
        // (1) If causality = "independent", it is neither allowed to define a value for initial nor a value for start.
        // (2) If causality = "input", it is not allowed to define a value for initial and a value for start must be
        // defined.
        // (3) [not relevant] If (C) and initial = "exact", then the variable is explicitly defined by its start value
        // in Initialization Mode
        if (c_independent && !i_none)
            throw std::runtime_error(
                "If causality = 'independent', it is neither allowed to define a value for initial nor a value for "
                "start.");

        if (c_input && !i_none)
            throw std::runtime_error(
                "If causality = 'input', it is not allowed to define a value for initial and a value for start must be "
                "defined.");

        // Incompatible variability/causality settings (see Table on page 51 of the FMI2.0.4 specification)
        // (a)
        if (v_constant && (c_parameter || c_calculated || c_input))
            throw std::runtime_error(
                "constants always have their value already set, thus their causality can be only 'output' or 'local'");
        // (b)
        if ((v_discrete || v_continuous) && (c_parameter || c_calculated))
            throw std::runtime_error(
                "parameters and calculatedParameters cannot be discrete nor continuous, as they do not change over "
                "time.");
        // (c)
        if (c_independent && !v_continuous)
            throw std::runtime_error("For an 'independent' variable only variability = 'continuous' makes sense.");
        // (d) + (e)
        if (c_input && (v_fixed || v_tunable))
            throw std::runtime_error(
                "A fixed or tunable 'input'|'output' have exactly the same properties as a fixed or tunable parameter. "
                "For simplicity, only fixed and tunable parameters|calculatedParameters shall be defined.");
    }

    FmuVariable(const FmuVariable& other) {
        m_type = other.m_type;
        m_name = other.m_name;
        m_valueReference = other.m_valueReference;
        m_unitname = other.m_unitname;
        m_causality = other.m_causality;
        m_variability = other.m_variability;
        m_initial = other.m_initial;
        m_description = other.m_description;
        m_has_start = other.m_has_start;
    }

    // Copy assignment operator
    FmuVariable& operator=(const FmuVariable& other) {
        if (this == &other) {
            return *this;  // Self-assignment guard
        }

        m_type = other.m_type;
        m_name = other.m_name;
        m_valueReference = other.m_valueReference;
        m_unitname = other.m_unitname;
        m_causality = other.m_causality;
        m_variability = other.m_variability;
        m_initial = other.m_initial;
        m_description = other.m_description;
        m_has_start = other.m_has_start;

        return *this;
    }

    virtual ~FmuVariable() {}

    bool operator<(const FmuVariable& other) const {
        return this->m_type != other.m_type ? this->m_type < other.m_type
                                            : this->m_valueReference < other.m_valueReference;
    }

    bool operator==(const FmuVariable& other) const {
        // according to FMI Reference can exist two different variables with same type and same valueReference;
        // they are called "alias" thus they should be allowed but not considered equal
        return this->m_name == other.m_name;
    }

    /// Return true if a start value is specified for this variable.
    bool HasStartVal() const { return m_has_start; }

    /// Check if setting this variable is allowed given the current FMU state.
    bool IsSetAllowed(FmuMachineState fmu_machine_state) const {
        if (m_variability != VariabilityType::constant) {
            if (m_initial == InitialType::approx)
                return fmu_machine_state == FmuMachineState::instantiated ||
                       fmu_machine_state == FmuMachineState::anySettableState;
            else if (m_initial == InitialType::exact)
                return fmu_machine_state == FmuMachineState::instantiated ||
                       fmu_machine_state == FmuMachineState::initializationMode ||
                       fmu_machine_state == FmuMachineState::anySettableState;
        }

        if (m_causality == CausalityType::input ||
            (m_causality == CausalityType::parameter && m_variability == VariabilityType::tunable))
            return fmu_machine_state == FmuMachineState::initializationMode ||
                   fmu_machine_state == FmuMachineState::stepCompleted ||
                   fmu_machine_state == FmuMachineState::anySettableState;

        return false;
    }

    /// Return a string with the name of the specified FMU variable type.
    static std::string Type_toString(Type type) {
        switch (type) {
            case Type::Real:
                return "Real";
                break;
            case Type::Integer:
                return "Integer";
                break;
            case Type::Boolean:
                return "Boolean";
                break;
            case Type::String:
                return "String";
                break;
            case Type::Unknown:
                return "Unknown";
                break;
            default:
                throw std::runtime_error("Type_toString: received bad type.");
                break;
        }
    }

    const inline std::string& GetName() const { return m_name; }
    inline CausalityType GetCausality() const { return m_causality; }
    inline VariabilityType GetVariability() const { return m_variability; }
    inline InitialType GetInitial() const { return m_initial; }
    const inline std::string& GetDescription() const { return m_description; }
    void SetDescription(const std::string& description) { m_description = description; }
    const inline fmi2ValueReference GetValueReference() const { return m_valueReference; }
    void SetValueReference(fmi2ValueReference valref) { m_valueReference = valref; }
    const inline std::string& GetUnitName() const { return m_unitname; }
    void SetUnitName(const std::string& unitname) { m_unitname = unitname; }
    Type GetType() const { return m_type; }

  protected:
    Type m_type = Type::Unknown;          ///< variable type
    std::string m_name;                   ///< variable name
    fmi2ValueReference m_valueReference;  ///< reference among variables of same type
    std::string m_unitname;               ///< variable units
    CausalityType m_causality;            ///< variable causality
    VariabilityType m_variability;        ///< variable variability
    InitialType m_initial;                ///< type of initial value
    std::string m_description;            ///< description of this variable

    bool m_has_start;  ///< start value provided
};

/// @} fmu_forge_fmi2

}  // namespace fmi2
}  // namespace fmu_forge

#endif
