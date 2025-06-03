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
// DEVELOPER NOTES:
// FmuVariableExport:
// - Set|GetValue have a specific implementation for fmi<N>String in order to handle them internally as std::strings
// - FmuVariableExport constness does not refer to the value nor the dimension of the pointed variable (that can indeed
// be changed), but to the 'binding' to that underlying variable
// =============================================================================

//// RADU TODO
//// - add support for binary variables
//// - complete FMI function implementations

#define NOMINMAX
#include <algorithm>

#include <regex>
#include <fstream>

#include "fmi3/FmuToolsExport.h"
#include "FmuToolsRuntimeLinking.h"

#include "rapidxml/rapidxml_ext.hpp"

namespace fmu_forge {
namespace fmi3 {

// =============================================================================

const std::unordered_set<UnitDefinition, UnitDefinition::Hash> common_unitdefinitions = {
    UD_kg,  UD_m,    UD_s,     UD_A,      UD_K, UD_mol, UD_cd, UD_rad,  //
    UD_m_s, UD_m_s2, UD_rad_s, UD_rad_s2,                               //
    UD_N,   UD_Nm,   UD_N_m2                                            //
};

bool is_pointer_variant(const FmuVariableBindType& myVariant) {
    return varns::visit([](auto&& arg) -> bool { return std::is_pointer_v<std::decay_t<decltype(arg)>>; }, myVariant);
}

bool createModelDescription(const std::string& path, std::string& err_msg) {
    bool has_cosim = true;
    bool has_modex = true;

    std::exception e_cosim;
    std::exception e_modex;

    FmuComponentBase* fmu = nullptr;

    try {
        fmu = fmi3InstantiateIMPL(FmuType::COSIMULATION,                                             //
                                  "",                                                                //
                                  FMU_GUID,                                                          //
                                  ("file:///" + GetLibraryLocation() + "/../../resources").c_str(),  //
                                  fmi3False, fmi3False,                                              //
                                  nullptr,                                                           //
                                  LoggingUtilities::logger_default                                   //
        );
    } catch (std::exception& e) {
        has_cosim = false;
        e_cosim = e;
    }
    try {
        fmu = fmi3InstantiateIMPL(FmuType::MODEL_EXCHANGE,                                           //
                                  "",                                                                //
                                  FMU_GUID,                                                          //
                                  ("file:///" + GetLibraryLocation() + "/../../resources").c_str(),  //
                                  fmi3False, fmi3False,                                              //
                                  nullptr,                                                           //
                                  LoggingUtilities::logger_default                                   //
        );
    } catch (std::exception& e) {
        has_modex = false;
        e_modex = e;
    }

    bool ok = has_cosim || has_modex;

    if (ok) {
        fmu->ExportModelDescription(path);
    } else {
        err_msg = "FMU is not set as either CoSimulation nor ModelExchange.\nCosim exception : " +
                  std::string(e_cosim.what()) + "\nModex exception: " + std::string(e_modex.what());
    }

    delete fmu;

    return ok;
}

// =============================================================================

FmuVariableExport::FmuVariableExport(const VarbindType& varbind,
                                     const std::string& name,
                                     FmuVariable::Type type,
                                     CausalityType causality,
                                     VariabilityType variability,
                                     InitialType initial)
    : FmuVariableExport(varbind, name, type, {}, causality, variability, initial) {}

FmuVariableExport::FmuVariableExport(const VarbindType& varbind,
                                     const std::string& name,
                                     FmuVariable::Type type,
                                     const DimensionsArrayType& dimensions,
                                     CausalityType causality,
                                     VariabilityType variability,
                                     InitialType initial)
    : FmuVariable(name, type, dimensions, causality, variability, initial) {
    // From FMI Reference
    // If initial = 'exact' or 'approx', or causality = 'input',       a start value MUST be provided.
    // If initial = 'calculated',        or causality = 'independent', a start value CANNOT be provided.
    if (m_initial == InitialType::calculated || m_causality == CausalityType::independent) {
        m_allowed_start = false;
        m_required_start = false;
    }

    if (m_initial == InitialType::exact || m_initial == InitialType::approx || m_causality == CausalityType::input) {
        m_allowed_start = true;
        m_required_start = true;
    }

    m_varbind = varbind;
}

FmuVariableExport::FmuVariableExport(const FmuVariableExport& other) : FmuVariable(other) {
    m_varbind = other.m_varbind;
    m_allowed_start = other.m_allowed_start;
    m_required_start = other.m_required_start;
}

FmuVariableExport& FmuVariableExport::operator=(const FmuVariableExport& other) {
    if (this == &other) {
        return *this;  // Self-assignment guard
    }

    FmuVariable::operator=(other);

    m_varbind = other.m_varbind;
    m_allowed_start = other.m_allowed_start;
    m_required_start = other.m_required_start;

    return *this;
}

void FmuVariableExport::SetValue(const fmi3String* val, size_t nValues) const {
    if (is_pointer_variant(m_varbind)) {
        assert((nValues == 0 || (IsScalar() && nValues == 1) || m_dimensions.size() > 0) &&
               ("Requested to get the value of " + std::to_string(nValues) +
                " coefficients for variable with valueReference: " + std::to_string(m_valueReference) +
                " but it seems that it is a scalar.")
                   .c_str());

        std::string* varptr_this = varns::get<std::string*>(m_varbind);

        //*varns::get<std::string*>(m_varbind) = std::string(*val);

        // try to fetch the dimension of the variable
        size_t var_size;
        bool success = GetSize(var_size);
        assert(success &&
               "Developer Error: the size of a fmi3String variable could not be determined. Please ensure it has been "
               "declared using {, true}.");
        assert(nValues == var_size && "The user provided nValues that is not matching the size of the variable.");

        // copy values
        for (size_t s = 0; s < nValues; s++) {
            varptr_this[s] = std::string(val[s]);
        }
    } else {
        varns::get<FunGetSet<std::string>>(m_varbind).second(std::string(*val));
    }
}

void FmuVariableExport::SetValue(const fmi3Binary* val, size_t nValues, const size_t* valueSize_ptr) const {
    if (is_pointer_variant(m_varbind)) {
        assert((nValues == 0 || (IsScalar() && nValues == 1) || m_dimensions.size() > 0) &&
               ("Requested to get the value of " + std::to_string(nValues) +
                " coefficients for variable with valueReference: " + std::to_string(m_valueReference) +
                " but it seems that it is a scalar.")
                   .c_str());

        std::vector<fmi3Byte>* varptr_this = varns::get<std::vector<fmi3Byte>*>(m_varbind);

        // try to fetch the dimension of the variable
        size_t var_size;
        bool success = GetSize(var_size);
        assert(success &&
               "Developer Error: the size of a fmi3Binary variable could not be determined. Please ensure it has been "
               "declared using {, true}.");
        assert(nValues == var_size && "The user provided nValues that is not matching the size of the variable.");

        // copy values
        for (size_t s = 0; s < nValues; s++) {
            varptr_this[s].resize(valueSize_ptr[s]);
            for (size_t inner_sel = 0; inner_sel < valueSize_ptr[s]; inner_sel++) {
                varptr_this[s][inner_sel] = val[s][inner_sel];
            }
        }
    } else {
        // TODO: consider multi-dimensional variables
        throw std::runtime_error(
            "Developer Error: SetValue has been called for a fmi3Binary variable, but the "
            "variable is not of type fmi3Binary.");
    }
}

void FmuVariableExport::GetValue(fmi3String* varptr_ext, size_t nValues) const {
    if (is_pointer_variant(m_varbind)) {
        // the fmi3Binary is implemented by default as an std::vector<fmi3Byte>
        std::string* varptr_this = varns::get<std::string*>(m_varbind);
        size_t var_size;
        bool success = GetSize(var_size);
        assert(success &&
               "Developer Error: the size of a fmi3Binary variable could not be determined. Please ensure it has been "
               "declared using {, true}.");
        assert(nValues == var_size && "The user provided nValues that is not matching the size of the variable.");
        for (size_t size = 0; size < var_size; size++) {
            varptr_ext[size] = varptr_this[size].data();  // TODO: check if c_str or data is needed
        }

    } else {
        // returns the address of the underlying string, does not copy any value
        *varptr_ext =
            varns::get<FunGetSet<std::string>>(m_varbind).first().data();  // TODO: check if c_str or data is needed
    }
}

void FmuVariableExport::GetValue(fmi3Binary* varptr_ext, size_t nValues, size_t* valueSize_ptr) const {
    if (is_pointer_variant(m_varbind)) {
        // the fmi3Binary is implemented by default as an std::vector<fmi3Byte>
        std::vector<fmi3Byte>* varptr_this = varns::get<std::vector<fmi3Byte>*>(m_varbind);
        size_t var_size;
        bool success = GetSize(var_size);
        assert(success &&
               "Developer Error: the size of a fmi3Binary variable could not be determined. Please ensure it has been "
               "declared using {, true}.");
        assert(nValues == var_size && "The user provided nValues that is not matching the size of the variable.");
        for (size_t size = 0; size < var_size; size++) {
            varptr_ext[size] = varptr_this[size].data();
            valueSize_ptr[size] = varptr_this[size].size();
        }

    } else {
        throw std::runtime_error("FunSetGet not implemented for fmi3Binary type.");
    }
}

void variant_to_string(const std::string* varb, size_t id, std::stringstream& ss) {
    ss << varb[id];
}

void variant_to_string(const std::vector<fmi3Byte>* varb, size_t id, std::stringstream& ss) {
    // TODO: can be done like ss << varb[id]?
    size_t size = varb[id].size();
    ss << std::hex;
    for (size_t s = 0; s < size; ++s) {
        ss << static_cast<unsigned int>(varb[id][s]);
    }
    ss << std::dec;
}

void variant_to_string(const FunGetSet<std::string> varb, size_t size, std::stringstream& ss) {
    ss << varb.first();
}

std::string FmuVariableExport::GetStartValAsString(size_t size_id) const {
    // TODO: C++17 would allow the overload operator in lambda
    // std::string start_string;
    // varns::visit([&start_string](auto&& arg) -> std::string {return start_string = std::to_string(*start_ptr)});

    std::stringstream start_value;

    if (m_type == Type::String || m_type == Type::Binary) {
        size_t id = size_id;  // alias, just to make the code more readable
        varns::visit([&start_value, id](auto&& varb) { variant_to_string(varb, id, start_value); }, m_varbind);
    } else {
        size_t size = size_id;  // alias, just to make the code more readable
        if (size == 0) {
            bool success = GetSize(size);
            if (!success)
                throw std::runtime_error("GetStartValAsString: cannot get size of variable.");
        }
        varns::visit([&start_value, size](auto&& varb) { variant_to_string(varb, size, start_value); }, m_varbind);
    }
    std::string s = start_value.str();
    return start_value.str();
}

// =============================================================================

FmuComponentBase::FmuComponentBase(FmuType fmiInterfaceType,
                                   fmi3String instanceName,
                                   fmi3String instantiationToken,
                                   fmi3String resourcePath,
                                   fmi3Boolean visible,
                                   fmi3Boolean loggingOn,
                                   fmi3InstanceEnvironment instanceEnvironment,
                                   fmi3LogMessageCallback logMessage,
                                   const std::unordered_map<std::string, bool>& logCategories_init,
                                   const std::unordered_set<std::string>& logCategories_debug_init)
    : m_instanceName(instanceName),
      m_logMessage(logMessage),
      m_instantiationToken(FMU_GUID),
      m_visible(visible == fmi3True ? true : false),
      m_debug_logging_enabled(loggingOn == fmi3True ? true : false),
      m_instanceEnvironment(instanceEnvironment),
      m_modelIdentifier(FMU_MODEL_IDENTIFIER),
      m_fmuMachineState(FmuMachineState::instantiated),
      m_logCategories_enabled(logCategories_init),
      m_logCategories_debug(logCategories_debug_init) {
    m_unitDefinitions["1"] = UnitDefinition("1");  // guarantee the existence of the default unit
    m_unitDefinitions[""] = UnitDefinition("");    // guarantee the existence of the unassigned unit

    AddFmuVariable(&m_time, "time", FmuVariable::Type::Float64, {}, "s", "time",  //
                   FmuVariable::CausalityType::independent,                       //
                   FmuVariable::VariabilityType::continuous);

    // Parse URL according to https://datatracker.ietf.org/doc/html/rfc3986
    std::string m_resources_location_str = std::string(resourcePath);

    std::regex url_patternA("^(\\w+):\\/\\/[^\\/]*\\/([^#\\?]+)");
    std::regex url_patternB("^(\\w+):\\/([^\\/][^#\\?]+)");

    std::smatch url_matches;
    std::string url_scheme;
    if ((std::regex_search(m_resources_location_str, url_matches, url_patternA) ||
         std::regex_search(m_resources_location_str, url_matches, url_patternB)) &&
        url_matches.size() >= 3) {
        if (url_matches[1].compare("file") != 0) {
            sendToLog("Bad URL scheme: " + url_matches[1].str() + ". Trying to continue.\n", fmi3Status::fmi3Warning,
                      "logStatusWarning");
        }
        m_resources_location = std::string(url_matches[2]) + "/";
    } else {
        // TODO: rollback?
        sendToLog("Cannot parse resource location: " + m_resources_location_str + "\n", fmi3Status::fmi3Warning,
                  "logStatusWarning");

        m_resources_location = GetLibraryLocation() + "/../../resources/";
        sendToLog("Rolled back to default location: " + m_resources_location + "\n", fmi3Status::fmi3Warning,
                  "logStatusWarning");
    }

    // Compare GUID
    if (std::string(instantiationToken).compare(m_instantiationToken)) {
        sendToLog("GUID used for instantiation not matching with source.\n", fmi3Status::fmi3Warning,
                  "logStatusWarning");
    }

    for (auto deb_sel = logCategories_debug_init.begin(); deb_sel != logCategories_debug_init.end(); ++deb_sel) {
        if (logCategories_init.find(*deb_sel) == logCategories_init.end()) {
            sendToLog("Developer error: Log category \"" + *deb_sel +
                          "\" specified to be of debug is not listed as a log category.\n",
                      fmi3Status::fmi3Warning, "logStatusWarning");
        }
    }
}

void FmuComponentBase::initializeType(FmuType fmuType) {
    switch (fmuType) {
        case FmuType::COSIMULATION:
            if (!is_cosimulation_available())
                throw std::runtime_error("Requested CoSimulation FMU mode but it is not available.");
            break;
        case FmuType::MODEL_EXCHANGE:
            if (!is_modelexchange_available())
                throw std::runtime_error("Requested ModelExchange FMU mode but it is not available.");
            break;
        default:
            throw std::runtime_error("Requested unrecognized FMU type.");
            break;
    }
    m_fmuType = fmuType;
}

void FmuComponentBase::SetDebugLogging(std::string cat, bool value) {
    try {
        m_logCategories_enabled[cat] = value;
    } catch (const std::exception&) {
        sendToLog("The LogCategory \"" + cat +
                      "\" is not recognized by the FMU. Please check its availability in modelDescription.xml.\n",
                  fmi3Status::fmi3Error, "logStatusError");
    }
}

const FmuVariableExport& FmuComponentBase::AddFmuVariable(const FmuVariableExport::VarbindType& varbind,
                                                          std::string name,
                                                          FmuVariable::Type scalartype,
                                                          const FmuVariable::DimensionsArrayType& dimensions,
                                                          std::string unitname,
                                                          std::string description,
                                                          FmuVariable::CausalityType causality,
                                                          FmuVariable::VariabilityType variability,
                                                          FmuVariable::InitialType initial) {
    // check if unit definition exists
    auto match_unit = m_unitDefinitions.find(unitname);
    if (match_unit == m_unitDefinitions.end()) {
        auto predicate_samename = [unitname](const UnitDefinition& var) { return var.name == unitname; };
        auto match_commonunit =
            std::find_if(common_unitdefinitions.begin(), common_unitdefinitions.end(), predicate_samename);
        if (match_commonunit == common_unitdefinitions.end()) {
            throw std::runtime_error(
                "Variable unit is not registered within this FmuComponentBase. Call 'addUnitDefinition' "
                "first.");
        } else {
            addUnitDefinition(*match_commonunit);
        }
    }

    // create new variable
    // check if same-name variable exists
    std::set<FmuVariableExport>::iterator it_name = findByName(name);
    if (it_name != m_variables.end())
        throw std::runtime_error("Cannot add two FMU variables with the same name.");

    // check if dimensions are valid
    for (auto& dim : dimensions) {
        if (dim.second == false) {
            std::set<FmuVariableExport>::iterator it_ref = findByValref(dim.first);
            if (it_ref == m_variables.end()) {
                sendToLog("WARNING: the variable with name '" + name +
                              "' have dimensions that depend on a variable not yet added to the FMU.\n",
                          fmi3Status::fmi3Warning, "logStatusWarning");
            } else {
                if (it_ref->GetType() != FmuVariable::Type::UInt64) {
                    sendToLog("WARNING: the variable with name '" + name +
                                  "' have dimensions that depend on a variable that is not of type UInt64.\n",
                              fmi3Status::fmi3Warning, "logStatusWarning");
                }
                if (it_ref->GetVariability() != FmuVariable::VariabilityType::constant &&
                    it_ref->GetCausality() != FmuVariable::CausalityType::structuralParameter) {
                    sendToLog("WARNING: the variable with name '" + name +
                                  "' have dimensions that depend on a variable that is not constant nor a structural "
                                  "parameter.\n",
                              fmi3Status::fmi3Warning, "logStatusWarning");
                }
            }
        }
    }

    FmuVariableExport newvar(varbind, name, scalartype, dimensions, causality, variability, initial);
    newvar.SetUnitName(unitname);
    newvar.SetValueReference(m_valrefCounter++);
    newvar.SetDescription(description);

    // check that the attributes of the variable would allow a no-set variable
    ////const FmuMachineState tempFmuState = FmuMachineState::anySettableState;

    newvar.ExposeStartValue(expose_variable_start_values_whenever_possible);

    std::pair<std::set<FmuVariableExport>::iterator, bool> ret = m_variables.insert(newvar);
    if (!ret.second)
        throw std::runtime_error("Developer error: cannot insert new variable into FMU.");

    return *(ret.first);
}

bool FmuComponentBase::RebindVariable(FmuVariableExport::VarbindType varbind, std::string name) {
    std::set<FmuVariableExport>::iterator it = this->findByName(name);
    if (it != m_variables.end()) {
        FmuVariableExport newvar(*it);
        newvar.Bind(varbind);
        m_variables.erase(*it);

        std::pair<std::set<FmuVariableExport>::iterator, bool> ret = m_variables.insert(newvar);

        return ret.second;
    }

    return false;
}

void FmuComponentBase::DeclareStateDerivative(const std::string& derivative_name,
                                              const std::string& state_name,
                                              const std::vector<std::string>& dependency_names) {
    addDerivative(derivative_name, state_name, dependency_names);
}

void FmuComponentBase::addDerivative(const std::string& derivative_name,
                                     const std::string& state_name,
                                     const std::vector<std::string>& dependency_names) {
    // Check that a variable with specified state name exists
    {
        std::set<FmuVariableExport>::iterator it = this->findByName(state_name);
        if (it == m_variables.end())
            throw std::runtime_error("No state variable with given name exists.");
    }

    // Check that a variable with specified derivative name exists
    {
        std::set<FmuVariableExport>::iterator it = this->findByName(derivative_name);
        if (it == m_variables.end())
            throw std::runtime_error("No state derivative variable with given name exists.");
    }

    m_derivatives.insert({derivative_name, {state_name, dependency_names}});
}

std::string FmuComponentBase::isDerivative(const std::string& name) {
    auto state = m_derivatives.find(name);
    if (state == m_derivatives.end())
        return "";
    return state->second.first;
}

void FmuComponentBase::DeclareVariableDependencies(const std::string& variable_name,
                                                   const std::vector<std::string>& dependency_names) {
    addDependencies(variable_name, dependency_names);
}

void FmuComponentBase::addDependencies(const std::string& variable_name,
                                       const std::vector<std::string>& dependency_names) {
    // Check that a variable with specified name exists
    {
        std::set<FmuVariableExport>::iterator it = this->findByName(variable_name);
        if (it == m_variables.end())
            throw std::runtime_error("No primary variable with given name exists.");
    }

    // Check that the specified dependencies corresponds to existing variables
    for (const auto& dependency_name : dependency_names) {
        std::set<FmuVariableExport>::iterator it = this->findByName(dependency_name);
        if (it == m_variables.end())
            throw std::runtime_error("No dependency variable with given name exists.");
    }

    // If a dependency list already exists for the specified variable, append to it; otherwise, initialize it
    auto list = m_variableDependencies.find(variable_name);
    if (list != m_variableDependencies.end()) {
        list->second.insert(list->second.end(), dependency_names.begin(), dependency_names.end());
    } else {
        m_variableDependencies.insert({variable_name, dependency_names});
    }
}

// -----------------------------------------------------------------------------

void FmuComponentBase::ExportModelDescription(std::string path) {
    preModelDescriptionExport();

    // Create the XML document
    rapidxml::xml_document<>* doc_ptr = new rapidxml::xml_document<>();

    // Add the XML declaration
    rapidxml::xml_node<>* declaration = doc_ptr->allocate_node(rapidxml::node_declaration);
    declaration->append_attribute(doc_ptr->allocate_attribute("version", "1.0"));
    declaration->append_attribute(doc_ptr->allocate_attribute("encoding", "UTF-8"));
    doc_ptr->append_node(declaration);

    // Create the root node
    rapidxml::xml_node<>* rootNode = doc_ptr->allocate_node(rapidxml::node_element, "fmiModelDescription");
    rootNode->append_attribute(doc_ptr->allocate_attribute("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance"));
    rootNode->append_attribute(doc_ptr->allocate_attribute("fmiVersion", fmi3GetVersion()));
    rootNode->append_attribute(
        doc_ptr->allocate_attribute("modelName", m_modelIdentifier.c_str()));  // modelName can be anything else
    rootNode->append_attribute(doc_ptr->allocate_attribute("instantiationToken", m_instantiationToken.c_str()));
    rootNode->append_attribute(doc_ptr->allocate_attribute("generationTool", "rapidxml"));
    rootNode->append_attribute(doc_ptr->allocate_attribute("variableNamingConvention", "structured"));
    doc_ptr->append_node(rootNode);

    // Add CoSimulation node
    if (is_cosimulation_available()) {
        rapidxml::xml_node<>* coSimNode = doc_ptr->allocate_node(rapidxml::node_element, "CoSimulation");
        coSimNode->append_attribute(doc_ptr->allocate_attribute("modelIdentifier", m_modelIdentifier.c_str()));
        coSimNode->append_attribute(doc_ptr->allocate_attribute("canHandleVariableCommunicationStepSize", "true"));
        coSimNode->append_attribute(doc_ptr->allocate_attribute("canInterpolateInputs", "true"));
        coSimNode->append_attribute(doc_ptr->allocate_attribute("maxOutputDerivativeOrder", "1"));
        coSimNode->append_attribute(doc_ptr->allocate_attribute("canGetAndSetFMUstate", "false"));
        coSimNode->append_attribute(doc_ptr->allocate_attribute("canSerializeFMUstate", "false"));
        coSimNode->append_attribute(doc_ptr->allocate_attribute("providesDirectionalDerivative", "false"));
        rootNode->append_node(coSimNode);
    }

    // Add ModelExchange node
    if (is_modelexchange_available()) {
        rapidxml::xml_node<>* modExNode = doc_ptr->allocate_node(rapidxml::node_element, "ModelExchange");
        modExNode->append_attribute(doc_ptr->allocate_attribute("modelIdentifier", m_modelIdentifier.c_str()));
        modExNode->append_attribute(doc_ptr->allocate_attribute("needsExecutionTool", "false"));
        modExNode->append_attribute(doc_ptr->allocate_attribute("completedIntegratorStepNotNeeded", "false"));
        modExNode->append_attribute(doc_ptr->allocate_attribute("canBeInstantiatedOnlyOncePerProcess", "false"));
        modExNode->append_attribute(doc_ptr->allocate_attribute("canNotUseMemoryManagementFunctions", "false"));
        modExNode->append_attribute(doc_ptr->allocate_attribute("canGetAndSetFMUstate", "false"));
        modExNode->append_attribute(doc_ptr->allocate_attribute("canSerializeFMUstate", "false"));
        modExNode->append_attribute(doc_ptr->allocate_attribute("providesDirectionalDerivative", "false"));
        rootNode->append_node(modExNode);
    }

    // NOTE: rapidxml does not copy the strings that we pass to print, but it just keeps the addresses until
    // it's time to print them so we cannot use a temporary string to convert the number to string and then
    // recycle it we cannot use std::vector because, in case of reallocation, it might move the array somewhere
    // else thus invalidating the addresses. To address this, use a list that remains in scope for the duration.
    std::list<std::string> stringbuf;

    // Add UnitDefinitions node
    rapidxml::xml_node<>* unitDefsNode = doc_ptr->allocate_node(rapidxml::node_element, "UnitDefinitions");

    for (auto& ud_pair : m_unitDefinitions) {
        auto& ud = ud_pair.second;
        rapidxml::xml_node<>* unitNode = doc_ptr->allocate_node(rapidxml::node_element, "Unit");
        unitNode->append_attribute(doc_ptr->allocate_attribute("name", ud.name.c_str()));

        rapidxml::xml_node<>* baseUnitNode = doc_ptr->allocate_node(rapidxml::node_element, "BaseUnit");
        if (ud.kg != 0) {
            stringbuf.push_back(std::to_string(ud.kg));
            baseUnitNode->append_attribute(doc_ptr->allocate_attribute("kg", stringbuf.back().c_str()));
        }
        if (ud.m != 0) {
            stringbuf.push_back(std::to_string(ud.m));
            baseUnitNode->append_attribute(doc_ptr->allocate_attribute("m", stringbuf.back().c_str()));
        }
        if (ud.s != 0) {
            stringbuf.push_back(std::to_string(ud.s));
            baseUnitNode->append_attribute(doc_ptr->allocate_attribute("s", stringbuf.back().c_str()));
        }
        if (ud.A != 0) {
            stringbuf.push_back(std::to_string(ud.A));
            baseUnitNode->append_attribute(doc_ptr->allocate_attribute("A", stringbuf.back().c_str()));
        }
        if (ud.K != 0) {
            stringbuf.push_back(std::to_string(ud.K));
            baseUnitNode->append_attribute(doc_ptr->allocate_attribute("K", stringbuf.back().c_str()));
        }
        if (ud.mol != 0) {
            stringbuf.push_back(std::to_string(ud.mol));
            baseUnitNode->append_attribute(doc_ptr->allocate_attribute("mol", stringbuf.back().c_str()));
        }
        if (ud.cd != 0) {
            stringbuf.push_back(std::to_string(ud.cd));
            baseUnitNode->append_attribute(doc_ptr->allocate_attribute("cd", stringbuf.back().c_str()));
        }
        if (ud.rad != 0) {
            stringbuf.push_back(std::to_string(ud.rad));
            baseUnitNode->append_attribute(doc_ptr->allocate_attribute("rad", stringbuf.back().c_str()));
        }
        unitNode->append_node(baseUnitNode);

        unitDefsNode->append_node(unitNode);
    }
    rootNode->append_node(unitDefsNode);

    // Add LogCategories node
    rapidxml::xml_node<>* logCategoriesNode = doc_ptr->allocate_node(rapidxml::node_element, "LogCategories");
    for (auto& lc : m_logCategories_enabled) {
        rapidxml::xml_node<>* logCategoryNode = doc_ptr->allocate_node(rapidxml::node_element, "Category");
        logCategoryNode->append_attribute(doc_ptr->allocate_attribute("name", lc.first.c_str()));
        logCategoryNode->append_attribute(doc_ptr->allocate_attribute(
            "description", m_logCategories_debug.find(lc.first) == m_logCategories_debug.end() ? "NotDebugCategory"
                                                                                               : "DebugCategory"));
        logCategoriesNode->append_node(logCategoryNode);
    }
    rootNode->append_node(logCategoriesNode);

    // Add DefaultExperiment node
    std::string startTime_str = std::to_string(m_startTime);
    std::string stopTime_str = std::to_string(m_stopTime);
    std::string stepSize_str = std::to_string(m_stepSize);
    std::string tolerance_str = std::to_string(m_tolerance);

    rapidxml::xml_node<>* defaultExpNode = doc_ptr->allocate_node(rapidxml::node_element, "DefaultExperiment");
    defaultExpNode->append_attribute(doc_ptr->allocate_attribute("startTime", startTime_str.c_str()));
    defaultExpNode->append_attribute(doc_ptr->allocate_attribute("stopTime", stopTime_str.c_str()));
    if (m_stepSize > 0)
        defaultExpNode->append_attribute(doc_ptr->allocate_attribute("stepSize", stepSize_str.c_str()));
    if (m_tolerance > 0)
        defaultExpNode->append_attribute(doc_ptr->allocate_attribute("tolerance", tolerance_str.c_str()));
    rootNode->append_node(defaultExpNode);

    // Add ModelVariables node
    rapidxml::xml_node<>* modelVarsNode = doc_ptr->allocate_node(rapidxml::node_element, "ModelVariables");

    // TODO: move elsewhere
    const std::unordered_map<FmuVariable::Type, std::string> Type_strings = {
        {FmuVariable::Type::Float32, "Float32"},  //
        {FmuVariable::Type::Float64, "Float64"},  //
        {FmuVariable::Type::Int8, "Int8"},        //
        {FmuVariable::Type::Int16, "Int16"},      //
        {FmuVariable::Type::Int32, "Int32"},      //
        {FmuVariable::Type::Int64, "Int64"},      //
        {FmuVariable::Type::UInt8, "UInt8"},      //
        {FmuVariable::Type::UInt16, "UInt16"},    //
        {FmuVariable::Type::UInt32, "UInt32"},    //
        {FmuVariable::Type::UInt64, "UInt64"},    //
        {FmuVariable::Type::Boolean, "Boolean"},  //
        {FmuVariable::Type::String, "String"},    //
        {FmuVariable::Type::Binary, "Binary"},    //
        {FmuVariable::Type::Unknown, "Unknown"}   //
    };

    const std::unordered_map<FmuVariable::InitialType, std::string> InitialType_strings = {
        {FmuVariable::InitialType::exact, "exact"},
        {FmuVariable::InitialType::approx, "approx"},
        {FmuVariable::InitialType::calculated, "calculated"}};

    const std::unordered_map<FmuVariable::VariabilityType, std::string> VariabilityType_strings = {
        {FmuVariable::VariabilityType::constant, "constant"},
        {FmuVariable::VariabilityType::fixed, "fixed"},
        {FmuVariable::VariabilityType::tunable, "tunable"},
        {FmuVariable::VariabilityType::discrete, "discrete"},
        {FmuVariable::VariabilityType::continuous, "continuous"}};

    const std::unordered_map<FmuVariable::CausalityType, std::string> CausalityType_strings = {
        {FmuVariable::CausalityType::structuralParameter, "structuralParameter"},
        {FmuVariable::CausalityType::parameter, "parameter"},
        {FmuVariable::CausalityType::calculatedParameter, "calculatedParameter"},
        {FmuVariable::CausalityType::input, "input"},
        {FmuVariable::CausalityType::output, "output"},
        {FmuVariable::CausalityType::local, "local"},
        {FmuVariable::CausalityType::independent, "independent"}};

    // Traverse all variables and cache their valref (map by variable name).
    // Include in list of output variables as appropriate.
    std::unordered_map<std::string, int> allValrefs;  // value references of all variables
    std::vector<int> outputValrefs;                   // value references of output variables

    for (auto it = m_variables.begin(); it != m_variables.end(); ++it) {
        auto valref = it->GetValueReference();
        allValrefs[it->GetName()] = valref;
        if (it->GetCausality() == FmuVariable::CausalityType::output)
            outputValrefs.push_back(valref);
    }

    // Traverse all variables and create XML nodes
    for (auto it = m_variables.begin(); it != m_variables.end(); ++it) {
        // Create the variable node (node name is the variable type)
        rapidxml::xml_node<>* varNode =
            doc_ptr->allocate_node(rapidxml::node_element, Type_strings.at(it->GetType()).c_str());

        // Add variable node attributes
        varNode->append_attribute(doc_ptr->allocate_attribute("name", it->GetName().c_str()));
        stringbuf.push_back(std::to_string(it->GetValueReference()));
        varNode->append_attribute(doc_ptr->allocate_attribute("valueReference", stringbuf.back().c_str()));
        varNode->append_attribute(
            doc_ptr->allocate_attribute("causality", CausalityType_strings.at(it->GetCausality()).c_str()));
        varNode->append_attribute(
            doc_ptr->allocate_attribute("variability", VariabilityType_strings.at(it->GetVariability()).c_str()));
        if (it->GetInitial() != FmuVariable::InitialType::none)
            varNode->append_attribute(
                doc_ptr->allocate_attribute("initial", InitialType_strings.at(it->GetInitial()).c_str()));
        if ((it->GetType() == FmuVariable::Type::Float64 || it->GetType() == FmuVariable::Type::Float32) &&
            !it->GetUnitName().empty())
            varNode->append_attribute(doc_ptr->allocate_attribute("unit", it->GetUnitName().c_str()));

        // Expose Dimension node (if array)
        if (it->GetDimensions().size() > 0) {
            for (auto& dim : it->GetDimensions()) {
                rapidxml::xml_node<>* dimNode = doc_ptr->allocate_node(rapidxml::node_element, "Dimension");
                if (dim.second == true) {
                    stringbuf.push_back(std::to_string(dim.first));
                    dimNode->append_attribute(doc_ptr->allocate_attribute("start", stringbuf.back().c_str()));
                } else {
                    stringbuf.push_back(std::to_string(dim.first));
                    dimNode->append_attribute(doc_ptr->allocate_attribute("valueReference", stringbuf.back().c_str()));
                }
                varNode->append_node(dimNode);
            }
        }

        // Expose start values
        if (it->IsStartValueExposed()) {
            if (it->GetType() == FmuVariable::Type::String || it->GetType() == FmuVariable::Type::Binary) {
                for (auto el_sel = 0; el_sel < GetVariableSize(*it); ++el_sel) {
                    rapidxml::xml_node<>* startNode = doc_ptr->allocate_node(rapidxml::node_element, "Start");
                    stringbuf.push_back(it->GetStartValAsString(el_sel));
                    startNode->append_attribute(doc_ptr->allocate_attribute("value", stringbuf.back().c_str()));
                    varNode->append_node(startNode);
                }
            } else {
                // For all other variables, the start value is given as an attribute of the variable element
                stringbuf.push_back(it->GetStartValAsString(GetVariableSize(*it)));
                varNode->append_attribute(doc_ptr->allocate_attribute("start", stringbuf.back().c_str()));
            }
        }

        // Check if this variable is the derivative of another variable
        auto state_name = isDerivative(it->GetName());
        if (!state_name.empty()) {
            auto state_valref = allValrefs[state_name];
            stringbuf.push_back(std::to_string(state_valref));
            varNode->append_attribute(doc_ptr->allocate_attribute("derivative", stringbuf.back().c_str()));
        }

        if (!it->GetDescription().empty())
            varNode->append_attribute(doc_ptr->allocate_attribute("description", it->GetDescription().c_str()));

        // Append the current variable node
        modelVarsNode->append_node(varNode);
    }

    rootNode->append_node(modelVarsNode);

    // Check that dependencies are defined for all variables that require them
    for (const auto& var : m_variables) {
        auto causality = var.GetCausality();
        auto initial = var.GetInitial();

        if (m_variableDependencies.find(var.GetName()) == m_variableDependencies.end()) {
            if (causality == FmuVariable::CausalityType::output &&
                (initial == FmuVariable::InitialType::approx || initial == FmuVariable::InitialType::calculated)) {
                std::string msg =
                    "Dependencies required for an 'output' variable with initial='approx' or 'calculated' (" +
                    var.GetName() + ").";
                std::cout << "ERROR: " << msg << std::endl;
                throw std::runtime_error(msg);
            }

            if (causality == FmuVariable::CausalityType::calculatedParameter) {
                std::string msg = "Dependencies required for a 'calculatedParameter' variable (" + var.GetName() + ").";
                std::cout << "ERROR: " << msg << std::endl;
                throw std::runtime_error(msg);
            }
        }
    }

    // Add ModelStructure node
    rapidxml::xml_node<>* modelStructNode = doc_ptr->allocate_node(rapidxml::node_element, "ModelStructure");

    //      ...Outputs
    for (int valref : outputValrefs) {
        rapidxml::xml_node<>* outputNode = doc_ptr->allocate_node(rapidxml::node_element, "Output");
        stringbuf.push_back(std::to_string(valref));
        outputNode->append_attribute(doc_ptr->allocate_attribute("valueReference", stringbuf.back().c_str()));
        modelStructNode->append_node(outputNode);
    }

    //     ...Derivatives
    for (const auto& d : m_derivatives) {
        rapidxml::xml_node<>* derivativeNode =
            doc_ptr->allocate_node(rapidxml::node_element, "ContinuousStateDerivative");

        stringbuf.push_back(std::to_string(allValrefs[d.first]));
        derivativeNode->append_attribute(doc_ptr->allocate_attribute("valueReference", stringbuf.back().c_str()));

        std::string dependencies = "";
        for (const auto& dep : d.second.second) {
            dependencies += std::to_string(allValrefs[dep]) + " ";
        }
        stringbuf.push_back(dependencies);
        derivativeNode->append_attribute(doc_ptr->allocate_attribute("dependencies", stringbuf.back().c_str()));

        //// TODO: dependeciesKind

        modelStructNode->append_node(derivativeNode);
    }

    //     ...InitialUnknowns
    for (const auto& v : m_variableDependencies) {
        rapidxml::xml_node<>* unknownNode = doc_ptr->allocate_node(rapidxml::node_element, "InitialUnknown");
        auto v_index = allValrefs[v.first];
        stringbuf.push_back(std::to_string(v_index));
        unknownNode->append_attribute(doc_ptr->allocate_attribute("valueReference", stringbuf.back().c_str()));
        std::string dependencies = "";
        for (const auto& d : v.second) {
            auto d_index = allValrefs[d];
            dependencies += std::to_string(d_index) + " ";
        }
        stringbuf.push_back(dependencies);
        unknownNode->append_attribute(doc_ptr->allocate_attribute("dependencies", stringbuf.back().c_str()));
        modelStructNode->append_node(unknownNode);
    }

    rootNode->append_node(modelStructNode);

    // Save the XML document to a file
    std::ofstream outFile(path + "/modelDescription.xml");
    outFile << *doc_ptr;
    outFile.close();

    delete doc_ptr;

    postModelDescriptionExport();
}

// -----------------------------------------------------------------------------

std::vector<size_t> FmuComponentBase::GetVariableDimensions(fmi3ValueReference valref) const {
    return GetVariableDimensions(*findByValref(valref));
}

size_t FmuComponentBase::GetVariableSize(const FmuVariable& var) const {
    if (var.GetDimensions().size() == 0)
        return 1;

    size_t size = 1;
    for (const auto& dim : GetVariableDimensions(var)) {
        size *= dim;
    }
    return size;
}

void FmuComponentBase::executePreStepCallbacks() {
    for (auto& callb : m_preStepCallbacks) {
        callb();
    }
}

void FmuComponentBase::executePostStepCallbacks() {
    for (auto& callb : m_postStepCallbacks) {
        callb();
    }
}

std::set<FmuVariableExport>::iterator FmuComponentBase::findByValref(fmi3ValueReference vr) {
    auto predicate_samevalref = [vr](const FmuVariable& var) { return var.GetValueReference() == vr; };
    return std::find_if(m_variables.begin(), m_variables.end(), predicate_samevalref);
}

std::set<FmuVariableExport>::const_iterator FmuComponentBase::findByValref(fmi3ValueReference vr) const {
    auto predicate_samevalref = [vr](const FmuVariable& var) { return var.GetValueReference() == vr; };
    return std::find_if(m_variables.begin(), m_variables.end(), predicate_samevalref);
}

std::set<FmuVariableExport>::iterator FmuComponentBase::findByName(const std::string& name) {
    auto predicate_samename = [name](const FmuVariable& var) { return !var.GetName().compare(name); };
    return std::find_if(m_variables.begin(), m_variables.end(), predicate_samename);
}

std::vector<size_t> FmuComponentBase::GetVariableDimensions(const FmuVariable& var) const {
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
            auto it_dim = findByValref(static_cast<fmi3ValueReference>(d.first));
            it_dim->GetValue(&cur_size, 1);
            sizes.push_back(cur_size);
        }
    }

