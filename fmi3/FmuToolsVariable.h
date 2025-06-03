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
// Definition of the FMU variable base class and logging utilities (FMI 3.0)
// =============================================================================

#ifndef FMUTOOLS_FMI3_VARIABLE_H
#define FMUTOOLS_FMI3_VARIABLE_H

#include <stdexcept>
#include <string>
#include <cstdarg>
#include <typeindex>
#include <unordered_map>
#include <cassert>

#include "fmi3/fmi3_headers/fmi3FunctionTypes.h"
#include "fmi3/fmi3_headers/fmi3Functions.h"

namespace fmu_forge {
/// Namespace for FMI 3.0 classes
namespace fmi3 {

/// @addtogroup fmu_forge_fmi3
/// @{

// =============================================================================

/// Enumeration of supported FMU types (interfaces).
enum class FmuType {
    MODEL_EXCHANGE,      ///< FMU for model exchange
    COSIMULATION,        ///< FMU for co-simulation
    SCHEDULED_EXECUTION  ///< FMU for co-simulation
};

// =============================================================================

/// Enumeration of FMI machine states.
enum class FmuMachineState {
    instantiated,            //
    initializationMode,      //
    eventMode,               //
    terminated,              //
    stepMode,                // only CoSimulation
    intermediateUpdateMode,  // only CoSimulation
    continuousTimeMode,      // only ModelExchange
    configurationMode,       //
    reconfigurationMode,     //
    clockActivationMode,     // only ScheduledExecution
    clockUpdateMode          // only ScheduledExecution
};

// =============================================================================

struct LoggingUtilities {
    static std::string fmi3Status_toString(fmi3Status status) {
        switch (status) {
            case fmi3Status::fmi3Discard:
                return "Discard";
                break;
            case fmi3Status::fmi3Error:
                return "Error";
                break;
            case fmi3Status::fmi3Fatal:
                return "Fatal";
                break;
            case fmi3Status::fmi3OK:
                return "OK";
                break;
            case fmi3Status::fmi3Warning:
                return "Warning";
                break;
            default:
                throw std::runtime_error("Wrong fmi3Status");
                break;
        }
    }

    static void logger_default(fmi3InstanceEnvironment c, fmi3Status status, fmi3String category, fmi3String message) {
        if (!category)
            category = "?";
        printf("[%s] %s: %s", fmi3Status_toString(status).c_str(), category, message);
    }
};

// =============================================================================

/// Implementation of an FMU variable.
/// Objects of this type are created during:
/// - FMU export (and encoded in the model description XML)
/// - FMU import (retrieved from the model description XML)
class FmuVariable {
  public:
    enum class Type {
        Float32 = 0,
        Float64 = 1,
        Int8 = 2,
        UInt8 = 3,
        Int16 = 4,
        UInt16 = 5,
        Int32 = 6,
        UInt32 = 7,
        Int64 = 8,
        UInt64 = 9,
        Boolean = 10,
        String = 11,
        Binary = 12,
        Unknown = 13
    };

    using DimensionsArrayType = std::vector<std::pair<std::uint64_t, bool>>;

    enum class CausalityType { structuralParameter, parameter, calculatedParameter, input, output, local, independent };

    enum class VariabilityType { constant, fixed, tunable, discrete, continuous };

    enum class InitialType { automatic, none, exact, approx, calculated };

    FmuVariable() : FmuVariable("", FmuVariable::Type::Float64) {}

