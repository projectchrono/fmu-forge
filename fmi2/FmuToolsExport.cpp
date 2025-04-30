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

#include <regex>
#include <fstream>

#include "fmi2/FmuToolsExport.h"
#include "FmuToolsRuntimeLinking.h"

#include "rapidxml/rapidxml_ext.hpp"

namespace fmu_forge {
namespace fmi2 {

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
    fmi2CallbackFunctions callfun = {LoggingUtilities::logger_default, calloc, free, nullptr, nullptr};

    try {
        fmu = fmi2InstantiateIMPL("",                                                                //
                                  fmi2Type::fmi2CoSimulation,                                        //
                                  FMU_GUID,                                                          //
                                  ("file:///" + GetLibraryLocation() + "/../../resources").c_str(),  //
                                  &callfun,                                                          //
                                  fmi2False, fmi2False                                               //
        );
    } catch (std::exception& e) {
        has_cosim = false;
        e_cosim = e;
    }
    try {
        fmu = fmi2InstantiateIMPL("",                                                                //
                                  fmi2Type::fmi2ModelExchange,                                       //
                                  FMU_GUID,                                                          //
                                  ("file:///" + GetLibraryLocation() + "/../../resources").c_str(),  //
                                  &callfun,                                                          //
                                  fmi2False, fmi2False                                               //
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
    : FmuVariable(name, type, causality, variability, initial) {
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
    m_start = other.m_start;
    m_allowed_start = other.m_allowed_start;
    m_required_start = other.m_required_start;
}

FmuVariableExport& FmuVariableExport::operator=(const FmuVariableExport& other) {
    if (this == &other) {
        return *this;  // Self-assignment guard
    }

    FmuVariable::operator=(other);

    m_varbind = other.m_varbind;
    m_start = other.m_start;
    m_allowed_start = other.m_allowed_start;
    m_required_start = other.m_required_start;

    return *this;
}

void FmuVariableExport::SetValue(const fmi2String& val) const {
    if (is_pointer_variant(m_varbind))
        *varns::get<std::string*>(m_varbind) = std::string(val);
    else
        varns::get<FunGetSet<std::string>>(m_varbind).second(std::string(val));
}

void FmuVariableExport::GetValue(fmi2String* varptr) const {
    *varptr = is_pointer_variant(m_varbind) ? varns::get<std::string*>(m_varbind)->c_str()
                                            : varns::get<FunGetSet<std::string>>(m_varbind).first().c_str();
}

void FmuVariableExport::SetStartVal(fmi2String start) {
    if (!m_allowed_start)
        return;
    m_has_start = true;
    m_start = std::string(start);
}

void FmuVariableExport::ExposeCurrentValueAsStart() {
    if (m_required_start) {
        varns::visit([this](auto&& arg) { this->setStartFromVar(arg); }, m_varbind);

        // if (is_pointer_variant(m_varbind)){
        //     // check if string TODO: check if this check is needed .-)
        //     if (varns::holds_alternative<fmi2String*>(m_varbind)) {
        //         // TODO
        //         varns::visit([this](auto& arg){ return m_start = arg; }, m_varbind);
        //     }
        //     else{
        //         varns::visit([this](auto& arg){ return this->SetStartVal(*arg); }, m_varbind);
        //     }
        // }
        // else{

        //}
    }
}

std::string FmuVariableExport::GetStartValAsString() const {
    // TODO: C++17 would allow the overload operator in lambda
    // std::string start_string;
    // varns::visit([&start_string](auto&& arg) -> std::string {return start_string = std::to_string(*start_ptr)});

    if (const fmi2Real* start_ptr = varns::get_if<fmi2Real>(&m_start))
        return std::to_string(*start_ptr);
    if (const fmi2Integer* start_ptr = varns::get_if<fmi2Integer>(&m_start))
        return std::to_string(*start_ptr);
    if (const fmi2Boolean* start_ptr = varns::get_if<fmi2Boolean>(&m_start))
        return std::to_string(*start_ptr);
    if (const std::string* start_ptr = varns::get_if<std::string>(&m_start))
        return *start_ptr;
    return "";
}

// =============================================================================

FmuComponentBase::FmuComponentBase(fmi2String instanceName,
                                   fmi2Type fmuType,
                                   fmi2String fmuGUID,
                                   fmi2String fmuResourceLocation,
                                   const fmi2CallbackFunctions* functions,
                                   fmi2Boolean visible,
                                   fmi2Boolean loggingOn,
                                   const std::unordered_map<std::string, bool>& logCategories_init,
                                   const std::unordered_set<std::string>& logCategories_debug_init)
    : m_instanceName(instanceName),
      m_callbackFunctions(*functions),
      m_fmuGUID(FMU_GUID),
      m_visible(visible == fmi2True ? true : false),
      m_debug_logging_enabled(loggingOn == fmi2True ? true : false),
      m_modelIdentifier(FMU_MODEL_IDENTIFIER),
      m_fmuMachineState(FmuMachineState::instantiated),
      m_logCategories_enabled(logCategories_init),
      m_logCategories_debug(logCategories_debug_init) {
    m_unitDefinitions["1"] = UnitDefinition("1");  // guarantee the existence of the default unit
    m_unitDefinitions[""] = UnitDefinition("");    // guarantee the existence of the unassigned unit

    AddFmuVariable(&m_time, "time", FmuVariable::Type::Real, "s", "time",  //
                   FmuVariable::CausalityType::independent,                //
                   FmuVariable::VariabilityType::continuous);

    // Parse URL according to https://datatracker.ietf.org/doc/html/rfc3986
    std::string m_resources_location_str = std::string(fmuResourceLocation);

    std::regex url_patternA("^(\\w+):\\/\\/[^\\/]*\\/([^#\\?]+)");
    std::regex url_patternB("^(\\w+):\\/([^\\/][^#\\?]+)");

    std::smatch url_matches;
    std::string url_scheme;
    if ((std::regex_search(m_resources_location_str, url_matches, url_patternA) ||
         std::regex_search(m_resources_location_str, url_matches, url_patternB)) &&
        url_matches.size() >= 3) {
        if (url_matches[1].compare("file") != 0) {
            sendToLog("Bad URL scheme: " + url_matches[1].str() + ". Trying to continue.\n", fmi2Status::fmi2Warning,
                      "logStatusWarning");
        }
        m_resources_location = std::string(url_matches[2]) + "/";
    } else {
        // TODO: rollback?
        sendToLog("Cannot parse resource location: " + m_resources_location_str + "\n", fmi2Status::fmi2Warning,
                  "logStatusWarning");

        m_resources_location = GetLibraryLocation() + "/../../resources/";
        sendToLog("Rolled back to default location: " + m_resources_location + "\n", fmi2Status::fmi2Warning,
                  "logStatusWarning");
    }

    // Compare GUID
    if (std::string(fmuGUID).compare(m_fmuGUID)) {
        sendToLog("GUID used for instantiation not matching with source.\n", fmi2Status::fmi2Warning,
                  "logStatusWarning");
    }

    for (auto deb_sel = logCategories_debug_init.begin(); deb_sel != logCategories_debug_init.end(); ++deb_sel) {
        if (logCategories_init.find(*deb_sel) == logCategories_init.end()) {
            sendToLog("Developer error: Log category \"" + *deb_sel +
                          "\" specified to be of debug is not listed as a log category.\n",
                      fmi2Status::fmi2Warning, "logStatusWarning");
        }
    }
}

void FmuComponentBase::initializeType(fmi2Type fmuType) {
    switch (fmuType) {
        case fmi2Type::fmi2CoSimulation:
            if (!is_cosimulation_available())
                throw std::runtime_error("Requested CoSimulation FMU mode but it is not available.");
            this->m_fmuType = fmi2Type::fmi2CoSimulation;
            break;
        case fmi2Type::fmi2ModelExchange:
            if (!is_modelexchange_available())
                throw std::runtime_error("Requested ModelExchange FMU mode but it is not available.");
            this->m_fmuType = fmi2Type::fmi2ModelExchange;
            break;
        default:
            throw std::runtime_error("Requested unrecognized FMU type.");
            break;
    }
}

void FmuComponentBase::SetDefaultExperiment(fmi2Boolean toleranceDefined,
                                            fmi2Real tolerance,
                                            fmi2Real startTime,
                                            fmi2Boolean stopTimeDefined,
                                            fmi2Real stopTime) {
    m_startTime = startTime;
    m_stopTime = stopTime;
    m_tolerance = tolerance;
    m_toleranceDefined = toleranceDefined;
    m_stopTimeDefined = stopTimeDefined;
}

void FmuComponentBase::SetDebugLogging(std::string cat, bool value) {
    try {
        m_logCategories_enabled[cat] = value;
    } catch (const std::exception&) {
        sendToLog("The LogCategory \"" + cat +
                      "\" is not recognized by the FMU. Please check its availability in modelDescription.xml.\n",
                  fmi2Error, "logStatusError");
    }
}

// Developer Note: unfortunately it is not possible to retrieve the fmi2 type based on the var_ptr only.
// Both fmi2Integer and fmi2Boolean are actually alias of type int, thus impeding any possible splitting based on type.
// If we accept to have both fmi2Integer and fmi2Boolean considered as the same type we can drop the 'scalartype'
// argument but the risk is that a variable might end up being flagged as Integer while it's actually a Boolean.
// At least, in this way, we do not have any redundant code.
const FmuVariableExport& FmuComponentBase::AddFmuVariable(const FmuVariableExport::VarbindType& varbind,
                                                          std::string name,
                                                          FmuVariable::Type scalartype,
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
    std::set<FmuVariableExport>::iterator it = this->findByName(name);
    if (it != m_variables.end())
        throw std::runtime_error("Cannot add two FMU variables with the same name.");

    FmuVariableExport newvar(varbind, name, scalartype, causality, variability, initial);
    newvar.SetUnitName(unitname);
    newvar.SetValueReference(++m_valueReferenceCounter[scalartype]);
    newvar.SetDescription(description);

    // check that the attributes of the variable would allow a no-set variable
    ////const FmuMachineState tempFmuState = FmuMachineState::anySettableState;

    newvar.ExposeCurrentValueAsStart();
    // varns::visit([&newvar](auto var_ptr_expanded) { newvar.SetStartValIfRequired(var_ptr_expanded);},
    // var_ptr);

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
    rootNode->append_attribute(doc_ptr->allocate_attribute("fmiVersion", fmi2GetVersion()));
    rootNode->append_attribute(
        doc_ptr->allocate_attribute("modelName", m_modelIdentifier.c_str()));  // modelName can be anything else
    rootNode->append_attribute(doc_ptr->allocate_attribute("guid", m_fmuGUID.c_str()));
    rootNode->append_attribute(doc_ptr->allocate_attribute("generationTool", "rapidxml"));
    rootNode->append_attribute(doc_ptr->allocate_attribute("variableNamingConvention", "structured"));
    rootNode->append_attribute(doc_ptr->allocate_attribute("numberOfEventIndicators", "0"));
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
    const std::unordered_map<FmuVariable::Type, std::string> Type_strings = {{FmuVariable::Type::Real, "Real"},
                                                                             {FmuVariable::Type::Integer, "Integer"},
                                                                             {FmuVariable::Type::Boolean, "Boolean"},
                                                                             {FmuVariable::Type::Unknown, "Unknown"},
                                                                             {FmuVariable::Type::String, "String"}};

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
        {FmuVariable::CausalityType::parameter, "parameter"},
        {FmuVariable::CausalityType::calculatedParameter, "calculatedParameter"},
        {FmuVariable::CausalityType::input, "input"},
        {FmuVariable::CausalityType::output, "output"},
        {FmuVariable::CausalityType::local, "local"},
        {FmuVariable::CausalityType::independent, "independent"}};

    // Traverse all variables and cache their index (map by variable name).
    // Include in list of output variables as appropriate.
    std::unordered_map<std::string, int> variableIndices;  // indices of all variables
    std::vector<int> outputIndices;                        // indices of output variables
    int crt_index = 1;                                     // start index value

    for (auto it = m_variables.begin(); it != m_variables.end(); ++it) {
        variableIndices[it->GetName()] = crt_index;
        if (it->GetCausality() == FmuVariable::CausalityType::output)
            outputIndices.push_back(crt_index);
        crt_index++;
    }

    // Traverse all variables and create XML nodes
    for (auto it = m_variables.begin(); it != m_variables.end(); ++it) {
        // Create a comment node with the variable index
        stringbuf.push_back("Index: " + std::to_string(variableIndices[it->GetName()]));
        rapidxml::xml_node<>* idNode = doc_ptr->allocate_node(rapidxml::node_comment, "", stringbuf.back().c_str());
        modelVarsNode->append_node(idNode);

        // Create a variable node
        rapidxml::xml_node<>* varNode = doc_ptr->allocate_node(rapidxml::node_element, "ScalarVariable");
        varNode->append_attribute(doc_ptr->allocate_attribute("name", it->GetName().c_str()));

        stringbuf.push_back(std::to_string(it->GetValueReference()));
        varNode->append_attribute(doc_ptr->allocate_attribute("valueReference", stringbuf.back().c_str()));

        if (!it->GetDescription().empty())
            varNode->append_attribute(doc_ptr->allocate_attribute("description", it->GetDescription().c_str()));
        if (it->GetCausality() != FmuVariable::CausalityType::local)
            varNode->append_attribute(
                doc_ptr->allocate_attribute("causality", CausalityType_strings.at(it->GetCausality()).c_str()));
        if (it->GetVariability() != FmuVariable::VariabilityType::continuous)
            varNode->append_attribute(
                doc_ptr->allocate_attribute("variability", VariabilityType_strings.at(it->GetVariability()).c_str()));
        if (it->GetInitial() != FmuVariable::InitialType::none)
            varNode->append_attribute(
                doc_ptr->allocate_attribute("initial", InitialType_strings.at(it->GetInitial()).c_str()));

        rapidxml::xml_node<>* unitNode =
            doc_ptr->allocate_node(rapidxml::node_element, Type_strings.at(it->GetType()).c_str());
        if (it->GetType() == FmuVariable::Type::Real && !it->GetUnitName().empty())
            unitNode->append_attribute(doc_ptr->allocate_attribute("unit", it->GetUnitName().c_str()));
        if (it->HasStartVal()) {
            stringbuf.push_back(it->GetStartValAsString());
            unitNode->append_attribute(doc_ptr->allocate_attribute("start", stringbuf.back().c_str()));
        }
        auto state_name = isDerivative(it->GetName());
        if (!state_name.empty()) {
            stringbuf.push_back(std::to_string(variableIndices[state_name]));
            unitNode->append_attribute(doc_ptr->allocate_attribute("derivative", stringbuf.back().c_str()));
        }
        varNode->append_node(unitNode);

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
    if (!outputIndices.empty()) {
        rapidxml::xml_node<>* outputsNode = doc_ptr->allocate_node(rapidxml::node_element, "Outputs");
        for (int index : outputIndices) {
            rapidxml::xml_node<>* unknownNode = doc_ptr->allocate_node(rapidxml::node_element, "Unknown");
            stringbuf.push_back(std::to_string(index));
            unknownNode->append_attribute(doc_ptr->allocate_attribute("index", stringbuf.back().c_str()));
            outputsNode->append_node(unknownNode);
        }
        modelStructNode->append_node(outputsNode);
    }

    //     ...Derivatives
    if (!m_derivatives.empty()) {
        rapidxml::xml_node<>* derivativesNode = doc_ptr->allocate_node(rapidxml::node_element, "Derivatives");
        for (const auto& d : m_derivatives) {
            rapidxml::xml_node<>* unknownNode = doc_ptr->allocate_node(rapidxml::node_element, "Unknown");

            stringbuf.push_back(std::to_string(variableIndices[d.first]));
            unknownNode->append_attribute(doc_ptr->allocate_attribute("index", stringbuf.back().c_str()));

            std::string dependencies = "";
            for (const auto& dep : d.second.second) {
                dependencies += std::to_string(variableIndices[dep]) + " ";
            }
            stringbuf.push_back(dependencies);
            unknownNode->append_attribute(doc_ptr->allocate_attribute("dependencies", stringbuf.back().c_str()));

            //// TODO: dependeciesKind

            derivativesNode->append_node(unknownNode);
        }
        modelStructNode->append_node(derivativesNode);
    }

    //     ...InitialUnknowns
    if (!m_variableDependencies.empty()) {
        rapidxml::xml_node<>* initialUnknownsNode = doc_ptr->allocate_node(rapidxml::node_element, "InitialUnknowns");
        for (const auto& v : m_variableDependencies) {
            rapidxml::xml_node<>* unknownNode = doc_ptr->allocate_node(rapidxml::node_element, "Unknown");
            auto v_index = variableIndices[v.first];
            stringbuf.push_back(std::to_string(v_index));
            unknownNode->append_attribute(doc_ptr->allocate_attribute("index", stringbuf.back().c_str()));
            std::string dependencies = "";
            for (const auto& d : v.second) {
                auto d_index = variableIndices[d];
                dependencies += std::to_string(d_index) + " ";
            }
            stringbuf.push_back(dependencies);
            unknownNode->append_attribute(doc_ptr->allocate_attribute("dependencies", stringbuf.back().c_str()));
            initialUnknownsNode->append_node(unknownNode);
        }
        modelStructNode->append_node(initialUnknownsNode);
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

std::set<FmuVariableExport>::iterator FmuComponentBase::findByValrefType(fmi2ValueReference vr,
                                                                         FmuVariable::Type vartype) {
    auto predicate_samevalreftype = [vr, vartype](const FmuVariable& var) {
        return var.GetValueReference() == vr && var.GetType() == vartype;
    };
    return std::find_if(m_variables.begin(), m_variables.end(), predicate_samevalreftype);
}

std::set<FmuVariableExport>::iterator FmuComponentBase::findByName(const std::string& name) {
    auto predicate_samename = [name](const FmuVariable& var) { return !var.GetName().compare(name); };
    return std::find_if(m_variables.begin(), m_variables.end(), predicate_samename);
}

// -----------------------------------------------------------------------------

fmi2Status FmuComponentBase::EnterInitializationMode() {
    m_fmuMachineState = FmuMachineState::initializationMode;
    fmi2Status status = enterInitializationModeIMPL();
    return status;
}

fmi2Status FmuComponentBase::ExitInitializationMode() {
    fmi2Status status = exitInitializationModeIMPL();
    m_fmuMachineState = FmuMachineState::stepCompleted;  // TODO: introduce additional state when after
                                                         // initialization and before step?
    return status;
}

fmi2Status FmuComponentBase::DoStep(fmi2Real currentCommunicationPoint,
                                    fmi2Real communicationStepSize,
                                    fmi2Boolean noSetFMUStatePriorToCurrentPoint) {
    // invoke any pre step callbacks (e.g., to process input variables)
    executePreStepCallbacks();

    fmi2Status status = doStepIMPL(currentCommunicationPoint, communicationStepSize, noSetFMUStatePriorToCurrentPoint);

    // invoke any post step callbacks (e.g., to update auxiliary variables)
    executePostStepCallbacks();

    switch (status) {
        case fmi2OK:
            m_fmuMachineState = FmuMachineState::stepCompleted;
            break;
        case fmi2Warning:
            m_fmuMachineState = FmuMachineState::stepCompleted;
            break;
        case fmi2Discard:
            m_fmuMachineState = FmuMachineState::stepFailed;
            break;
        case fmi2Error:
            m_fmuMachineState = FmuMachineState::error;
            break;
        case fmi2Fatal:
            m_fmuMachineState = FmuMachineState::fatal;
            break;
        case fmi2Pending:
            m_fmuMachineState = FmuMachineState::stepInProgress;
            break;
        default:
            throw std::runtime_error("Developer error: unexpected status from doStepIMPL");
            break;
    }

    return status;
}

fmi2Status FmuComponentBase::NewDiscreteStates(fmi2EventInfo* fmi2eventInfo) {
    fmi2Status status = newDiscreteStatesIMPL(fmi2eventInfo);

    //// TODO - anything else here?

    return status;
}

fmi2Status FmuComponentBase::CompletedIntegratorStep(fmi2Boolean noSetFMUStatePriorToCurrentPoint,
                                                     fmi2Boolean* enterEventMode,
                                                     fmi2Boolean* terminateSimulation) {
    fmi2Status status =
        completedIntegratorStepIMPL(noSetFMUStatePriorToCurrentPoint, enterEventMode, terminateSimulation);

    //// TODO - anything else here?

    return status;
}

fmi2Status FmuComponentBase::SetTime(fmi2Real time) {
    m_time = time;

    fmi2Status status = setTimeIMPL(time);

    //// TODO - anything else here?

    return status;
}

fmi2Status FmuComponentBase::GetContinuousStates(fmi2Real x[], size_t nx) {
    fmi2Status status = getContinuousStatesIMPL(x, nx);

    //// TODO - interpret/process status?

    return status;
}

fmi2Status FmuComponentBase::SetContinuousStates(const fmi2Real x[], size_t nx) {
    fmi2Status status = setContinuousStatesIMPL(x, nx);

    //// TODO - interpret/process status?
    ////   Set FMU machine state (m_fmuMachineState)

    return status;
}

fmi2Status FmuComponentBase::GetDerivatives(fmi2Real derivatives[], size_t nx) {
    // invoke any pre step callbacks (e.g., to process input variables)
    executePreStepCallbacks();

    fmi2Status status = getDerivativesIMPL(derivatives, nx);

    // invoke any post step callbacks (e.g., to update auxiliary variables)
    executePostStepCallbacks();

    //// TODO - interpret/process status?
    ////   Set FMU machine state (m_fmuMachineState)

    return status;
}

void FmuComponentBase::addUnitDefinition(const UnitDefinition& unit_definition) {
    m_unitDefinitions[unit_definition.name] = unit_definition;
}

void FmuComponentBase::sendToLog(std::string msg, fmi2Status status, std::string msg_cat) {
    assert(m_logCategories_enabled.find(msg_cat) != m_logCategories_enabled.end() &&
           ("Developer warning: the category \"" + msg_cat + "\" is not recognized by the FMU").c_str());

    if (m_logCategories_enabled.find(msg_cat) == m_logCategories_enabled.end() || m_logCategories_enabled[msg_cat] ||
        (m_debug_logging_enabled && m_logCategories_debug.find(msg_cat) != m_logCategories_debug.end())) {
        m_callbackFunctions.logger(m_callbackFunctions.componentEnvironment, m_instanceName.c_str(), status,
                                   msg_cat.c_str(), msg.c_str());
    }
}

}  // namespace fmi2
}  // namespace fmu_forge

// =============================================================================
// FMU FUNCTIONS
// =============================================================================

using namespace fmu_forge::fmi2;

fmi2Component fmi2Instantiate(fmi2String instanceName,
                              fmi2Type fmuType,
                              fmi2String fmuGUID,
                              fmi2String fmuResourceLocation,
                              const fmi2CallbackFunctions* functions,
                              fmi2Boolean visible,
                              fmi2Boolean loggingOn) {
    FmuComponentBase* fmu_ptr =
        fmi2InstantiateIMPL(instanceName, fmuType, fmuGUID, fmuResourceLocation, functions, visible, loggingOn);
    return reinterpret_cast<void*>(fmu_ptr);
}

const char* fmi2GetTypesPlatform(void) {
    return fmi2TypesPlatform;
}

const char* fmi2GetVersion(void) {
    return fmi2Version;
}

fmi2Status fmi2SetDebugLogging(fmi2Component c,
                               fmi2Boolean loggingOn,
                               size_t nCategories,
                               const fmi2String categories[]) {
    FmuComponentBase* fmu_ptr = reinterpret_cast<FmuComponentBase*>(c);
    for (auto cs = 0; cs < nCategories; ++cs) {
        fmu_ptr->SetDebugLogging(categories[cs], loggingOn == fmi2True ? true : false);
    }

    return fmi2Status::fmi2OK;
}

void fmi2FreeInstance(fmi2Component c) {
    delete reinterpret_cast<FmuComponentBase*>(c);
}

fmi2Status fmi2SetupExperiment(fmi2Component c,
                               fmi2Boolean toleranceDefined,
                               fmi2Real tolerance,
                               fmi2Real startTime,
                               fmi2Boolean stopTimeDefined,
                               fmi2Real stopTime) {
    reinterpret_cast<FmuComponentBase*>(c)->SetDefaultExperiment(toleranceDefined, tolerance, startTime,
                                                                 stopTimeDefined, stopTime);
    return fmi2Status::fmi2OK;
}

fmi2Status fmi2EnterInitializationMode(fmi2Component c) {
    return reinterpret_cast<FmuComponentBase*>(c)->EnterInitializationMode();
}

fmi2Status fmi2ExitInitializationMode(fmi2Component c) {
    return reinterpret_cast<FmuComponentBase*>(c)->ExitInitializationMode();
}
fmi2Status fmi2Terminate(fmi2Component c) {
    return fmi2Status::fmi2OK;
}
fmi2Status fmi2Reset(fmi2Component c) {
    return fmi2Status::fmi2OK;
}

fmi2Status fmi2GetReal(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2Real value[]) {
    return reinterpret_cast<FmuComponentBase*>(c)->fmi2GetVariable(vr, nvr, value, FmuVariable::Type::Real);
}

fmi2Status fmi2GetInteger(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2Integer value[]) {
    return reinterpret_cast<FmuComponentBase*>(c)->fmi2GetVariable(vr, nvr, value, FmuVariable::Type::Integer);
}

fmi2Status fmi2GetBoolean(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2Boolean value[]) {
    return reinterpret_cast<FmuComponentBase*>(c)->fmi2GetVariable(vr, nvr, value, FmuVariable::Type::Boolean);
}

fmi2Status fmi2GetString(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, fmi2String value[]) {
    return reinterpret_cast<FmuComponentBase*>(c)->fmi2GetVariable(vr, nvr, value, FmuVariable::Type::String);
}

fmi2Status fmi2SetReal(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Real value[]) {
    return reinterpret_cast<FmuComponentBase*>(c)->fmi2SetVariable(vr, nvr, value, FmuVariable::Type::Real);
}

fmi2Status fmi2SetInteger(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Integer value[]) {
    return reinterpret_cast<FmuComponentBase*>(c)->fmi2SetVariable(vr, nvr, value, FmuVariable::Type::Integer);
}

fmi2Status fmi2SetBoolean(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2Boolean value[]) {
    return reinterpret_cast<FmuComponentBase*>(c)->fmi2SetVariable(vr, nvr, value, FmuVariable::Type::Boolean);
}

fmi2Status fmi2SetString(fmi2Component c, const fmi2ValueReference vr[], size_t nvr, const fmi2String value[]) {
    return reinterpret_cast<FmuComponentBase*>(c)->fmi2SetVariable(vr, nvr, value, FmuVariable::Type::String);
}

fmi2Status fmi2GetFMUstate(fmi2Component c, fmi2FMUstate* FMUstate) {
    return fmi2Status::fmi2OK;
}
fmi2Status fmi2SetFMUstate(fmi2Component c, fmi2FMUstate FMUstate) {
    return fmi2Status::fmi2OK;
}
fmi2Status fmi2FreeFMUstate(fmi2Component c, fmi2FMUstate* FMUstate) {
    return fmi2Status::fmi2OK;
}
fmi2Status fmi2SerializedFMUstateSize(fmi2Component c, fmi2FMUstate FMUstate, size_t* size) {
    return fmi2Status::fmi2OK;
}
fmi2Status fmi2SerializeFMUstate(fmi2Component c, fmi2FMUstate FMUstate, fmi2Byte serializedState[], size_t size) {
    return fmi2Status::fmi2OK;
}
fmi2Status fmi2DeSerializeFMUstate(fmi2Component c,
                                   const fmi2Byte serializedState[],
                                   size_t size,
                                   fmi2FMUstate* FMUstate) {
    return fmi2Status::fmi2OK;
}
fmi2Status fmi2GetDirectionalDerivative(fmi2Component c,
                                        const fmi2ValueReference vUnknown_ref[],
                                        size_t nUnknown,
                                        const fmi2ValueReference vKnown_ref[],
                                        size_t nKnown,
                                        const fmi2Real dvKnown[],
                                        fmi2Real dvUnknown[]) {
    return fmi2Status::fmi2OK;
}

// Model Exchange

fmi2Status fmi2EnterEventMode(fmi2Component c) {
    return fmi2Status::fmi2OK;
}

fmi2Status fmi2NewDiscreteStates(fmi2Component c, fmi2EventInfo* fmi2eventInfo) {
    return reinterpret_cast<FmuComponentBase*>(c)->NewDiscreteStates(fmi2eventInfo);
}

fmi2Status fmi2EnterContinuousTimeMode(fmi2Component c) {
    return fmi2Status::fmi2OK;
}

fmi2Status fmi2CompletedIntegratorStep(fmi2Component c,
                                       fmi2Boolean noSetFMUStatePriorToCurrentPoint,
                                       fmi2Boolean* enterEventMode,
                                       fmi2Boolean* terminateSimulation) {
    return reinterpret_cast<FmuComponentBase*>(c)->CompletedIntegratorStep(noSetFMUStatePriorToCurrentPoint,
                                                                           enterEventMode, terminateSimulation);
}

fmi2Status fmi2SetTime(fmi2Component c, fmi2Real time) {
    return reinterpret_cast<FmuComponentBase*>(c)->SetTime(time);
}

fmi2Status fmi2SetContinuousStates(fmi2Component c, const fmi2Real x[], size_t nx) {
    return reinterpret_cast<FmuComponentBase*>(c)->SetContinuousStates(x, nx);
}

fmi2Status fmi2GetDerivatives(fmi2Component c, fmi2Real derivatives[], size_t nx) {
    return reinterpret_cast<FmuComponentBase*>(c)->GetDerivatives(derivatives, nx);
}

fmi2Status fmi2GetEventIndicators(fmi2Component c, fmi2Real eventIndicators[], size_t ni) {
    return fmi2Status::fmi2OK;
}

fmi2Status fmi2GetContinuousStates(fmi2Component c, fmi2Real x[], size_t nx) {
    return reinterpret_cast<FmuComponentBase*>(c)->GetContinuousStates(x, nx);
}

fmi2Status fmi2GetNominalsOfContinuousStates(fmi2Component c, fmi2Real x_nominal[], size_t nx) {
    return fmi2Status::fmi2OK;
}

// Co-Simulation

fmi2Status fmi2SetRealInputDerivatives(fmi2Component c,
                                       const fmi2ValueReference vr[],
                                       size_t nvr,
                                       const fmi2Integer order[],
                                       const fmi2Real value[]) {
    return fmi2Status::fmi2OK;
}

fmi2Status fmi2GetRealOutputDerivatives(fmi2Component c,
                                        const fmi2ValueReference vr[],
                                        size_t nvr,
                                        const fmi2Integer order[],
                                        fmi2Real value[]) {
    return fmi2Status::fmi2OK;
}

fmi2Status fmi2DoStep(fmi2Component c,
                      fmi2Real currentCommunicationPoint,
                      fmi2Real communicationStepSize,
                      fmi2Boolean noSetFMUStatePriorToCurrentPoint) {
    return reinterpret_cast<FmuComponentBase*>(c)->DoStep(currentCommunicationPoint, communicationStepSize,
                                                          noSetFMUStatePriorToCurrentPoint);
}

fmi2Status fmi2CancelStep(fmi2Component c) {
    return fmi2Status::fmi2OK;
}

fmi2Status fmi2GetStatus(fmi2Component c, const fmi2StatusKind s, fmi2Status* value) {
    return fmi2Status::fmi2OK;
}

fmi2Status fmi2GetRealStatus(fmi2Component c, const fmi2StatusKind s, fmi2Real* value) {
    return fmi2Status::fmi2OK;
}

fmi2Status fmi2GetIntegerStatus(fmi2Component c, const fmi2StatusKind s, fmi2Integer* value) {
    return fmi2Status::fmi2OK;
}

fmi2Status fmi2GetBooleanStatus(fmi2Component c, const fmi2StatusKind s, fmi2Boolean* value) {
    return fmi2Status::fmi2OK;
}

fmi2Status fmi2GetStringStatus(fmi2Component c, const fmi2StatusKind s, fmi2String* value) {
    return fmi2Status::fmi2OK;
}