    return sizes;
}

// -----------------------------------------------------------------------------

fmi3Status FmuComponentBase::EnterInitializationMode(fmi3Boolean toleranceDefined,
                                                     fmi3Float64 tolerance,
                                                     fmi3Float64 startTime,
                                                     fmi3Boolean stopTimeDefined,
                                                     fmi3Float64 stopTime) {
    m_startTime = startTime;
    m_stopTime = stopTime;
    m_tolerance = tolerance;
    m_toleranceDefined = toleranceDefined;
    m_stopTimeDefined = stopTimeDefined;

    m_fmuMachineState = FmuMachineState::initializationMode;

    fmi3Status status = enterInitializationModeIMPL();

    if (status == fmi3Status::fmi3Fatal) {
        m_fmuMachineState = FmuMachineState::terminated;
    }

    return status;
}

fmi3Status FmuComponentBase::ExitInitializationMode() {
    fmi3Status status = exitInitializationModeIMPL();

    switch (m_fmuType) {
        case FmuType::MODEL_EXCHANGE:
            m_fmuMachineState = FmuMachineState::continuousTimeMode;
            break;
        case FmuType::COSIMULATION:
            m_fmuMachineState = m_eventModeUsed ? FmuMachineState::eventMode : FmuMachineState::stepMode;
            break;
        case FmuType::SCHEDULED_EXECUTION:
            m_fmuMachineState = FmuMachineState::clockActivationMode;
            break;
    }

    if (status == fmi3Status::fmi3Fatal) {
        m_fmuMachineState = FmuMachineState::terminated;
    }

    return status;
}