    FmuVariable(const std::string& name,
                FmuVariable::Type type,
                const DimensionsArrayType& _dimensions = DimensionsArrayType(),
                CausalityType causality = CausalityType::local,
                VariabilityType variability = VariabilityType::continuous,
                InitialType initial = InitialType::automatic)
        : m_name(name),
          m_valueReference(0),
          m_unitname("1"),
          m_type(type),
          m_dimensions(_dimensions),
          m_causality(causality),
          m_variability(variability),
          m_initial(initial),
          m_description("") {
        // Readibility replacements
        bool c_structural = (m_causality == CausalityType::structuralParameter);
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

        // Check on 'initial' attribute (see Table 22 in FMI3.0 specification)
        // - if initital == InitialType::automatic: automatically sets the "initial" property to its default value
        // - if initital != InitialType::automatic: checks if the "initial" property is feasible for the current
        // variable
        //
        // (A)
        if (                                                              //
            ((v_constant) && (c_output || c_local)) ||                    //
            ((v_fixed || v_tunable) && (c_structural || c_parameter)) ||  //
            ((v_discrete || v_continuous) && (c_input))                   //
        ) {
            if (m_initial == InitialType::automatic)
                m_initial = InitialType::exact;
            else if (m_initial != InitialType::exact)
                throw std::runtime_error("'initial' attribute for variable '" + name + "' not set properly.");
        }
        // (B)
        else if ((v_fixed || v_tunable) && (c_calculated || c_local)) {
            if (m_initial == InitialType::automatic)
                m_initial = InitialType::calculated;
            else if (m_initial != InitialType::approx && m_initial != InitialType::calculated)
                throw std::runtime_error("'initial' attribute for variable '" + name + "' not set properly.");
        }
        // (C)
        else if ((v_discrete || v_continuous) && (c_output || c_local)) {
            if (m_initial == InitialType::automatic)
                m_initial = InitialType::calculated;
            else if (m_initial != InitialType::approx && m_initial != InitialType::calculated &&
                     m_initial != InitialType::exact)
                throw std::runtime_error("'initial' attribute for variable '" + name + "' not set properly.");
        } else if (m_initial == InitialType::automatic)
            m_initial = InitialType::none;
        else if (m_initial != InitialType::none) {
            throw std::runtime_error("'initial' attribute for variable '" + name +
                                     "' can be set to 'automatic' or 'none' only.");
        }

        assert(m_initial != InitialType::automatic);

        // Incompatible variability/causality settings (see Tables 18 and 19 of the FMI3.0 specification)
        // (a)
        if (v_constant && (c_structural || c_parameter || c_calculated || c_input))
            throw std::runtime_error("Variable '" + name +
                                     "': constants always have their value already set, thus their causality can be "
                                     "only 'output' or 'local'.");
        // (b)
        if ((v_discrete || v_continuous) && (c_structural || c_parameter || c_calculated))
            throw std::runtime_error("Variable '" + name +
                                     "': structuralParameters, parameters and calculatedParameters cannot be discrete "
                                     "nor continuous, as they "
                                     "do not change over time.");
        // (c)
        if (c_independent && !v_continuous)
            throw std::runtime_error("Variable '" + name +
                                     "': for an 'independent' variable only variability = 'continuous' makes sense.");
        // (d) + (e)
        if (c_input && (v_fixed || v_tunable))
            throw std::runtime_error(
                "Variable '" + name +
                "': a fixed or tunable 'input'|'output' have exactly the same properties as a fixed or tunable "
                "parameter. "
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
        m_dimensions = other.m_dimensions;
    }

    // Copy assignment operator
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
        m_dimensions = other.m_dimensions;

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

    /// Check if setting this variable is allowed given the current FMU state.
    bool IsSetAllowed(FmuMachineState fmu_machine_state) const {
        //// RADU TODO: additional checks for configurationMode state?

        bool is_groupA =
            m_variability != FmuVariable::VariabilityType::constant &&
            (m_initial == FmuVariable::InitialType::exact || m_initial == FmuVariable::InitialType::approx);
        bool is_groupB =
            m_variability != FmuVariable::VariabilityType::constant && m_initial == FmuVariable::InitialType::exact;
        bool is_groupC = m_causality == FmuVariable::CausalityType::input ||
                         (m_causality == FmuVariable::CausalityType::parameter &&
                          m_variability == FmuVariable::VariabilityType::tunable);
        bool is_groupD = m_causality == FmuVariable::CausalityType::structuralParameter &&
                         (m_variability == FmuVariable::VariabilityType::fixed ||
                          m_variability == FmuVariable::VariabilityType::tunable);
        bool is_groupE = m_causality == FmuVariable::CausalityType::structuralParameter &&
                         m_variability == FmuVariable::VariabilityType::tunable;
        bool is_groupF = m_causality == FmuVariable::CausalityType::input &&
                         m_variability == FmuVariable::VariabilityType::continuous;
        bool is_groupG =
            m_causality == FmuVariable::CausalityType::input &&
            m_variability != FmuVariable::VariabilityType::discrete &&
            m_intermediateUpdate == true;  // the variable must be contained in requiredIntermediateVariables

        switch (fmu_machine_state) {
            case FmuMachineState::instantiated:
                return is_groupA;
                break;
            case FmuMachineState::initializationMode:
                return is_groupB;
                break;
            case FmuMachineState::eventMode:
                return is_groupC;
                break;
            case FmuMachineState::terminated:
                return false;
                break;
            case FmuMachineState::stepMode:
                return is_groupC;
                break;
            case FmuMachineState::intermediateUpdateMode:
                return is_groupG;
                break;
            case FmuMachineState::continuousTimeMode:
                return is_groupF;
                break;
            case FmuMachineState::configurationMode:
                return is_groupD;
                break;
            case FmuMachineState::reconfigurationMode:
                return is_groupE;
                break;
            case FmuMachineState::clockActivationMode:
                return false;
                break;
            case FmuMachineState::clockUpdateMode:
                return false;
                break;
            default:
                throw std::runtime_error("IsSetAllowed: received bad state.");
                break;
        }
    }

    /// Return a string with the name of the specified FMU variable type.
    /// Return a string with the name of the specified FMU variable type.
    static std::string Type_toString(Type type) {
        switch (type) {
            case Type::Float32:
                return "Float32";
                break;
            case Type::Float64:
                return "Float64";
                break;
            case Type::Int8:
                return "Int8";
                break;
            case Type::Int16:
                return "Int16";
                break;
            case Type::Int32:
                return "Int32";
                break;
            case Type::Int64:
                return "Int64";
                break;
            case Type::UInt8:
                return "UInt8";
                break;
            case Type::UInt16:
                return "UInt16";
                break;
            case Type::UInt32:
                return "UInt32";
                break;
            case Type::UInt64:
                return "UInt64";
                break;
            case Type::Boolean:
                return "Boolean";
                break;
            case Type::String:
                return "String";
                break;
            case Type::Binary:
                return "Unknown";
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
    const inline fmi3ValueReference GetValueReference() const { return m_valueReference; }
    void SetValueReference(fmi3ValueReference valref) { m_valueReference = valref; }
    const inline std::string& GetUnitName() const { return m_unitname; }
    void SetUnitName(const std::string& unitname) { m_unitname = unitname; }
    Type GetType() const { return m_type; }

    /// [INTERNAL] Return the dimensions array.
    /// WARNING: the dimension array might be empty in case of scalar arrays.
    /// This method is intended for internal use only.
    /// In order to retrieve a more user-friendly representation of the dimensions, use
    /// FmuComponentBase::GetVariableDimensions().
    DimensionsArrayType GetDimensions() const { return m_dimensions; }

    /// Return true is this variable is a scalar (i.e., has dimension 1).
    bool IsScalar() const { return m_dimensions.empty(); }

    /// Try to retrieve the size of the variable.
    /// Returns true if the size can be retrieved (i.e. the dimensions are fixed and not given by other variables),
    /// false otherwise.
    bool GetSize(size_t& size) const {
        if (m_dimensions.empty()) {
            size = 1;
            return true;
        }

        size = 1;
        for (const auto& dim : m_dimensions) {
            if (dim.second) {
                size *= dim.first;
            } else {
                return false;
            }
        }

        return size;
    }

  protected:
    Type m_type = Type::Unknown;          ///< variable type
    std::string m_name;                   ///< variable name
    fmi3ValueReference m_valueReference;  ///< reference among variables of same type
    std::string m_unitname;               ///< variable units
    CausalityType m_causality;            ///< variable causality
    VariabilityType m_variability;        ///< variable variability
    InitialType m_initial;                ///< type of initial value
    std::string m_description;            ///< description of this variable
    bool m_intermediateUpdate = false;    ///< TODO: if true, this variable is updated at intermediate steps

    /// List of pairs (size, fixed) for each dimension.
    /// - if m_dimensions[i].second == true (i.e. labelled as 'fixed') then 'size' is the actual size for that
    /// dimension;
    /// - if m_dimensions[i].second == false (i.e. labelled as not 'fixed') then 'size' provides an fmi3ValueReference
    /// to another variable that will provides the size of this variable;
    mutable DimensionsArrayType m_dimensions;
};

/// @} fmu_forge_fmi3

}  // namespace fmi3
}  // namespace fmu_forge

#endif
