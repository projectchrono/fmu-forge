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
// Utility functions for FMU import, independent of FMI version
// =============================================================================

#ifndef FMUTOOLS_IMPORT_COMMON_H
#define FMUTOOLS_IMPORT_COMMON_H

// ATTENTION: order of included headers is IMPORTANT!
//            rapidxml.hpp must be included first here.

#include "rapidxml/rapidxml.hpp"
#include "miniz-cpp/zip_file.hpp"
#include "filesystem.hpp"

/// Main namespace for the fmu-forge package
namespace fmu_forge {

/// @addtogroup fmu-forge
/// @{

/// Enumeration of supported FMI standard versions.
enum class FmuVersion { FMI2, FMI3 };

/// Extract the given FMU in the specified directory.
void UnzipFmu(const std::string& fmufilename, const std::string& unzipdir) {
    std::error_code ec;
    fs::remove_all(unzipdir, ec);
    fs::create_directories(unzipdir);
    miniz_cpp::zip_file zipfile(fmufilename);
    zipfile.extractall(unzipdir);
}

/// Get the FMI version (2.0 or 3.0) from the model description file of the specified FMU.
FmuVersion GetFmuVersion(const std::string& fmufilename) {
    // Unzip FMU archive in temporary directory
    std::string unzipdir = fs::temp_directory_path().generic_string() + std::string("/fmu_ver_temp");
    UnzipFmu(fmufilename, unzipdir);

    // Open model description XML
    std::string xml_filename = unzipdir + "/modelDescription.xml";
    std::ifstream file(xml_filename);
    if (!file.good())
        throw std::runtime_error("Cannot find file: " + xml_filename + "\n");

    rapidxml::xml_document<>* doc_ptr = new rapidxml::xml_document<>();
    std::vector<char> buffer((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    buffer.push_back('\0');
    doc_ptr->parse<0>(&buffer[0]);

    auto root_node = doc_ptr->first_node("fmiModelDescription");
    if (!root_node)
        throw std::runtime_error("Not a valid FMU. Missing <fmiModelDescription> node in XML. \n");

    auto attr = root_node->first_attribute("fmiVersion");
    if (!attr)
        throw std::runtime_error("Missing fmiVersion attribute in the XML node <fmiModelDescription>. \n");

    std::string fmi_version = attr->value();

    if (fmi_version.compare("2.0") == 0)
        return FmuVersion::FMI2;
    else if (fmi_version.compare("3.0") == 0)
        return FmuVersion::FMI3;
    else
        throw std::runtime_error("Unsupported FMI version. \n");
}

/// @} fmu-forge

}  // namespace fmu_forge

#endif