fmi3Status FmuComponentBase::UpdateDiscreteStates(fmi3Boolean* discreteStatesNeedUpdate,
                                                  fmi3Boolean* terminateSimulation,
                                                  fmi3Boolean* nominalsOfContinuousStatesChanged,
                                                  fmi3Boolean* valuesOfContinuousStatesChanged,
                                                  fmi3Boolean* nextEventTimeDefined,
                                                  fmi3Float64* nextEventTime) {
    fmi3Status status =
        updateDiscreteStatesIMPL(discreteStatesNeedUpdate, terminateSimulation, nominalsOfContinuousStatesChanged,
                                 valuesOfContinuousStatesChanged, nextEventTimeDefined, nextEventTime);

    if (status == fmi3Status::fmi3Fatal) {
        m_fmuMachineState = FmuMachineState::terminated;
    }
    return status;
}

fmi3Status FmuComponentBase::DoStep(fmi3Float64 currentCommunicationPoint,
                                    fmi3Float64 communicationStepSize,
                                    fmi3Boolean noSetFMUStatePriorToCurrentPoint,
                                    fmi3Boolean* eventHandlingNeeded,
                                    fmi3Boolean* terminateSimulation,
                                    fmi3Boolean* earlyReturn,
                                    fmi3Float64* lastSuccessfulTime) {
    // invoke any pre step callbacks (e.g., to process input variables)
    executePreStepCallbacks();

    fmi3Status status = doStepIMPL(currentCommunicationPoint, communicationStepSize, noSetFMUStatePriorToCurrentPoint,
                                   eventHandlingNeeded, terminateSimulation, earlyReturn, lastSuccessfulTime);

    // invoke any post step callbacks (e.g., to update auxiliary variables)
    executePostStepCallbacks();

    return status;
}

