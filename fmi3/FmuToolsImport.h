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
// Classes for loading, instantiating, and using FMUs (FMI 3.0)
// =============================================================================

#pragma once

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <cstdarg>
#include <iostream>
#include <system_error>
#include <algorithm>
#include <iterator>

#include "FmuToolsRuntimeLinking.h"
#include "FmuToolsImportCommon.h"
#include "fmi3/FmuToolsVariable.h"

namespace fmu_forge {
namespace fmi3 {

/// @addtogroup fmu_forge_fmi3
/// @{

#define LOAD_FMI_FUNCTION(funcName)                                                                    \
    this->_##funcName = (funcName##TYPE*)get_function_ptr(this->dynlib_handle, #funcName);             \
    if (!this->_##funcName)                                                                            \
        throw std::runtime_error(std::string(std::string("Could not find ") + std::string(#funcName) + \
                                             std::string(" in the FMU library. Wrong or outdated FMU?")));

// =============================================================================

/// Implementation of an FMU variable for import.
class FmuVariableImport : public FmuVariable {
  public:
    FmuVariableImport() : FmuVariableImport("", FmuVariable::Type::Float64) {}

    FmuVariableImport(const std::string& name,
                      FmuVariable::Type type,
                      const FmuVariable::DimensionsArrayType& dimensions = FmuVariable::DimensionsArrayType(),
                      CausalityType causality = CausalityType::local,
                      VariabilityType variability = VariabilityType::continuous,
                      InitialType initial = InitialType::automatic)
        : FmuVariable(name, type, dimensions, causality, variability, initial),
          m_is_state(false),
          m_is_deriv(false) {}

    bool IsState() const { return m_is_state; }
    bool IsDeriv() const { return m_is_deriv; }

  private:
    bool m_is_state;  ///< true if this is a state variable
    bool m_is_deriv;  ///< true if this is a state derivative variable

    friend class FmuUnit;
};

// =============================================================================

/// Class for a node in a tree of FMU variables.
/// The tree is constructed by analyzing the flat list of variables in the XML.
/// In the XML one has the flattened list such as for example
///     myobject.mysubobjetct.pos
///     myobject.mysubobjetct.dir
/// so the tree will contain:
///     myobject
///          mysubobject
///                pos
///                dir
class FmuVariableTreeNode {
  public:
    std::string object_name;

    std::map<std::string, FmuVariableTreeNode> children;
    FmuVariableImport* leaf = nullptr;

    FmuVariableTreeNode() {}
};

// =============================================================================

/// Class for managing an impoerted FMU.
/// Provides functions to parse the model description XML file, load the shared library in run-time, set/get variables,
/// and invoke FMI functions on the FMU.
class FmuUnit {
  public:
    // since FMI3.0 has unique varRefs we can use them for indexing instead of strings
    typedef std::map<fmi3ValueReference, FmuVariableImport> VarList;

    FmuUnit();
    virtual ~FmuUnit() {}

    /// Enable/disable verbose messages during FMU loading.
    void SetVerbose(bool verbose) { m_verbose = verbose; }

    /// Load the FMU, optionally defining where the FMU will be unzipped (default is the temporary folder).
    void Load(FmuType fmuType,
              const std::string& fmupath,
              const std::string& unzipdir = fs::temp_directory_path().generic_string() + std::string("/_fmu_temp"));

    /// Load the FMU from the specified directory, assuming it has been already unzipped.
    virtual void LoadUnzipped(FmuType fmuType, const std::string& directory);

    /// Return the folder in which the FMU has been unzipped.
    std::string GetUnzippedFolder() const { return m_directory; }

    /// Return version number of header files.
    std::string GetVersion() const;

    /// Return the number of state variables.
    size_t GetNumStates() const { return m_nx; }

    /// Instantiate the model.
    void Instantiate(const std::string& instanceName,
                     const std::string& resource_dir,
                     bool logging = false,
                     bool visible = false);

    /// Instantiate the model, setting as resources folder the one from the unzipped FMU.
    void Instantiate(const std::string& instanceName, bool logging = false, bool visible = false);

    /// Get the list of FMU variables.
    const VarList& GetVariablesList() const { return m_variables; }

    /// Get the value reference of a variable from its name.
    /// Throws an exception if the variable is not found.
    fmi3ValueReference GetValueReference(const std::string& varname) const {
        for (auto& var : m_variables) {
            if (var.second.GetName() == varname)
                return var.first;
        }
        throw std::runtime_error("Variable not found: " + varname);
    }

    /// Get the value reference of a variable from its name.
    /// Return true if the variable is found, false otherwise. No throw.
    bool GetValueReference(const std::string& varname, fmi3ValueReference& valueref) const noexcept(true) {
        for (auto& var : m_variables) {
            if (var.second.GetName() == varname) {
                valueref = var.first;
                return true;
            }
        }
        return false;
    }

    /// Print the tree of variables
    void PrintVariablesTree(int tab) { PrintVariablesTree(&tree_variables, tab); }

    /// Set debug logging level.
    fmi3Status SetDebugLogging(fmi3Boolean loggingOn, const std::vector<std::string>& logCategories);

    /// Initialize the experiment.
    fmi3Status EnterInitializationMode(fmi3Boolean toleranceDefined,
                                       fmi3Float64 tolerance,
                                       fmi3Float64 startTime,
                                       fmi3Boolean stopTimeDefined,
                                       fmi3Float64 stopTime);

    fmi3Status ExitInitializationMode();

    /// Advance state of the FMU from currentCommunicationPoint to currentCommunicationPoint+communicationStepSize.
    /// Available only for an FMU that implements the Co-Simulation interface.
    fmi3Status DoStep(fmi3Float64 currentCommunicationPoint,
                      fmi3Float64 communicationStepSize,
                      fmi3Boolean noSetFMUStatePriorToCurrentPoint);

    /// Set a new time instant and re-initialize caching of variables that depend on time.
    /// Available only for an FMU that implements the Model Exchange interface.
    fmi3Status SetTime(const fmi3Float64 time);

    /// Get the (continuous) state vector.
    fmi3Status GetContinuousStates(fmi3Float64 x[], size_t nx);

    /// Set a new (continuous) state vector and re-initialize caching of variables that depend on the states. Argument
    /// nx is the length of vector x and is provided for checking purposes.
    /// Available only for an FMU that iplements the Model Exchange interface.
    fmi3Status SetContinuousStates(const fmi3Float64 x[], size_t nx);

    /// Compute state derivatives at the current time instant and for the current states. The derivatives are returned
    /// as a vector with nx elements.
    /// Available only for an FMU that iplements the Model Exchange interface.
    fmi3Status GetContinuousStateDerivatives(fmi3Float64 derivatives[], size_t nx);

    /// Get the current dimensions of a variable.
    std::vector<size_t> GetVariableDimensions(const FmuVariable& var) const {
        auto dim = var.GetDimensions();

        // case of scalar variable
        if (dim.size() == 0)
            return std::vector<size_t>({1});

        std::vector<size_t> sizes;
        for (const auto& d : dim) {
            if (d.second == true)
                // the size of the current dimension is fixed
                sizes.push_back(d.first);
            else {
                // the size of the current dimension is given by another variable
                size_t cur_size;
                GetVariable(static_cast<fmi3ValueReference>(d.first), cur_size);
                sizes.push_back(cur_size);
            }
        }

        return sizes;
    }

    std::vector<size_t> GetVariableDimensions(fmi3ValueReference valref) const {
        return GetVariableDimensions(m_variables.at(valref));
    }
    /// Get the current total size of a variable.
    size_t GetVariableSize(const FmuVariable& var) const {
        if (var.GetDimensions().size() == 0)
            return 1;

        size_t size = 1;
        for (const auto& dim : GetVariableDimensions(var)) {
            size *= dim;
        }
        return size;
    };

    size_t GetVariableSize(fmi3ValueReference valref) const { return GetVariableSize(m_variables.at(valref)); }

    /// Set the value of a variable.
    /// Values will be fetched from 'values' assuming its dimensions, memory alignment and allocation are according
    /// to FMI standard.
    template <class T>
    fmi3Status SetVariable(fmi3ValueReference vr, const T& values, size_t nValues = 0) noexcept(false);

    template <class T>
    fmi3Status SetVariable(fmi3ValueReference vr, const std::vector<T>& values_vect) noexcept(false);

    fmi3Status SetVariable(fmi3ValueReference vr, const std::vector<fmi3Byte>& values) noexcept(false);

    /// Set the value of a scalar fmi3Binary variable.
    fmi3Status SetVariable(fmi3ValueReference vr, fmi3Binary& value, size_t valueSize) noexcept(false);

    fmi3Status SetVariable(fmi3ValueReference vr,
                           const std::vector<fmi3Binary>& values_vect,
                           const std::vector<size_t>& valueSizes) noexcept(false);

    fmi3Status SetVariable(fmi3ValueReference vr,
                           const std::vector<std::vector<fmi3Byte>>& values_vect) noexcept(false);

    fmi3Status SetVariable(fmi3ValueReference vr, const std::vector<fmi3String>& values_vect) noexcept(false);

    fmi3Status SetVariable(fmi3ValueReference vr, const std::vector<std::string>& values_vect) noexcept(false);

    /// Get the value of a variable.
    /// The 'values' pointer could either point to single scalar variable or to an array of variables.
    /// In this latter case, the array should be pre-allocated with size given by GetVariableSize(); at this point, such
    /// value can also be passed as 'nValues' so to avoid a second recomputation.
    /// In order to simplify the process, an alternative overload of this function is provided that accepts
    /// std::vector<T> as input that is automatically resized.
    ///
    /// A special treatment is dedicated to fmi3Binary variables (i.e. const fmi3Byte*). These variables in their scalar
    /// form (i.e. without any Dimension XML node) have indeed a nValues = 1. However, since they are storing a pointer,
    /// this latter turns out to point to an array of bytes whose size is not exposed explicitely in the
    /// modelDescription.xml. Its size is indeed retrieved by a preliminar call to fmi3GetBinary. This call will
    /// fill the the valueSizes[0] array with the size of the underlying array of bytes.
    /// If the fmi3Binary variable is an array, then the nValues will be the number of elements in the array and
    /// valueSizes[i] will contain the size of the i-th element of the fmi3Binary array. The internal implementation of
    /// a fmi3Binary data is up to the FMU developer, but it surely needs to store both a pointer to the data and the
    /// size of the data. Because of this, a smart choice is to internally store the data of a single fmi3Binary as a
    /// std::vector<fmi3Byte>. Since this might be common also for the FMU user a dedicated overload of this function
    /// supports this type natively. However, an std::vector<fmi3Byte>, even if it resembles other arrays like
    /// std::vector<double>, it has not to be considered an array.
    /// Finally, scalar fmi3Binary could be in the form:
    /// - [C]   fmi3Binary, the official way, passed as fmi3Binary* to GetVariable(fmi3ValueReference, T*, size_t)
    /// - [C++] std::vector<fmi3Byte>, passed to GetVariable(fmi3ValueReference, std::vector<T>&, size_t)
    /// Arrays of fmi3Binary could be in the form:
    /// - [C  |C  ] fmi3Binary[] (e.g. const fmi3Byte**) is the official C-like case, treated by
    /// GetVariable(fmi3ValueReference, T*, size_t)
    /// - [C++|C++] std::vector<std::vector<fmi3Byte>> treated by GetVariable(fmi3ValueReference,
    /// std::vector<std::vector<fmi3Byte>>&, size_t)
    /// - [C++|C  ] std::vector<fmi3Binary> treated by GetVariable(fmi3ValueReference, std::vector<T>&, size_t)
    /// The case of std::vector<fmi3Byte>[] is a very special case that is not supported by this function.
    ///
    /// A second special case is the one of fmi3String. As for fmi3Binary, the fmi3String returned from this function is
    /// just the pointer to the head of the fmi3String array, but no copy happened. The size is inferred by the
    /// null-termination of the string.
    template <class T>
    fmi3Status GetVariable(fmi3ValueReference vr, T& values, size_t nValues = 0) const noexcept(false);

    /// Get the value of the array of variables.
    template <class T>
    fmi3Status GetVariable(fmi3ValueReference vr, std::vector<T>& values_vect) const noexcept(false);

    /// Get the value from a scalar fmi3Binary variable in the shape of a std::vector<fmi3Byte>.
    /// The vector will be automatically resized according to variable size.
    fmi3Status GetVariable(fmi3ValueReference vr, std::vector<fmi3Byte>& values) const noexcept(false);

    /// Get the location of a scalar fmi3Binary variable.
    /// After the call to this function no copy happened.
    /// The 'value' variable has just been filled with the pointer to the data of the fmi3Binary variable and
    /// 'valueSize' with the size of the scalar variable. The user must then allocate space for 'valueSize' element and
    /// parse their values from the FMU starting from the pointer 'value'.
    fmi3Status GetVariable(fmi3ValueReference vr, fmi3Binary& value, size_t& valueSize) const noexcept(false);

    /// Get the location of all the elements of a fmi3Binary array.
    /// After the call to this function no copy happened.
    /// The 'values' vector has just been filled with the pointer to the data of the fmi3Binary array and 'valueSizes'
    /// with the size of each element of the array. The user must then allocate appropriate space for each of the
    /// elements (valueSizes[i] contains the size of the i-th element) and copy the data from the FMU.
    fmi3Status GetVariable(fmi3ValueReference vr,
                           std::vector<fmi3Binary>& values_vect,
                           std::vector<size_t>& valueSizes) const noexcept(false);

    /// Get the value from an array of fmi3Binary variable in the shape of a std::vector<std::vector<fmi3Byte>>.
    fmi3Status GetVariable(fmi3ValueReference vr, std::vector<std::vector<fmi3Byte>>& values_vect) const
        noexcept(false);

    /// Get the location of an array of fmi3String variables in the shape of a std::vector<fmi3String>.
    /// The function will return in 'values_vect' the pointer to the fmi3String arrays. The user must then allocate
    /// appropriate space for each of the elements and retrieve the value from the pointer. This can be done manually by
    /// querying the length of the string with std::strlen or by using the std::string constructor to automatically get
    /// an std::string of the proper size. Please mind that, for the latter case, is more convenient to use
    /// GetVariable(fmi3ValueReference, std::vector<std::string>&) directly.
    fmi3Status GetVariable(fmi3ValueReference vr, std::vector<fmi3String>& values_vect) const noexcept(false);

    /// Get the value from an array of fmi3String variable in the shape of an std::vector<std::string>.
    fmi3Status GetVariable(fmi3ValueReference vr, std::vector<std::string>& values_vect) const noexcept(false);

    template <typename... Args>
    fmi3Status SetVariable(const std::string& str, Args&&... args) {
        fmi3ValueReference vr = GetValueReference(str);
        return SetVariable(vr, std::forward<Args>(args)...);
    }

    template <typename... Args>
    fmi3Status GetVariable(const std::string& str, Args&&... args) const {
        fmi3ValueReference vr = GetValueReference(str);
        return GetVariable(vr, std::forward<Args>(args)...);
    }

  protected:
    std::string modelName;
    std::string instantiationToken;
    std::string fmiVersion;
    std::string description;
    std::string generationTool;
    std::string generationDateAndTime;
    std::string variableNamingConvention;
    std::string numberOfEventIndicators;

    bool has_cosimulation;
    std::string info_cosim_modelIdentifier;
    std::string info_cosim_needsExecutionTool;
    std::string info_cosim_canHandleVariableCommunicationStepSize;
    std::string info_cosim_canInterpolateInputs;
    std::string info_cosim_maxOutputDerivativeOrder;
    std::string info_cosim_canRunAsynchronuously;
    std::string info_cosim_canBeInstantiatedOnlyOncePerProcess;
    std::string info_cosim_canNotUseMemoryManagementFunctions;
    std::string info_cosim_canGetAndSetFMUstate;
    std::string info_cosim_canSerializeFMUstate;

    bool has_model_exchange;
    std::string info_modex_modelIdentifier;
    std::string info_modex_needsExecutionTool;
    std::string info_modex_completedIntegratorStepNotNeeded;
    std::string info_modex_canBeInstantiatedOnlyOncePerProcess;
    std::string info_modex_canNotUseMemoryManagementFunctions;
    std::string info_modex_canGetAndSetFMUState;
    std::string info_modex_canSerializeFMUstate;
    std::string info_modex_providesDirectionalDerivative;

    bool has_scheduled_execution;

    VarList m_variables;  ///< FMU variables

    FmuVariableTreeNode tree_variables;

  public:
    fmi3LogMessageCallback log_message_callback;
    fmi3ClockUpdateCallback clock_update_callback;
    fmi3IntermediateUpdateCallback intermediate_update_callback;
    fmi3LockPreemptionCallback lock_preemption_callback;
    fmi3UnlockPreemptionCallback unlock_preemption_callback;

    fmi3Instance instance;

    fmi3GetVersionTYPE* _fmi3GetVersion;
    fmi3SetDebugLoggingTYPE* _fmi3SetDebugLogging;
    fmi3InstantiateModelExchangeTYPE* _fmi3InstantiateModelExchange;
    fmi3InstantiateCoSimulationTYPE* _fmi3InstantiateCoSimulation;
    fmi3InstantiateScheduledExecutionTYPE* _fmi3InstantiateScheduledExecution;
    fmi3FreeInstanceTYPE* _fmi3FreeInstance;
    fmi3EnterInitializationModeTYPE* _fmi3EnterInitializationMode;
    fmi3ExitInitializationModeTYPE* _fmi3ExitInitializationMode;
    fmi3EnterEventModeTYPE* _fmi3EnterEventMode;
    fmi3TerminateTYPE* _fmi3Terminate;
    fmi3ResetTYPE* _fmi3Reset;
    fmi3GetFloat32TYPE* _fmi3GetFloat32;
    fmi3GetFloat64TYPE* _fmi3GetFloat64;
    fmi3GetInt8TYPE* _fmi3GetInt8;
    fmi3GetUInt8TYPE* _fmi3GetUInt8;
    fmi3GetInt16TYPE* _fmi3GetInt16;
    fmi3GetUInt16TYPE* _fmi3GetUInt16;
    fmi3GetInt32TYPE* _fmi3GetInt32;
    fmi3GetUInt32TYPE* _fmi3GetUInt32;
    fmi3GetInt64TYPE* _fmi3GetInt64;
    fmi3GetUInt64TYPE* _fmi3GetUInt64;
    fmi3GetBooleanTYPE* _fmi3GetBoolean;
    fmi3GetStringTYPE* _fmi3GetString;
    fmi3GetBinaryTYPE* _fmi3GetBinary;
    fmi3GetClockTYPE* _fmi3GetClock;
    fmi3SetFloat32TYPE* _fmi3SetFloat32;
    fmi3SetFloat64TYPE* _fmi3SetFloat64;
    fmi3SetInt8TYPE* _fmi3SetInt8;
    fmi3SetUInt8TYPE* _fmi3SetUInt8;
    fmi3SetInt16TYPE* _fmi3SetInt16;
    fmi3SetUInt16TYPE* _fmi3SetUInt16;
    fmi3SetInt32TYPE* _fmi3SetInt32;
    fmi3SetUInt32TYPE* _fmi3SetUInt32;
    fmi3SetInt64TYPE* _fmi3SetInt64;
    fmi3SetUInt64TYPE* _fmi3SetUInt64;
    fmi3SetBooleanTYPE* _fmi3SetBoolean;
    fmi3SetStringTYPE* _fmi3SetString;
    fmi3SetBinaryTYPE* _fmi3SetBinary;
    fmi3SetClockTYPE* _fmi3SetClock;
    fmi3GetNumberOfVariableDependenciesTYPE* _fmi3GetNumberOfVariableDependencies;
    fmi3GetVariableDependenciesTYPE* _fmi3GetVariableDependencies;
    fmi3GetFMUStateTYPE* _fmi3GetFMUState;
    fmi3SetFMUStateTYPE* _fmi3SetFMUState;
    fmi3FreeFMUStateTYPE* _fmi3FreeFMUState;
    fmi3SerializedFMUStateSizeTYPE* _fmi3SerializedFMUStateSize;
    fmi3SerializeFMUStateTYPE* _fmi3SerializeFMUState;
    fmi3DeserializeFMUStateTYPE* _fmi3DeserializeFMUState;
    fmi3GetDirectionalDerivativeTYPE* _fmi3GetDirectionalDerivative;
    fmi3GetAdjointDerivativeTYPE* _fmi3GetAdjointDerivative;
    fmi3EnterConfigurationModeTYPE* _fmi3EnterConfigurationMode;
    fmi3ExitConfigurationModeTYPE* _fmi3ExitConfigurationMode;
    fmi3GetIntervalDecimalTYPE* _fmi3GetIntervalDecimal;
    fmi3GetIntervalFractionTYPE* _fmi3GetIntervalFraction;
    fmi3GetShiftDecimalTYPE* _fmi3GetShiftDecimal;
    fmi3GetShiftFractionTYPE* _fmi3GetShiftFraction;
    fmi3SetIntervalDecimalTYPE* _fmi3SetIntervalDecimal;
    fmi3SetIntervalFractionTYPE* _fmi3SetIntervalFraction;
    fmi3SetShiftDecimalTYPE* _fmi3SetShiftDecimal;
    fmi3SetShiftFractionTYPE* _fmi3SetShiftFraction;
    fmi3EvaluateDiscreteStatesTYPE* _fmi3EvaluateDiscreteStates;
    fmi3UpdateDiscreteStatesTYPE* _fmi3UpdateDiscreteStates;
    fmi3EnterContinuousTimeModeTYPE* _fmi3EnterContinuousTimeMode;
    fmi3CompletedIntegratorStepTYPE* _fmi3CompletedIntegratorStep;
    fmi3SetTimeTYPE* _fmi3SetTime;
    fmi3SetContinuousStatesTYPE* _fmi3SetContinuousStates;
    fmi3GetContinuousStateDerivativesTYPE* _fmi3GetContinuousStateDerivatives;
    fmi3GetEventIndicatorsTYPE* _fmi3GetEventIndicators;
    fmi3GetContinuousStatesTYPE* _fmi3GetContinuousStates;
    fmi3GetNominalsOfContinuousStatesTYPE* _fmi3GetNominalsOfContinuousStates;
    fmi3GetNumberOfEventIndicatorsTYPE* _fmi3GetNumberOfEventIndicators;
    fmi3GetNumberOfContinuousStatesTYPE* _fmi3GetNumberOfContinuousStates;
    fmi3EnterStepModeTYPE* _fmi3EnterStepMode;
    fmi3GetOutputDerivativesTYPE* _fmi3GetOutputDerivatives;
    fmi3DoStepTYPE* _fmi3DoStep;
    fmi3ActivateModelPartitionTYPE* _fmi3ActivateModelPartition;

  private:
    /// Parse XML and create the list of variables.
    void LoadXML();

    /// Load the shared library in run-time and do the dynamic linking to the required FMU functions.
    void LoadSharedLibrary(FmuType fmuType);

    /// Construct a tree of variables from the flat variable list.
    void BuildVariablesTree();

    /// Print the tree of variables (recursive).
    void PrintVariablesTree(FmuVariableTreeNode* mynode, int tab);

    std::string m_directory;
    std::string m_bin_directory;

    FmuType m_fmuType;
    bool m_verbose;

    size_t m_nx;  ///< number of state variables

    DYNLIB_HANDLE dynlib_handle;
};

// -----------------------------------------------------------------------------

bool areStringsEqual(const char* str, size_t strSize, const char* fixedString) {
    size_t fixedStringLength = std::strlen(fixedString);

    // Determine the minimum length to compare
    size_t minLength = std::min(fixedStringLength, strSize);

    // Compare the minimum length of characters
    if (std::strncmp(str, fixedString, minLength) == 0 && strSize == fixedStringLength) {
        return true;
    } else {
        return false;
    }
}

FmuUnit::FmuUnit() : has_cosimulation(false), has_model_exchange(false), m_nx(0), m_verbose(false) {
    // default binaries directory in FMU unzipped directory
    m_bin_directory = "/binaries/" + std::string(FMI3_PLATFORM);
}

void FmuUnit::Load(FmuType fmuType, const std::string& filepath, const std::string& unzipdir) {
    if (m_verbose) {
        std::cout << "Unzipping FMU: " << filepath << std::endl;
        std::cout << "           in: " << unzipdir << std::endl;
    }

    UnzipFmu(filepath, unzipdir);

    try {
        LoadUnzipped(fmuType, unzipdir);
    } catch (std::exception&) {
        throw;
    }
}

void FmuUnit::LoadUnzipped(FmuType fmuType, const std::string& directory) {
    m_fmuType = fmuType;
    m_directory = directory;

    try {
        LoadXML();
    } catch (std::exception&) {
        throw;
    }

    if (fmuType == FmuType::COSIMULATION && !has_cosimulation)
        throw std::runtime_error("Attempting to load Co-Simulation FMU, but not a CS FMU.");
    if (fmuType == FmuType::MODEL_EXCHANGE && !has_model_exchange)
        throw std::runtime_error("Attempting to load as Model Exchange, but not an ME FMU.");
    if (fmuType == FmuType::SCHEDULED_EXECUTION && !has_scheduled_execution)
        throw std::runtime_error("Attempting to load as Scheduled Execution, but not an SE FMU.");

    try {
        LoadSharedLibrary(fmuType);
    } catch (std::exception&) {
        throw;
    }

    BuildVariablesTree();
}

void FmuUnit::LoadXML() {
    std::string xml_filename = m_directory + "/modelDescription.xml";
    if (m_verbose)
        std::cout << "Loading model description file: " << xml_filename << std::endl;

    rapidxml::xml_document<>* doc_ptr = new rapidxml::xml_document<>();

    // Read the xml file into a vector
    std::ifstream file(xml_filename);

    if (!file.good())
        throw std::runtime_error("Cannot find file: " + xml_filename + "\n");

    std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    buffer.push_back('\0');

    // Parse the buffer using the xml file parsing library into doc
    doc_ptr->parse<0>(&buffer[0]);

    // Find the root node
    auto root_node = doc_ptr->first_node("fmiModelDescription");
    if (!root_node)
        throw std::runtime_error("Not a valid FMU. Missing <fmiModelDescription> in XML. \n");

    if (auto attr = root_node->first_attribute("modelName")) {
        modelName = attr->value();
    }
    if (auto attr = root_node->first_attribute("instantiationToken")) {
        instantiationToken = attr->value();
    }
    if (auto attr = root_node->first_attribute("fmiVersion")) {
        fmiVersion = attr->value();
    }
    if (auto attr = root_node->first_attribute("description")) {
        description = attr->value();
    }
    if (auto attr = root_node->first_attribute("generationTool")) {
        generationTool = attr->value();
    }
    if (auto attr = root_node->first_attribute("generationDateAndTime")) {
        generationDateAndTime = attr->value();
    }
    if (auto attr = root_node->first_attribute("variableNamingConvention")) {
        variableNamingConvention = attr->value();
    }
    if (auto attr = root_node->first_attribute("numberOfEventIndicators")) {
        numberOfEventIndicators = attr->value();
    }

    if (fmiVersion.compare("3.0") != 0)
        throw std::runtime_error("Not an FMI 3.0 FMU");

    // Find the cosimulation node
    auto cosimulation_node = root_node->first_node("CoSimulation");
    if (cosimulation_node) {
        if (auto attr = cosimulation_node->first_attribute("modelIdentifier")) {
            info_cosim_modelIdentifier = attr->value();
        }
        if (auto attr = cosimulation_node->first_attribute("needsExecutionTool")) {
            info_cosim_needsExecutionTool = attr->value();
        }
        if (auto attr = cosimulation_node->first_attribute("canHandleVariableCommunicationStepSize")) {
            info_cosim_canHandleVariableCommunicationStepSize = attr->value();
        }
        if (auto attr = cosimulation_node->first_attribute("canInterpolateInputs")) {
            info_cosim_canInterpolateInputs = attr->value();
        }
        if (auto attr = cosimulation_node->first_attribute("maxOutputDerivativeOrder")) {
            info_cosim_maxOutputDerivativeOrder = attr->value();
        }
        if (auto attr = cosimulation_node->first_attribute("canRunAsynchronuously")) {
            info_cosim_canRunAsynchronuously = attr->value();
        }
        if (auto attr = cosimulation_node->first_attribute("canBeInstantiatedOnlyOncePerProcess")) {
            info_cosim_canBeInstantiatedOnlyOncePerProcess = attr->value();
        }
        if (auto attr = cosimulation_node->first_attribute("canNotUseMemoryManagementFunctions")) {
            info_cosim_canNotUseMemoryManagementFunctions = attr->value();
        }
        if (auto attr = cosimulation_node->first_attribute("canGetAndSetFMUstate")) {
            info_cosim_canGetAndSetFMUstate = attr->value();
        }
        if (auto attr = cosimulation_node->first_attribute("canSerializeFMUstate")) {
            info_cosim_canSerializeFMUstate = attr->value();
        }
        has_cosimulation = true;

        if (m_verbose)
            std::cout << "  Found CS interface" << std::endl;
    }

    // Find the model exchange node
    auto modelexchange_node = root_node->first_node("ModelExchange");
    if (modelexchange_node) {
        if (auto attr = modelexchange_node->first_attribute("modelIdentifier")) {
            info_modex_modelIdentifier = attr->value();
        }
        if (auto attr = modelexchange_node->first_attribute("needsExecutionTool")) {
            info_modex_needsExecutionTool = attr->value();
        }
        if (auto attr = modelexchange_node->first_attribute("completedIntegratorStepNotNeeded")) {
            info_modex_completedIntegratorStepNotNeeded = attr->value();
        }

        if (auto attr = modelexchange_node->first_attribute("canBeInstantiatedOnlyOncePerProcess")) {
            info_modex_canBeInstantiatedOnlyOncePerProcess = attr->value();
        }
        if (auto attr = modelexchange_node->first_attribute("canNotUseMemoryManagementFunctions")) {
            info_modex_canNotUseMemoryManagementFunctions = attr->value();
        }
        if (auto attr = modelexchange_node->first_attribute("canGetAndSetFMUState")) {
            info_modex_canGetAndSetFMUState = attr->value();
        }
        if (auto attr = modelexchange_node->first_attribute("canSerializeFMUstate")) {
            info_modex_canSerializeFMUstate = attr->value();
        }
        if (auto attr = modelexchange_node->first_attribute("providesDirectionalDerivative")) {
            info_modex_providesDirectionalDerivative = attr->value();
        }
        has_model_exchange = true;

        if (m_verbose)
            std::cout << "  Found ME interface" << std::endl;
    }

    if (!has_cosimulation && !has_model_exchange) {
        throw std::runtime_error(
            "Not a valid FMU. Missing <CoSimulation>, <ModelExchange> or <ScheduledExecution> in XML. \n");
    }

    // Find the variable container node
    auto variables_node = root_node->first_node("ModelVariables");
    if (!variables_node)
        throw std::runtime_error("Not a valid FMU. Missing <ModelVariables> in XML.");

    // Initialize variable value references and lists of state and state derivative variables
    std::vector<int> state_valref;
    std::vector<int> deriv_valref;

    // Iterate over the variable nodes and load container of FMU variables
    for (auto var_node = variables_node->first_node(); var_node; var_node = var_node->next_sibling()) {
        // Get variable name
        std::string var_name;

        if (auto attr = var_node->first_attribute("name"))
            var_name = attr->value();
        else
            throw std::runtime_error("Cannot find 'name' property in variable.");

        // Get variable attributes
        fmi3ValueReference valref = 0;
        std::string description = "";
        std::string variability = "";
        std::string causality = "";
        std::string initial = "";

        // valueReference is 1-based
        if (auto attr = var_node->first_attribute("valueReference"))
            valref = std::stoul(attr->value());
        else
            throw std::runtime_error("Cannot find 'valueReference' property in variable.");

        if (auto attr = var_node->first_attribute("description"))
            description = attr->value();

        if (auto attr = var_node->first_attribute("variability"))
            variability = attr->value();

        if (auto attr = var_node->first_attribute("causality"))
            causality = attr->value();

        if (auto attr = var_node->first_attribute("initial"))
            initial = attr->value();

        FmuVariable::CausalityType causality_enum;
        FmuVariable::VariabilityType variability_enum;
        FmuVariable::InitialType initial_enum;

        if (causality.empty())
            causality_enum = FmuVariable::CausalityType::local;
        else if (!causality.compare("parameter"))
            causality_enum = FmuVariable::CausalityType::parameter;
        else if (!causality.compare("calculatedParameter"))
            causality_enum = FmuVariable::CausalityType::calculatedParameter;
        else if (!causality.compare("input"))
            causality_enum = FmuVariable::CausalityType::input;
        else if (!causality.compare("output"))
            causality_enum = FmuVariable::CausalityType::output;
        else if (!causality.compare("local"))
            causality_enum = FmuVariable::CausalityType::local;
        else if (!causality.compare("independent"))
            causality_enum = FmuVariable::CausalityType::independent;
        else
            throw std::runtime_error("causality is badly formatted.");

        if (variability.empty())
            variability_enum = FmuVariable::VariabilityType::continuous;
        else if (!variability.compare("constant"))
            variability_enum = FmuVariable::VariabilityType::constant;
        else if (!variability.compare("fixed"))
            variability_enum = FmuVariable::VariabilityType::fixed;
        else if (!variability.compare("tunable"))
            variability_enum = FmuVariable::VariabilityType::tunable;
        else if (!variability.compare("discrete"))
            variability_enum = FmuVariable::VariabilityType::discrete;
        else if (!variability.compare("continuous"))
            variability_enum = FmuVariable::VariabilityType::continuous;
        else
            throw std::runtime_error("variability is badly formatted.");

        if (initial.empty())
            initial_enum = FmuVariable::InitialType::none;
        else if (!initial.compare("exact"))
            initial_enum = FmuVariable::InitialType::exact;
        else if (!initial.compare("approx"))
            initial_enum = FmuVariable::InitialType::approx;
        else if (!initial.compare("calculated"))
            initial_enum = FmuVariable::InitialType::calculated;
        else
            throw std::runtime_error("variability is badly formatted.");

        FmuVariable::DimensionsArrayType dimensions;
        for (auto node_dim = var_node->first_node("Dimension"); node_dim;
             node_dim = node_dim->next_sibling("Dimension")) {
            if (auto node_dim_startsize = node_dim->first_attribute("start")) {
                dimensions.push_back(std::make_pair(std::stoul(node_dim_startsize->value()), true));
            } else if (auto node_dim_vrsize = node_dim->first_attribute("valueReference")) {
                dimensions.push_back(std::make_pair(std::stoul(node_dim_vrsize->value()), false));
            } else
                throw std::runtime_error("Dimension must have either 'start' or 'valueReference' attribute.");
        }

        // Get variable type
        FmuVariable::Type var_type;

        if (areStringsEqual(var_node->name(), var_node->name_size(), "Float32")) {
            var_type = FmuVariable::Type::Float32;
        } else if (areStringsEqual(var_node->name(), var_node->name_size(), "Float64")) {
            var_type = FmuVariable::Type::Float64;
        } else if (areStringsEqual(var_node->name(), var_node->name_size(), "Int8")) {
            var_type = FmuVariable::Type::Int8;
        } else if (areStringsEqual(var_node->name(), var_node->name_size(), "UInt8")) {
            var_type = FmuVariable::Type::UInt8;
        } else if (areStringsEqual(var_node->name(), var_node->name_size(), "Int16")) {
            var_type = FmuVariable::Type::Int16;
        } else if (areStringsEqual(var_node->name(), var_node->name_size(), "UInt16")) {
            var_type = FmuVariable::Type::UInt16;
        } else if (areStringsEqual(var_node->name(), var_node->name_size(), "Int32")) {
            var_type = FmuVariable::Type::Int32;
        } else if (areStringsEqual(var_node->name(), var_node->name_size(), "UInt32")) {
            var_type = FmuVariable::Type::UInt32;
        } else if (areStringsEqual(var_node->name(), var_node->name_size(), "Int64")) {
            var_type = FmuVariable::Type::Int64;
        } else if (areStringsEqual(var_node->name(), var_node->name_size(), "UInt64")) {
            var_type = FmuVariable::Type::UInt64;
        } else if (areStringsEqual(var_node->name(), var_node->name_size(), "Boolean")) {
            var_type = FmuVariable::Type::Boolean;
        } else if (areStringsEqual(var_node->name(), var_node->name_size(), "String")) {
            var_type = FmuVariable::Type::String;
        } else if (areStringsEqual(var_node->name(), var_node->name_size(), "Binary")) {
            var_type = FmuVariable::Type::Binary;
        } else {
            std::cerr << "Unknown variable type: " + std::string(var_node->name()) << std::endl;
            throw std::runtime_error("Unknown variable type: " + std::string(var_node->name()));
        }

        // Check if variable is a derivative, check for unit and start value
        bool is_deriv = false;
        std::string unit = "";

        if (auto attr = var_node->first_attribute("derivative")) {
            state_valref.push_back(std::stoul(attr->value()));
            deriv_valref.push_back(valref);
            is_deriv = true;
        }
        if (auto attr = var_node->first_attribute("unit")) {
            unit = attr->value();
        }
        if (auto attr = var_node->first_attribute("start")) {
            //// TODO
        }

        // Create and cache the new variable (also caching its value reference)
        FmuVariableImport var(var_name, var_type, dimensions, causality_enum, variability_enum, initial_enum);
        var.m_is_deriv = is_deriv;
        var.SetValueReference(valref);

        m_variables[valref] = var;
    }

    // Traverse the list of state value references and mark the corresponding FMU variable as a state
    for (const auto& si : state_valref) {
        m_variables.at(si).m_is_state = true;
    }

    m_nx = state_valref.size();
    if (deriv_valref.size() != m_nx)
        throw std::runtime_error("Incompatible number of states and state derivatives in XML file.");

    if (m_verbose) {
        std::cout << "  Found " << m_variables.size() << " FMU variables" << std::endl;
        if (m_nx > 0) {
            std::cout << "     States      ";
            std::copy(state_valref.begin(), state_valref.end(), std::ostream_iterator<int>(std::cout, " "));
            std::cout << std::endl;
            std::cout << "     Derivatives ";
            std::copy(deriv_valref.begin(), deriv_valref.end(), std::ostream_iterator<int>(std::cout, " "));
            std::cout << std::endl;
        }
    }

    delete doc_ptr;
}

void FmuUnit::LoadSharedLibrary(FmuType fmuType) {
    std::string modelIdentifier;
    switch (fmuType) {
        case FmuType::COSIMULATION:
            modelIdentifier = info_cosim_modelIdentifier;
            break;
        case FmuType::MODEL_EXCHANGE:
            modelIdentifier = info_modex_modelIdentifier;
            break;
    }

    std::string dynlib_dir = m_directory + "/" + m_bin_directory;
    std::string dynlib_name = dynlib_dir + "/" + modelIdentifier + std::string(SHARED_LIBRARY_SUFFIX);

    if (m_verbose)
        std::cout << "Loading shared library " << dynlib_name << std::endl;

    dynlib_handle = RuntimeLinkLibrary(dynlib_dir, dynlib_name);

    if (!dynlib_handle)
        throw std::runtime_error("Could not locate the compiled FMU files: " + dynlib_name + "\n");

    // run time binding of functions
    LOAD_FMI_FUNCTION(fmi3GetVersion);
    LOAD_FMI_FUNCTION(fmi3SetDebugLogging);
    LOAD_FMI_FUNCTION(fmi3InstantiateModelExchange);
    LOAD_FMI_FUNCTION(fmi3InstantiateCoSimulation);
    LOAD_FMI_FUNCTION(fmi3InstantiateScheduledExecution);
    LOAD_FMI_FUNCTION(fmi3FreeInstance);
    LOAD_FMI_FUNCTION(fmi3EnterInitializationMode);
    LOAD_FMI_FUNCTION(fmi3ExitInitializationMode);
    LOAD_FMI_FUNCTION(fmi3EnterEventMode);
    LOAD_FMI_FUNCTION(fmi3Terminate);
    LOAD_FMI_FUNCTION(fmi3Reset);
    LOAD_FMI_FUNCTION(fmi3GetFloat32);
    LOAD_FMI_FUNCTION(fmi3GetFloat64);
    LOAD_FMI_FUNCTION(fmi3GetInt8);
    LOAD_FMI_FUNCTION(fmi3GetUInt8);
    LOAD_FMI_FUNCTION(fmi3GetInt16);
    LOAD_FMI_FUNCTION(fmi3GetUInt16);
    LOAD_FMI_FUNCTION(fmi3GetInt32);
    LOAD_FMI_FUNCTION(fmi3GetUInt32);
    LOAD_FMI_FUNCTION(fmi3GetInt64);
    LOAD_FMI_FUNCTION(fmi3GetUInt64);
    LOAD_FMI_FUNCTION(fmi3GetBoolean);
    LOAD_FMI_FUNCTION(fmi3GetString);
    LOAD_FMI_FUNCTION(fmi3GetBinary);
    LOAD_FMI_FUNCTION(fmi3GetClock);
    LOAD_FMI_FUNCTION(fmi3SetFloat32);
    LOAD_FMI_FUNCTION(fmi3SetFloat64);
    LOAD_FMI_FUNCTION(fmi3SetInt8);
    LOAD_FMI_FUNCTION(fmi3SetUInt8);
    LOAD_FMI_FUNCTION(fmi3SetInt16);
    LOAD_FMI_FUNCTION(fmi3SetUInt16);
    LOAD_FMI_FUNCTION(fmi3SetInt32);
    LOAD_FMI_FUNCTION(fmi3SetUInt32);
    LOAD_FMI_FUNCTION(fmi3SetInt64);
    LOAD_FMI_FUNCTION(fmi3SetUInt64);
    LOAD_FMI_FUNCTION(fmi3SetBoolean);
    LOAD_FMI_FUNCTION(fmi3SetString);
    LOAD_FMI_FUNCTION(fmi3SetBinary);
    LOAD_FMI_FUNCTION(fmi3SetClock);
    LOAD_FMI_FUNCTION(fmi3GetNumberOfVariableDependencies);
    LOAD_FMI_FUNCTION(fmi3GetVariableDependencies);
    LOAD_FMI_FUNCTION(fmi3GetFMUState);
    LOAD_FMI_FUNCTION(fmi3SetFMUState);
    LOAD_FMI_FUNCTION(fmi3FreeFMUState);
    LOAD_FMI_FUNCTION(fmi3SerializedFMUStateSize);
    LOAD_FMI_FUNCTION(fmi3SerializeFMUState);
    LOAD_FMI_FUNCTION(fmi3DeserializeFMUState);
    LOAD_FMI_FUNCTION(fmi3GetDirectionalDerivative);
    LOAD_FMI_FUNCTION(fmi3GetAdjointDerivative);
    LOAD_FMI_FUNCTION(fmi3EnterConfigurationMode);
    LOAD_FMI_FUNCTION(fmi3ExitConfigurationMode);
    LOAD_FMI_FUNCTION(fmi3GetIntervalDecimal);
    LOAD_FMI_FUNCTION(fmi3GetIntervalFraction);
    LOAD_FMI_FUNCTION(fmi3GetShiftDecimal);
    LOAD_FMI_FUNCTION(fmi3GetShiftFraction);
    LOAD_FMI_FUNCTION(fmi3SetIntervalDecimal);
    LOAD_FMI_FUNCTION(fmi3SetIntervalFraction);
    LOAD_FMI_FUNCTION(fmi3SetShiftDecimal);
    LOAD_FMI_FUNCTION(fmi3SetShiftFraction);
    LOAD_FMI_FUNCTION(fmi3EvaluateDiscreteStates);
    LOAD_FMI_FUNCTION(fmi3UpdateDiscreteStates);

    if (has_cosimulation) {
        LOAD_FMI_FUNCTION(fmi3EnterStepMode);
        LOAD_FMI_FUNCTION(fmi3GetOutputDerivatives);
        LOAD_FMI_FUNCTION(fmi3DoStep);
    }
    if (has_model_exchange) {
        LOAD_FMI_FUNCTION(fmi3EnterContinuousTimeMode);
        LOAD_FMI_FUNCTION(fmi3CompletedIntegratorStep);
        LOAD_FMI_FUNCTION(fmi3SetTime);
        LOAD_FMI_FUNCTION(fmi3SetContinuousStates);
        LOAD_FMI_FUNCTION(fmi3GetContinuousStateDerivatives);
        LOAD_FMI_FUNCTION(fmi3GetEventIndicators);
        LOAD_FMI_FUNCTION(fmi3GetContinuousStates);
        LOAD_FMI_FUNCTION(fmi3GetNominalsOfContinuousStates);
        LOAD_FMI_FUNCTION(fmi3GetNumberOfEventIndicators);
        LOAD_FMI_FUNCTION(fmi3GetNumberOfContinuousStates);
    }
    if (has_scheduled_execution) {
        LOAD_FMI_FUNCTION(fmi3ActivateModelPartition);
    }

    if (m_verbose) {
        std::cout << "FMI version:  " << GetVersion() << std::endl;
    }
}

void FmuUnit::BuildVariablesTree() {
    if (m_verbose)
        std::cout << "Building variables tree" << std::endl;

    for (auto& iv : this->m_variables) {
        std::string token;
        std::istringstream ss(iv.second.GetName());

        // scan all tokens delimited by "."
        int ntokens = 0;
        FmuVariableTreeNode* tree_node = &this->tree_variables;
        while (std::getline(ss, token, '.') && ntokens < 300) {
            auto next_node = tree_node->children.find(token);
            if (next_node != tree_node->children.end())  // already added
            {
                tree_node = &(next_node->second);
            } else {
                FmuVariableTreeNode new_node;
                new_node.object_name = token;
                tree_node->children[token] = new_node;
                tree_node = &tree_node->children[token];
            }
            ++ntokens;
        }
        tree_node->leaf = &iv.second;
    }
}

void FmuUnit::PrintVariablesTree(FmuVariableTreeNode* mynode, int tab) {
    for (auto& in : mynode->children) {
        for (int itab = 0; itab < tab; ++itab) {
            std::cout << "\t";
        }

        // name of tree level
        std::cout << in.first;

        // level is a FMU variable (tree leaf)
        if (in.second.leaf) {
            std::cout << " -> FMU reference:" << in.second.leaf->GetValueReference();
        }

        std::cout << "\n";
        PrintVariablesTree(&in.second, tab + 1);
    }
}

std::string FmuUnit::GetVersion() const {
    return std::string(_fmi3GetVersion());
}

void FmuUnit::Instantiate(const std::string& instanceName,
                          const std::string& resource_dir,
                          bool logging,
                          bool visible) {
    if (m_verbose)
        std::cout << "Instantiate FMU\n" << std::endl;

    log_message_callback = LoggingUtilities::logger_default;

    fmi3InstanceEnvironment instance_environment = NULL;

    if (m_fmuType == FmuType::MODEL_EXCHANGE) {
        instance = _fmi3InstantiateModelExchange(instanceName.c_str(),            // instanceName
                                                 instantiationToken.c_str(),      // instantiationToken
                                                 resource_dir.c_str(),            // resourcePath
                                                 visible ? fmi3True : fmi3False,  // visible
                                                 logging,                         // loggingOn
                                                 instance_environment,            // instanceEnvironment
                                                 log_message_callback             // logMessage
        );
    } else if (m_fmuType == FmuType::COSIMULATION) {
        fmi3Boolean eventModeUsed = fmi3False;
        fmi3Boolean earlyReturnAllowed = fmi3False;
        const size_t nRequiredIntermediateVariables = 1;
        fmi3ValueReference requiredIntermediateVariables[nRequiredIntermediateVariables];

        instance = _fmi3InstantiateCoSimulation(instanceName.c_str(),            // instanceName
                                                instantiationToken.c_str(),      // instantiationToken
                                                resource_dir.c_str(),            // resourcePath
                                                visible ? fmi3True : fmi3False,  // visible
                                                logging,                         // loggingOn
                                                eventModeUsed,                   // eventModeUsed
                                                earlyReturnAllowed,              // earlyReturnAllowed
                                                requiredIntermediateVariables,   // requiredIntermediateVariables
                                                nRequiredIntermediateVariables,  // nRequiredIntermediateVariables
                                                instance_environment,            // instanceEnvironment
                                                log_message_callback,            // logMessage
                                                intermediate_update_callback);   // intermediateUpdate
    } else if (m_fmuType == FmuType::SCHEDULED_EXECUTION) {
        instance = _fmi3InstantiateScheduledExecution(instanceName.c_str(),            // instance name
                                                      instantiationToken.c_str(),      // instantiationToken
                                                      resource_dir.c_str(),            // resource dir
                                                      visible ? fmi3True : fmi3False,  // visible
                                                      logging,                         // logging
                                                      instance_environment,            // instanceEnvironment
                                                      log_message_callback,            // logMessage
                                                      clock_update_callback,           // clockUpdate
                                                      lock_preemption_callback,        // lockPreemption
                                                      unlock_preemption_callback       // unlockPreemption
        );
    }

    if (!instance)
        throw std::runtime_error("Failed to instantiate the FMU.");
}

void FmuUnit::Instantiate(const std::string& instanceName, bool logging, bool visible) {
    try {
        Instantiate(instanceName, "file:///" + m_directory + "/resources", logging, visible);
    } catch (std::exception&) {
        throw;
    }
}

fmi3Status FmuUnit::SetDebugLogging(fmi3Boolean loggingOn, const std::vector<std::string>& logCategories) {
    std::vector<const char*> categories;
    for (const auto& category : logCategories) {
        categories.push_back(category.c_str());
    }
    return _fmi3SetDebugLogging(this->instance, loggingOn, categories.size(), categories.data());
}

fmi3Status FmuUnit::EnterInitializationMode(fmi3Boolean toleranceDefined,
                                            fmi3Float64 tolerance,
                                            fmi3Float64 startTime,
                                            fmi3Boolean stopTimeDefined,
                                            fmi3Float64 stopTime) {
    auto status = _fmi3EnterInitializationMode(this->instance,               //
                                               toleranceDefined, tolerance,  // define tolerance
                                               startTime,                    // start time
                                               stopTimeDefined, stopTime);   // use stop time
    return status;
}

fmi3Status FmuUnit::ExitInitializationMode() {
    return _fmi3ExitInitializationMode(this->instance);
}

fmi3Status FmuUnit::DoStep(fmi3Float64 currentCommunicationPoint,
                           fmi3Float64 communicationStepSize,
                           fmi3Boolean noSetFMUStatePriorToCurrentPoint) {
    if (!has_cosimulation)
        throw std::runtime_error("DoStep available only for a Co-Simulation FMU.\n");

    // TODO
    fmi3Boolean eventHandlingNeeded;
    fmi3Boolean terminateSimulation;
    fmi3Boolean earlyReturn;
    fmi3Float64 lastSuccessfulTime;

    auto status =
        _fmi3DoStep(instance, currentCommunicationPoint, communicationStepSize, noSetFMUStatePriorToCurrentPoint,
                    &eventHandlingNeeded, &terminateSimulation, &earlyReturn, &lastSuccessfulTime);
    return status;
}

fmi3Status FmuUnit::SetTime(const fmi3Float64 time) {
    if (!has_model_exchange)
        throw std::runtime_error("SetTime available only for a Model Exchange FMU.\n");

    auto status = _fmi3SetTime(this->instance, time);

    return status;
}

fmi3Status FmuUnit::GetContinuousStates(fmi3Float64 x[], size_t nx) {
    if (!has_model_exchange)
        throw std::runtime_error("GetContinuousStates available only for a Model Exchange FMU. \n");

    auto status = _fmi3GetContinuousStates(this->instance, x, nx);

    return status;
}

fmi3Status FmuUnit::SetContinuousStates(const fmi3Float64 x[], size_t nx) {
    if (!has_model_exchange)
        throw std::runtime_error("SetContinuousStates available only for a Model Exchange FMU. \n");

    auto status = _fmi3SetContinuousStates(this->instance, x, nx);

    return status;
}

fmi3Status FmuUnit::GetContinuousStateDerivatives(fmi3Float64 derivatives[], size_t nx) {
    if (!has_model_exchange)
        throw std::runtime_error("GetContinuousStateDerivatives available only for a Model Exchange FMU. \n");

    auto status = _fmi3GetContinuousStateDerivatives(this->instance, derivatives, nx);

    return status;
}

template <class T>
fmi3Status FmuUnit::SetVariable(fmi3ValueReference vr, const T& value, size_t nValues) noexcept(false) {
    fmi3Status status = fmi3Status::fmi3Error;

    FmuVariableImport& var = m_variables.at(vr);

    if (!nValues)
        nValues = GetVariableSize(var);
    std::vector<size_t> valueSizes;

    // only one variable will be parsed at a time
    const size_t nValueReferences = 1;

    auto vartype = var.GetType();

    switch (vartype) {
        case FmuVariable::Type::Float32:
            status = this->_fmi3SetFloat32(this->instance, &vr, nValueReferences, (fmi3Float32*)&value, nValues);
            break;
        case FmuVariable::Type::Float64:
            status = this->_fmi3SetFloat64(this->instance, &vr, nValueReferences, (fmi3Float64*)&value, nValues);
            break;
        case FmuVariable::Type::Int8:
            status = this->_fmi3SetInt8(this->instance, &vr, nValueReferences, (fmi3Int8*)&value, nValues);
            break;
        case FmuVariable::Type::UInt8:
            status = this->_fmi3SetUInt8(this->instance, &vr, nValueReferences, (fmi3UInt8*)&value, nValues);
            break;
        case FmuVariable::Type::Int16:
            status = this->_fmi3SetInt16(this->instance, &vr, nValueReferences, (fmi3Int16*)&value, nValues);
            break;
        case FmuVariable::Type::UInt16:
            status = this->_fmi3SetUInt16(this->instance, &vr, nValueReferences, (fmi3UInt16*)&value, nValues);
            break;
        case FmuVariable::Type::Int32:
            status = this->_fmi3SetInt32(this->instance, &vr, nValueReferences, (fmi3Int32*)&value, nValues);
            break;
        case FmuVariable::Type::UInt32:
            status = this->_fmi3SetUInt32(this->instance, &vr, nValueReferences, (fmi3UInt32*)&value, nValues);
            break;
        case FmuVariable::Type::Int64:
            status = this->_fmi3SetInt64(this->instance, &vr, nValueReferences, (fmi3Int64*)&value, nValues);
            break;
        case FmuVariable::Type::UInt64:
            status = this->_fmi3SetUInt64(this->instance, &vr, nValueReferences, (fmi3UInt64*)&value, nValues);
            break;
        case FmuVariable::Type::Boolean:
            status = this->_fmi3SetBoolean(this->instance, &vr, nValueReferences, (fmi3Boolean*)&value, nValues);
            break;
        case FmuVariable::Type::String:
            status = this->_fmi3SetString(this->instance, &vr, nValueReferences, (fmi3String*)&value, nValues);
            break;
        case FmuVariable::Type::Binary:
            // WARNING: this case may be hit when T* is:
            // - std::vector<fmi3Byte>*
            // - fmi3Binary*
            valueSizes.resize(nValues);
            status = this->_fmi3SetBinary(this->instance, &vr, nValueReferences, valueSizes.data(), (fmi3Binary*)&value,
                                          nValues);
            break;

        case FmuVariable::Type::Unknown:
            throw std::runtime_error("Fmu Variable type not initialized.");
            break;
        default:
            throw std::runtime_error("Fmu Variable type not valid.");
            break;
    }

    return status;
}

template <class T>
fmi3Status FmuUnit::SetVariable(fmi3ValueReference vr, const std::vector<T>& values_vect) noexcept(false) {
    fmi3Status status = fmi3Status::fmi3Error;

    FmuVariableImport& var = m_variables.at(vr);

    size_t nValues = GetVariableSize(var);
    auto vartype = var.GetType();
    assert(nValues == values_vect.size() && "value size should match the one of the requested variable.");

    // only one variable will be parsed at a time
    const size_t nValueReferences = 1;

    switch (vartype) {
        case FmuVariable::Type::Float32:
            status = this->_fmi3SetFloat32(this->instance, &vr, nValueReferences, (fmi3Float32*)(values_vect.data()),
                                           nValues);
            break;
        case FmuVariable::Type::Float64:
            status = this->_fmi3SetFloat64(this->instance, &vr, nValueReferences, (fmi3Float64*)(values_vect.data()),
                                           nValues);
            break;
        case FmuVariable::Type::Int8:
            status =
                this->_fmi3SetInt8(this->instance, &vr, nValueReferences, (fmi3Int8*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::UInt8:
            status =
                this->_fmi3SetUInt8(this->instance, &vr, nValueReferences, (fmi3UInt8*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::Int16:
            status =
                this->_fmi3SetInt16(this->instance, &vr, nValueReferences, (fmi3Int16*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::UInt16:
            status =
                this->_fmi3SetUInt16(this->instance, &vr, nValueReferences, (fmi3UInt16*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::Int32:
            status =
                this->_fmi3SetInt32(this->instance, &vr, nValueReferences, (fmi3Int32*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::UInt32:
            status =
                this->_fmi3SetUInt32(this->instance, &vr, nValueReferences, (fmi3UInt32*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::Int64:
            status =
                this->_fmi3SetInt64(this->instance, &vr, nValueReferences, (fmi3Int64*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::UInt64:
            status =
                this->_fmi3SetUInt64(this->instance, &vr, nValueReferences, (fmi3UInt64*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::Boolean:
            status = this->_fmi3SetBoolean(this->instance, &vr, nValueReferences, (fmi3Boolean*)(values_vect.data()),
                                           nValues);
            break;
        case FmuVariable::Type::String:
            // status = this->_fmi3SetString(this->instance, &vr, nValueReferences, (fmi3String*)(values_vect.data()),
            // nValues);
            throw std::runtime_error(
                "Developer Error: this case should not be hit. Other dedicated methods should take care of this case.");
            break;
        case FmuVariable::Type::Binary:
            throw std::runtime_error(
                "Developer Error: this case should not be hit. Other dedicated methods should take care of this case.");
            break;
        case FmuVariable::Type::Unknown:
            throw std::runtime_error("Fmu Variable type not initialized.");
            break;
        default:
            throw std::runtime_error("Fmu Variable type not valid.");
            break;
    }

    return status;
}

fmi3Status FmuUnit::SetVariable(fmi3ValueReference vr, const std::vector<fmi3Byte>& values) noexcept(false) {
    FmuVariableImport& var = m_variables.at(vr);

    size_t nValues = GetVariableSize(var);
    assert(nValues == 1 && "Developer error: the variable is expected to be a scalar but it is an array indeed.");

    std::vector<size_t> valueSizes(1);
    valueSizes[0] = values.size();

    // only one variable will be parsed at a time
    const size_t nValueReferences = 1;

    assert(var.GetType() == FmuVariable::Type::Binary &&
           "Developer Error: SetVariable for std::vector<fmi3Byte> has been called for the wrong FMI variable type");
    // TODO: why the casting is needed?
    fmi3Status status = this->_fmi3SetBinary(this->instance, &vr, nValueReferences, valueSizes.data(),
                                             (fmi3Binary*)values.data(), nValues);

    return status;
}

fmi3Status FmuUnit::SetVariable(fmi3ValueReference vr, fmi3Binary& value, size_t valueSize) noexcept(false) {
    FmuVariableImport& var = m_variables.at(vr);

    size_t nValues = GetVariableSize(var);
    assert(nValues == 1 && "The FMU variable is expected to be a scalar but it is an array.");

    // only one variable will be parsed at a time
    const size_t nValueReferences = 1;

    assert(var.GetType() == FmuVariable::Type::Binary &&
           "Developer Error: SetVariable for fmi3Binary& has been called for the wrong FMI variable type");
    fmi3Status status = this->_fmi3SetBinary(this->instance, &vr, nValueReferences, &valueSize, &value, nValues);

    return status;
}

fmi3Status FmuUnit::SetVariable(fmi3ValueReference vr,
                                const std::vector<fmi3Binary>& values_vect,
                                const std::vector<size_t>& valueSizes) noexcept(false) {
    FmuVariableImport& var = m_variables.at(vr);

    size_t nValues = GetVariableSize(var);
    assert(valueSizes.size() == values_vect.size() && "values_vect and valueSizes vectors must have the same size.");
    assert(nValues == values_vect.size() && "The FMU variable dimensions does not match with the size of values_vect.");

    // only one variable will be parsed at a time
    const size_t nValueReferences = 1;

    assert(var.GetType() == FmuVariable::Type::Binary &&
           "Developer Error: SetVariable for std::vector<fmi3Binary> has been called for the wrong FMI variable type");
    fmi3Status status =
        this->_fmi3SetBinary(this->instance, &vr, nValueReferences, valueSizes.data(), values_vect.data(), nValues);

    return status;
}

fmi3Status FmuUnit::SetVariable(fmi3ValueReference vr,
                                const std::vector<std::vector<fmi3Byte>>& values_vect) noexcept(false) {
    size_t nValues = values_vect.size();
    std::vector<fmi3Binary> values_vect_temp(values_vect.size());
    for (auto s = 0; s < nValues; ++s) {
        values_vect_temp[s] = values_vect[s].data();
    }

    std::vector<size_t> valueSizes(nValues);
    for (size_t i = 0; i < nValues; ++i) {
        valueSizes[i] = values_vect[i].size();
    }

    fmi3Status status = SetVariable(vr, values_vect_temp, valueSizes);

    return status;
}

fmi3Status FmuUnit::SetVariable(fmi3ValueReference vr, const std::vector<fmi3String>& values_vect) noexcept(false) {
    FmuVariableImport& var = m_variables.at(vr);

    size_t nValues = GetVariableSize(var);
    assert(nValues == values_vect.size() && "Developer error: the variable has a size that differs from values_vect.");

    // only one variable will be parsed at a time
    const size_t nValueReferences = 1;

    assert(var.GetType() == FmuVariable::Type::String &&
           "Developer Error: SetVariable for std::vector<fmi3String>& has been called for the wrong FMI variable type");
    fmi3Status status = this->_fmi3SetString(this->instance, &vr, nValueReferences, values_vect.data(), nValues);

    return status;
}

fmi3Status FmuUnit::SetVariable(fmi3ValueReference vr, const std::vector<std::string>& values_vect) noexcept(false) {
    std::vector<fmi3String> values_vect_temp(values_vect.size());
    for (size_t i = 0; i < values_vect.size(); ++i) {
        values_vect_temp[i] = values_vect[i].data();
    }
    fmi3Status status = SetVariable(vr, values_vect_temp);

    return status;
}

template <class T>
fmi3Status FmuUnit::GetVariable(fmi3ValueReference vr, T& value, size_t nValues) const noexcept(false) {
    fmi3Status status = fmi3Status::fmi3Error;

    // TODO
    size_t valueSizes[1];
    valueSizes[0] = 1;

    const FmuVariableImport& var = m_variables.at(vr);

    if (!nValues)
        nValues = GetVariableSize(var);

    // only one variable will be parsed at a time
    const size_t nValueReferences = 1;

    auto vartype = var.GetType();

    switch (vartype) {
        case FmuVariable::Type::Float32:
            status = this->_fmi3GetFloat32(this->instance, &vr, nValueReferences, (fmi3Float32*)&value, nValues);
            break;
        case FmuVariable::Type::Float64:
            status = this->_fmi3GetFloat64(this->instance, &vr, nValueReferences, (fmi3Float64*)&value, nValues);
            break;
        case FmuVariable::Type::Int8:
            status = this->_fmi3GetInt8(this->instance, &vr, nValueReferences, (fmi3Int8*)&value, nValues);
            break;
        case FmuVariable::Type::UInt8:
            status = this->_fmi3GetUInt8(this->instance, &vr, nValueReferences, (fmi3UInt8*)&value, nValues);
            break;
        case FmuVariable::Type::Int16:
            status = this->_fmi3GetInt16(this->instance, &vr, nValueReferences, (fmi3Int16*)&value, nValues);
            break;
        case FmuVariable::Type::UInt16:
            status = this->_fmi3GetUInt16(this->instance, &vr, nValueReferences, (fmi3UInt16*)&value, nValues);
            break;
        case FmuVariable::Type::Int32:
            status = this->_fmi3GetInt32(this->instance, &vr, nValueReferences, (fmi3Int32*)&value, nValues);
            break;
        case FmuVariable::Type::UInt32:
            status = this->_fmi3GetUInt32(this->instance, &vr, nValueReferences, (fmi3UInt32*)&value, nValues);
            break;
        case FmuVariable::Type::Int64:
            status = this->_fmi3GetInt64(this->instance, &vr, nValueReferences, (fmi3Int64*)&value, nValues);
            break;
        case FmuVariable::Type::UInt64:
            status = this->_fmi3GetUInt64(this->instance, &vr, nValueReferences, (fmi3UInt64*)&value, nValues);
            break;
        case FmuVariable::Type::Boolean:
            status = this->_fmi3GetBoolean(this->instance, &vr, nValueReferences, (fmi3Boolean*)&value, nValues);
            break;
        case FmuVariable::Type::String:
            status = this->_fmi3GetString(this->instance, &vr, nValueReferences, (fmi3String*)&value, nValues);
            break;
        case FmuVariable::Type::Binary:
            // the only case that should end up here is when T* is fmi3Binary*
            status =
                this->_fmi3GetBinary(this->instance, &vr, nValueReferences, valueSizes, (fmi3Binary*)&value, nValues);
            break;
        case FmuVariable::Type::Unknown:
            throw std::runtime_error("Fmu Variable type not initialized.");
            break;
        default:
            throw std::runtime_error("Fmu Variable type not valid.");
            break;
    }

    return status;
}

template <class T>
fmi3Status FmuUnit::GetVariable(fmi3ValueReference vr, std::vector<T>& values_vect) const noexcept(false) {
    fmi3Status status = fmi3Status::fmi3Error;

    const FmuVariableImport& var = m_variables.at(vr);

    size_t nValues = GetVariableSize(var);
    auto vartype = var.GetType();
    values_vect.resize(nValues);

    // only one variable will be parsed at a time
    const size_t nValueReferences = 1;

    std::vector<fmi3String> values_ptr(nValues);

    switch (vartype) {
        case FmuVariable::Type::Float32:
            status = this->_fmi3GetFloat32(this->instance, &vr, nValueReferences, (fmi3Float32*)(values_vect.data()),
                                           nValues);
            break;
        case FmuVariable::Type::Float64:
            status = this->_fmi3GetFloat64(this->instance, &vr, nValueReferences, (fmi3Float64*)(values_vect.data()),
                                           nValues);
            break;
        case FmuVariable::Type::Int8:
            status =
                this->_fmi3GetInt8(this->instance, &vr, nValueReferences, (fmi3Int8*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::UInt8:
            status =
                this->_fmi3GetUInt8(this->instance, &vr, nValueReferences, (fmi3UInt8*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::Int16:
            status =
                this->_fmi3GetInt16(this->instance, &vr, nValueReferences, (fmi3Int16*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::UInt16:
            status =
                this->_fmi3GetUInt16(this->instance, &vr, nValueReferences, (fmi3UInt16*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::Int32:
            status =
                this->_fmi3GetInt32(this->instance, &vr, nValueReferences, (fmi3Int32*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::UInt32:
            status =
                this->_fmi3GetUInt32(this->instance, &vr, nValueReferences, (fmi3UInt32*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::Int64:
            status =
                this->_fmi3GetInt64(this->instance, &vr, nValueReferences, (fmi3Int64*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::UInt64:
            status =
                this->_fmi3GetUInt64(this->instance, &vr, nValueReferences, (fmi3UInt64*)(values_vect.data()), nValues);
            break;
        case FmuVariable::Type::Boolean:
            status = this->_fmi3GetBoolean(this->instance, &vr, nValueReferences, (fmi3Boolean*)(values_vect.data()),
                                           nValues);
            break;
        case FmuVariable::Type::String:
            throw std::runtime_error(
                "Developer Error: this case should not be hit. Other dedicated methods should take care of this case.");
            break;
        case FmuVariable::Type::Binary:
            throw std::runtime_error(
                "Developer Error: this case should not be hit. Other dedicated methods should take care of this case.");
            break;
        case FmuVariable::Type::Unknown:
            throw std::runtime_error("Fmu Variable type not initialized.");
            break;
        default:
            throw std::runtime_error("Fmu Variable type not valid.");
            break;
    }

    return status;
}

fmi3Status FmuUnit::GetVariable(fmi3ValueReference vr, fmi3Binary& values, size_t& valueSize) const noexcept(false) {
    const FmuVariableImport& var = m_variables.at(vr);

    size_t nValues = GetVariableSize(var);
    assert(nValues == 1 && "Developer error: the variable is expected to be a scalar but it is an array indeed.");
    assert(var.GetType() == FmuVariable::Type::Binary &&
           "Developer Error: GetVariable for fmi3Binary& has been called for the wrong FMI variable type");

    const size_t nValueReferences = 1;
    fmi3Status status = this->_fmi3GetBinary(this->instance, &vr, nValueReferences, &valueSize, &values, nValues);

    return status;
}

fmi3Status FmuUnit::GetVariable(fmi3ValueReference vr, std::vector<fmi3Byte>& values) const noexcept(false) {
    const FmuVariableImport& var = m_variables.at(vr);

    size_t nValues = GetVariableSize(var);
    assert(nValues == 1 && "Developer error: the variable is expected to be a scalar but it is an array indeed.");
    assert(var.GetType() == FmuVariable::Type::Binary &&
           "Developer Error: GetVariable for std::vector<fmi3Byte> has been called for the wrong FMI variable type");

    const size_t nValueReferences = 1;
    std::vector<size_t> valueSizes(1);
    std::vector<fmi3Binary> values_ptr(1);
    fmi3Status status =
        this->_fmi3GetBinary(this->instance, &vr, nValueReferences, valueSizes.data(), values_ptr.data(), nValues);

    values.resize(valueSizes[0]);
    for (auto val = 0; val < valueSizes[0]; ++val) {
        values[val] = values_ptr[0][val];
    }

    return status;
}

fmi3Status FmuUnit::GetVariable(fmi3ValueReference vr,
                                std::vector<fmi3Binary>& values_vect,
                                std::vector<size_t>& valueSizes) const noexcept(false) {
    const FmuVariableImport& var = m_variables.at(vr);
    assert(var.GetType() == FmuVariable::Type::Binary &&
           "Developer Error: GetVariable for std::vector<fmi3Binary> has been called for the wrong FMI variable type");

    size_t nValues = GetVariableSize(var);
    valueSizes.resize(nValues);
    values_vect.resize(nValues);
    const size_t nValueReferences = 1;

    fmi3Status status =
        this->_fmi3GetBinary(this->instance, &vr, nValueReferences, valueSizes.data(), values_vect.data(), nValues);

    return status;
}

fmi3Status FmuUnit::GetVariable(fmi3ValueReference vr, std::vector<std::vector<fmi3Byte>>& values_vect) const
    noexcept(false) {
    const FmuVariableImport& var = m_variables.at(vr);

    size_t nValues = GetVariableSize(var);
    values_vect.resize(nValues);
    std::vector<size_t> valueSizes(nValues);

    const size_t nValueReferences = 1;

    assert(var.GetType() == FmuVariable::Type::Binary &&
           "GetVariable for std::vector<std::vector<fmi3Byte>> has been called for the wrong FMI variable type");
    std::vector<fmi3Binary> values_ptr(nValues);
    fmi3Status status =
        this->_fmi3GetBinary(this->instance, &vr, nValueReferences, valueSizes.data(), values_ptr.data(), nValues);

    // copy the values from the FMU
    for (auto val = 0; val < nValues; ++val) {
        values_vect[val].resize(valueSizes[val]);
        for (auto sel = 0; sel < valueSizes[val]; ++sel) {
            values_vect[val][sel] = values_ptr[val][sel];
        }
    }

    return status;
}

fmi3Status FmuUnit::GetVariable(fmi3ValueReference vr, std::vector<fmi3String>& values_vect) const noexcept(false) {
    const FmuVariableImport& var = m_variables.at(vr);
    assert(var.GetType() == FmuVariable::Type::String &&
           "Developer Error: GetVariable for std::vector<fmi3String> has been called for the wrong FMI variable type");

    size_t nValues = GetVariableSize(var);
    values_vect.resize(nValues);
    const size_t nValueReferences = 1;

    fmi3Status status = this->_fmi3GetString(this->instance, &vr, nValueReferences, values_vect.data(), nValues);

    return status;
}

fmi3Status FmuUnit::GetVariable(fmi3ValueReference vr, std::vector<std::string>& values_vect) const noexcept(false) {
    std::vector<fmi3String> values_vect_temp;  // will be resized later

    fmi3Status status = GetVariable(vr, values_vect_temp);
    values_vect.resize(values_vect_temp.size());
    for (size_t i = 0; i < values_vect_temp.size(); ++i) {
        values_vect[i] = std::string(values_vect_temp[i]);
    }

    return status;
}

/// @} fmu_forge_fmi3

}  // namespace fmi3
}  // namespace fmu_forge