fmi3Status FmuComponentBase::CompletedIntegratorStep(fmi3Boolean noSetFMUStatePriorToCurrentPoint,
                                                     fmi3Boolean* enterEventMode,
                                                     fmi3Boolean* terminateSimulation) {
    fmi3Status status =
        completedIntegratorStepIMPL(noSetFMUStatePriorToCurrentPoint, enterEventMode, terminateSimulation);

    return status;
}

fmi3Status FmuComponentBase::SetTime(fmi3Float64 time) {
    m_time = time;

    fmi3Status status = setTimeIMPL(time);

    //// TODO - anything else here?

    return status;
}

fmi3Status FmuComponentBase::GetContinuousStates(fmi3Float64 continuousStates[], size_t nContinuousStates) {
    fmi3Status status = getContinuousStatesIMPL(continuousStates, nContinuousStates);

    //// TODO - interpret/process status?

    return status;
}

fmi3Status FmuComponentBase::SetContinuousStates(const fmi3Float64 continuousStates[], size_t nContinuousStates) {
    fmi3Status status = setContinuousStatesIMPL(continuousStates, nContinuousStates);

    //// TODO - interpret/process status?
    ////   Set FMU machine state (m_fmuMachineState)

    return status;
}

fmi3Status FmuComponentBase::GetDerivatives(fmi3Float64 derivatives[], size_t nContinuousStates) {
    // invoke any pre step callbacks (e.g., to process input variables)
    executePreStepCallbacks();

    fmi3Status status = getDerivativesIMPL(derivatives, nContinuousStates);

    // invoke any post step callbacks (e.g., to update auxiliary variables)
    executePostStepCallbacks();

    //// TODO - interpret/process status?
    ////   Set FMU machine state (m_fmuMachineState)

    return status;
}

void FmuComponentBase::addUnitDefinition(const UnitDefinition& unit_definition) {
    m_unitDefinitions[unit_definition.name] = unit_definition;
}

void FmuComponentBase::sendToLog(std::string msg, fmi3Status status, std::string msg_cat) {
    assert(m_logCategories_enabled.find(msg_cat) != m_logCategories_enabled.end() &&
           ("Developer warning: the category \"" + msg_cat + "\" is not recognized by the FMU").c_str());

    if (m_logCategories_enabled.find(msg_cat) == m_logCategories_enabled.end() ||                        //
        m_logCategories_enabled[msg_cat] ||                                                              //
        (m_debug_logging_enabled && m_logCategories_debug.find(msg_cat) != m_logCategories_debug.end())  //
    ) {
        m_logMessage(m_instanceEnvironment, status, msg_cat.c_str(), msg.c_str());
    }
}

}  // namespace fmi3
}  // namespace fmu_forge

// =============================================================================
// FMU FUNCTIONS
// =============================================================================

using namespace fmu_forge::fmi3;

// ------ Inquire version numbers and set debug logging

const char* fmi3GetVersion(void) {
    return fmi3Version;
}

fmi3Status fmi3SetDebugLogging(fmi3Instance instance,
                               fmi3Boolean loggingOn,
                               size_t nCategories,
                               const fmi3String categories[]) {
    FmuComponentBase* fmu_ptr = reinterpret_cast<FmuComponentBase*>(instance);
    for (auto cs = 0; cs < nCategories; ++cs) {
        fmu_ptr->SetDebugLogging(categories[cs], loggingOn == fmi3True ? true : false);
    }

    return fmi3Status::fmi3OK;
}

// ------ Creation and destruction of FMU instances

fmi3Instance fmi3InstantiateModelExchange(fmi3String instanceName,
                                          fmi3String instantiationToken,
                                          fmi3String resourcePath,
                                          fmi3Boolean visible,
                                          fmi3Boolean loggingOn,
                                          fmi3InstanceEnvironment instanceEnvironment,
                                          fmi3LogMessageCallback logMessage) {
    FmuComponentBase* fmu_ptr = fmi3InstantiateIMPL(FmuType::MODEL_EXCHANGE, instanceName, instantiationToken,
                                                    resourcePath, visible, loggingOn, instanceEnvironment, logMessage);
    return reinterpret_cast<void*>(fmu_ptr);
}

fmi3Instance fmi3InstantiateCoSimulation(fmi3String instanceName,
                                         fmi3String instantiationToken,
                                         fmi3String resourcePath,
                                         fmi3Boolean visible,
                                         fmi3Boolean loggingOn,
                                         fmi3Boolean eventModeUsed,
                                         fmi3Boolean earlyReturnAllowed,
                                         const fmi3ValueReference requiredIntermediateVariables[],
                                         size_t nRequiredIntermediateVariables,
                                         fmi3InstanceEnvironment instanceEnvironment,
                                         fmi3LogMessageCallback logMessage,
                                         fmi3IntermediateUpdateCallback intermediateUpdate) {
    FmuComponentBase* fmu_ptr = fmi3InstantiateIMPL(FmuType::COSIMULATION, instanceName, instantiationToken,
                                                    resourcePath, visible, loggingOn, instanceEnvironment, logMessage);
    if (!fmu_ptr)
        return nullptr;

    //// RADU TODO - set cosimulation-specific flags on the FMU (based on input arguments)
    fmu_ptr->SetIntermediateUpdateCallback(intermediateUpdate);

    return reinterpret_cast<void*>(fmu_ptr);
}

fmi3Instance fmi3InstantiateScheduledExecution(fmi3String instanceName,
                                               fmi3String instantiationToken,
                                               fmi3String resourcePath,
                                               fmi3Boolean visible,
                                               fmi3Boolean loggingOn,
                                               fmi3InstanceEnvironment instanceEnvironment,
                                               fmi3LogMessageCallback logMessage,
                                               fmi3ClockUpdateCallback clockUpdate,
                                               fmi3LockPreemptionCallback lockPreemption,
                                               fmi3UnlockPreemptionCallback unlockPreemption) {
    // Not supported in fmu-forge
    logMessage(instanceEnvironment, fmi3Status::fmi3Error, "logStatusError",
               "fmu-forge does not currently support the ScheduledExecution interface.\n");
    return nullptr;
}

void fmi3FreeInstance(fmi3Instance instance) {
    delete reinterpret_cast<FmuComponentBase*>(instance);
}

// ------ Enter and exit initialization mode, terminate and reset

fmi3Status fmi3EnterInitializationMode(fmi3Instance instance,
                                       fmi3Boolean toleranceDefined,
                                       fmi3Float64 tolerance,
                                       fmi3Float64 startTime,
                                       fmi3Boolean stopTimeDefined,
                                       fmi3Float64 stopTime) {
    return reinterpret_cast<FmuComponentBase*>(instance)->EnterInitializationMode(toleranceDefined, tolerance,
                                                                                  startTime, stopTimeDefined, stopTime);
}

fmi3Status fmi3ExitInitializationMode(fmi3Instance instance) {
    return reinterpret_cast<FmuComponentBase*>(instance)->ExitInitializationMode();
}

fmi3Status fmi3EnterEventMode(fmi3Instance instance) {
    return fmi3Status::fmi3OK;
}

fmi3Status fmi3Terminate(fmi3Instance instance) {
    return fmi3Status::fmi3OK;
}

fmi3Status fmi3Reset(fmi3Instance instance) {
    return fmi3Status::fmi3OK;
}

// ------ Getting variable values

fmi3Status fmi3GetFloat32(fmi3Instance instance,
                          const fmi3ValueReference valueReferences[],
                          size_t nValueReferences,
                          fmi3Float32 values[],
                          size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3GetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3GetFloat64(fmi3Instance instance,
                          const fmi3ValueReference valueReferences[],
                          size_t nValueReferences,
                          fmi3Float64 values[],
                          size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3GetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3GetInt8(fmi3Instance instance,
                       const fmi3ValueReference valueReferences[],
                       size_t nValueReferences,
                       fmi3Int8 values[],
                       size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3GetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3GetUInt8(fmi3Instance instance,
                        const fmi3ValueReference valueReferences[],
                        size_t nValueReferences,
                        fmi3UInt8 values[],
                        size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3GetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3GetInt16(fmi3Instance instance,
                        const fmi3ValueReference valueReferences[],
                        size_t nValueReferences,
                        fmi3Int16 values[],
                        size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3GetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3GetUInt16(fmi3Instance instance,
                         const fmi3ValueReference valueReferences[],
                         size_t nValueReferences,
                         fmi3UInt16 values[],
                         size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3GetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3GetInt32(fmi3Instance instance,
                        const fmi3ValueReference valueReferences[],
                        size_t nValueReferences,
                        fmi3Int32 values[],
                        size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3GetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3GetUInt32(fmi3Instance instance,
                         const fmi3ValueReference valueReferences[],
                         size_t nValueReferences,
                         fmi3UInt32 values[],
                         size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3GetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3GetInt64(fmi3Instance instance,
                        const fmi3ValueReference valueReferences[],
                        size_t nValueReferences,
                        fmi3Int64 values[],
                        size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3GetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3GetUInt64(fmi3Instance instance,
                         const fmi3ValueReference valueReferences[],
                         size_t nValueReferences,
                         fmi3UInt64 values[],
                         size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3GetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3GetBoolean(fmi3Instance instance,
                          const fmi3ValueReference valueReferences[],
                          size_t nValueReferences,
                          fmi3Boolean values[],
                          size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3GetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3GetString(fmi3Instance instance,
                         const fmi3ValueReference valueReferences[],
                         size_t nValueReferences,
                         fmi3String values[],
                         size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3GetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3GetBinary(fmi3Instance instance,
                         const fmi3ValueReference valueReferences[],
                         size_t nValueReferences,
                         size_t valueSizes[],
                         fmi3Binary values[],
                         size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3GetVariable(valueReferences, nValueReferences, valueSizes,
                                                                          values, nValues);
}

fmi3Status fmi3GetClock(fmi3Instance instance,
                        const fmi3ValueReference valueReferences[],
                        size_t nValueReferences,
                        fmi3Clock values[]) {
    //// RADU TODO
    return fmi3Status::fmi3Error;
}

// ------ Setting variable values

fmi3Status fmi3SetFloat32(fmi3Instance instance,
                          const fmi3ValueReference valueReferences[],
                          size_t nValueReferences,
                          const fmi3Float32 values[],
                          size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3SetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3SetFloat64(fmi3Instance instance,
                          const fmi3ValueReference valueReferences[],
                          size_t nValueReferences,
                          const fmi3Float64 values[],
                          size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3SetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3SetInt8(fmi3Instance instance,
                       const fmi3ValueReference valueReferences[],
                       size_t nValueReferences,
                       const fmi3Int8 values[],
                       size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3SetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3SetUInt8(fmi3Instance instance,
                        const fmi3ValueReference valueReferences[],
                        size_t nValueReferences,
                        const fmi3UInt8 values[],
                        size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3SetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3SetInt16(fmi3Instance instance,
                        const fmi3ValueReference valueReferences[],
                        size_t nValueReferences,
                        const fmi3Int16 values[],
                        size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3SetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3SetUInt16(fmi3Instance instance,
                         const fmi3ValueReference valueReferences[],
                         size_t nValueReferences,
                         const fmi3UInt16 values[],
                         size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3SetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3SetInt32(fmi3Instance instance,
                        const fmi3ValueReference valueReferences[],
                        size_t nValueReferences,
                        const fmi3Int32 values[],
                        size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3SetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3SetUInt32(fmi3Instance instance,
                         const fmi3ValueReference valueReferences[],
                         size_t nValueReferences,
                         const fmi3UInt32 values[],
                         size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3SetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3SetInt64(fmi3Instance instance,
                        const fmi3ValueReference valueReferences[],
                        size_t nValueReferences,
                        const fmi3Int64 values[],
                        size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3SetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3SetUInt64(fmi3Instance instance,
                         const fmi3ValueReference valueReferences[],
                         size_t nValueReferences,
                         const fmi3UInt64 values[],
                         size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3SetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3SetBoolean(fmi3Instance instance,
                          const fmi3ValueReference valueReferences[],
                          size_t nValueReferences,
                          const fmi3Boolean values[],
                          size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3SetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3SetString(fmi3Instance instance,
                         const fmi3ValueReference valueReferences[],
                         size_t nValueReferences,
                         const fmi3String values[],
                         size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3SetVariable(valueReferences, nValueReferences, values,
                                                                          nValues);
}

fmi3Status fmi3SetBinary(fmi3Instance instance,
                         const fmi3ValueReference valueReferences[],
                         size_t nValueReferences,
                         const size_t valueSizes[],
                         const fmi3Binary values[],
                         size_t nValues) {
    return reinterpret_cast<FmuComponentBase*>(instance)->fmi3SetVariable(valueReferences, nValueReferences, valueSizes,
                                                                          values, nValues);
}

fmi3Status fmi3SetClock(fmi3Instance instance,
                        const fmi3ValueReference valueReferences[],
                        size_t nValueReferences,
                        const fmi3Clock values[]) {
    //// RADU TODO
    return fmi3Status::fmi3Error;
}

// ------ Getting Variable Dependency Information

fmi3Status fmi3GetNumberOfVariableDependencies(fmi3Instance instance,
                                               fmi3ValueReference valueReference,
                                               size_t* nDependencies) {
    return fmi3Status::fmi3Error;
}

fmi3Status fmi3GetVariableDependencies(fmi3Instance instance,
                                       fmi3ValueReference dependent,
                                       size_t elementIndicesOfDependent[],
                                       fmi3ValueReference independents[],
                                       size_t elementIndicesOfIndependents[],
                                       fmi3DependencyKind dependencyKinds[],
                                       size_t nDependencies) {
    return fmi3Status::fmi3Error;
}

// ------ Getting and setting the internal FMU state

fmi3Status fmi3GetFMUState(fmi3Instance instance, fmi3FMUState* FMUState) {
    return fmi3Status::fmi3Error;
}
fmi3Status fmi3SetFMUState(fmi3Instance instance, fmi3FMUState FMUState) {
    return fmi3Status::fmi3Error;
}
fmi3Status fmi3FreeFMUState(fmi3Instance instance, fmi3FMUState* FMUState) {
    return fmi3Status::fmi3Error;
}
fmi3Status fmi3SerializedFMUStateSize(fmi3Instance instance, fmi3FMUState FMUState, size_t* size) {
    return fmi3Status::fmi3Error;
}
fmi3Status fmi3SerializeFMUState(fmi3Instance instance,
                                 fmi3FMUState FMUState,
                                 fmi3Byte serializedState[],
                                 size_t size) {
    return fmi3Status::fmi3Error;
}
fmi3Status fmi3DeserializeFMUState(fmi3Instance instance,
                                   const fmi3Byte serializedState[],
                                   size_t size,
                                   fmi3FMUState* FMUState) {
    return fmi3Status::fmi3Error;
}

// ------ Getting partial derivatives
fmi3Status fmi3GetDirectionalDerivative(fmi3Instance instance,
                                        const fmi3ValueReference unknowns[],
                                        size_t nUnknowns,
                                        const fmi3ValueReference knowns[],
                                        size_t nKnowns,
                                        const fmi3Float64 seed[],
                                        size_t nSeed,
                                        fmi3Float64 sensitivity[],
                                        size_t nSensitivity) {
    return fmi3Status::fmi3Error;
}

fmi3Status fmi3GetAdjointDerivative(fmi3Instance instance,
                                    const fmi3ValueReference unknowns[],
                                    size_t nUnknowns,
                                    const fmi3ValueReference knowns[],
                                    size_t nKnowns,
                                    const fmi3Float64 seed[],
                                    size_t nSeed,
                                    fmi3Float64 sensitivity[],
                                    size_t nSensitivity) {
    return fmi3Status::fmi3Error;
}

// ------ Entering and exiting the Configuration or Reconfiguration Mode

fmi3Status fmi3EnterConfigurationMode(fmi3Instance instance) {
    //// RADU TODO
    return fmi3Status::fmi3Error;
}

fmi3Status fmi3ExitConfigurationMode(fmi3Instance instance) {
    //// RADU TODO
    return fmi3Status::fmi3Error;
}

// ------ Clock related functions

fmi3Status fmi3GetIntervalDecimal(fmi3Instance instance,
                                  const fmi3ValueReference valueReferences[],
                                  size_t nValueReferences,
                                  fmi3Float64 intervals[],
                                  fmi3IntervalQualifier qualifiers[]) {
    return fmi3Status::fmi3Error;
}
fmi3Status fmi3GetIntervalFraction(fmi3Instance instance,
                                   const fmi3ValueReference valueReferences[],
                                   size_t nValueReferences,
                                   fmi3UInt64 counters[],
                                   fmi3UInt64 resolutions[],
                                   fmi3IntervalQualifier qualifiers[]) {
    return fmi3Status::fmi3Error;
}
fmi3Status fmi3GetShiftDecimal(fmi3Instance instance,
                               const fmi3ValueReference valueReferences[],
                               size_t nValueReferences,
                               fmi3Float64 shifts[]) {
    return fmi3Status::fmi3Error;
}
fmi3Status fmi3GetShiftFraction(fmi3Instance instance,
                                const fmi3ValueReference valueReferences[],
                                size_t nValueReferences,
                                fmi3UInt64 counters[],
                                fmi3UInt64 resolutions[]) {
    return fmi3Status::fmi3Error;
}
fmi3Status fmi3SetIntervalDecimal(fmi3Instance instance,
                                  const fmi3ValueReference valueReferences[],
                                  size_t nValueReferences,
                                  const fmi3Float64 intervals[]) {
    return fmi3Status::fmi3Error;
}
fmi3Status fmi3SetIntervalFraction(fmi3Instance instance,
                                   const fmi3ValueReference valueReferences[],
                                   size_t nValueReferences,
                                   const fmi3UInt64 counters[],
                                   const fmi3UInt64 resolutions[]) {
    return fmi3Status::fmi3Error;
}
fmi3Status fmi3SetShiftDecimal(fmi3Instance instance,
                               const fmi3ValueReference valueReferences[],
                               size_t nValueReferences,
                               const fmi3Float64 shifts[]) {
    return fmi3Status::fmi3Error;
}
fmi3Status fmi3SetShiftFraction(fmi3Instance instance,
                                const fmi3ValueReference valueReferences[],
                                size_t nValueReferences,
                                const fmi3UInt64 counters[],
                                const fmi3UInt64 resolutions[]) {
    return fmi3Status::fmi3Error;
}
fmi3Status fmi3EvaluateDiscreteStates(fmi3Instance instance) {
    return fmi3Status::fmi3Error;
}
fmi3Status fmi3UpdateDiscreteStates(fmi3Instance instance,
                                    fmi3Boolean* discreteStatesNeedUpdate,
                                    fmi3Boolean* terminateSimulation,
                                    fmi3Boolean* nominalsOfContinuousStatesChanged,
                                    fmi3Boolean* valuesOfContinuousStatesChanged,
                                    fmi3Boolean* nextEventTimeDefined,
                                    fmi3Float64* nextEventTime) {
    return reinterpret_cast<FmuComponentBase*>(instance)->UpdateDiscreteStates(
        discreteStatesNeedUpdate, terminateSimulation, nominalsOfContinuousStatesChanged,
        valuesOfContinuousStatesChanged, nextEventTimeDefined, nextEventTime);
}

//////////////////////////////////////////

// ------ Model Exchange

fmi3Status fmi3EnterContinuousTimeMode(fmi3Instance instance) {
    return fmi3Status::fmi3Error;
}

fmi3Status fmi3CompletedIntegratorStep(fmi3Instance instance,
                                       fmi3Boolean noSetFMUStatePriorToCurrentPoint,
                                       fmi3Boolean* enterEventMode,
                                       fmi3Boolean* terminateSimulation) {
    return reinterpret_cast<FmuComponentBase*>(instance)->CompletedIntegratorStep(noSetFMUStatePriorToCurrentPoint,
                                                                                  enterEventMode, terminateSimulation);
}

fmi3Status fmi3SetTime(fmi3Instance instance, fmi3Float64 time) {
    return reinterpret_cast<FmuComponentBase*>(instance)->SetTime(time);
}

fmi3Status fmi3SetContinuousStates(fmi3Instance instance,
                                   const fmi3Float64 continuousStates[],
                                   size_t nContinuousStates) {
    return reinterpret_cast<FmuComponentBase*>(instance)->SetContinuousStates(continuousStates, nContinuousStates);
}

fmi3Status fmi3GetContinuousStateDerivatives(fmi3Instance instance,
                                             fmi3Float64 derivatives[],
                                             size_t nContinuousStates) {
    return reinterpret_cast<FmuComponentBase*>(instance)->GetDerivatives(derivatives, nContinuousStates);
}

fmi3Status fmi3GetEventIndicators(fmi3Instance instance, fmi3Float64 eventIndicators[], size_t nEventIndicators) {
    return fmi3Status::fmi3Error;
}

fmi3Status fmi3GetContinuousStates(fmi3Instance instance, fmi3Float64 continuousStates[], size_t nContinuousStates) {
    return reinterpret_cast<FmuComponentBase*>(instance)->GetContinuousStates(continuousStates, nContinuousStates);
}

fmi3Status fmi3GetNominalsOfContinuousStates(fmi3Instance instance, fmi3Float64 nominals[], size_t nContinuousStates) {
    return fmi3Status::fmi3Error;
}

fmi3Status fmi3GetNumberOfEventIndicators(fmi3Instance instance, size_t* nEventIndicators) {
    return fmi3Status::fmi3Error;
}

fmi3Status fmi3GetNumberOfContinuousStates(fmi3Instance instance, size_t* nContinuousStates) {
    return fmi3Status::fmi3Error;
}

// ------ Co-Simulation

fmi3Status fmi3EnterStepMode(fmi3Instance instance) {
    return fmi3Status::fmi3Error;
}

fmi3Status fmi3GetOutputDerivatives(fmi3Instance instance,
                                    const fmi3ValueReference valueReferences[],
                                    size_t nValueReferences,
                                    const fmi3Int32 orders[],
                                    fmi3Float64 values[],
                                    size_t nValues) {
    return fmi3Status::fmi3Error;
}

fmi3Status fmi3DoStep(fmi3Instance instance,
                      fmi3Float64 currentCommunicationPoint,
                      fmi3Float64 communicationStepSize,
                      fmi3Boolean noSetFMUStatePriorToCurrentPoint,
                      fmi3Boolean* eventHandlingNeeded,
                      fmi3Boolean* terminateSimulation,
                      fmi3Boolean* earlyReturn,
                      fmi3Float64* lastSuccessfulTime) {
    return reinterpret_cast<FmuComponentBase*>(instance)->DoStep(currentCommunicationPoint, communicationStepSize,
                                                                 noSetFMUStatePriorToCurrentPoint, eventHandlingNeeded,
                                                                 terminateSimulation, earlyReturn, lastSuccessfulTime);
}

// ------ Scheduled Execution

fmi3Status fmi3ActivateModelPartition(fmi3Instance instance,
                                      fmi3ValueReference clockReference,
                                      fmi3Float64 activationTime) {
    return fmi3Status::fmi3Error;
}
