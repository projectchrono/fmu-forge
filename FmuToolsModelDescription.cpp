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
// Program for generating the model description XML for an FMU
// =============================================================================

#include <string>
#include <iostream>

#include "FmuToolsRuntimeLinking.h"

using namespace fmu_forge;

int main(int argc, char* argv[]) {
    std::string dynlib_fullpath;
    std::string dynlib_dir;
    std::string output_path;

    std::string suffix = ".fmu";

    if (argc == 3 || argc == 4) {
        dynlib_dir = argv[1];
        std::string dynlib_name = argv[2];

        // check if a proper fmu has been provided
        if (dynlib_name.substr(dynlib_name.length() - suffix.length()) == suffix) {
            std::cerr << "ERROR: Please unzip the FMU first and point to the binaries directory." << std::endl;
            return 3;
        }

        // make sure that the path is using forward slashes
        for (char& c : dynlib_dir) {
            if (c == '\\') {
                c = '/';
            }
        }

        // Check if the last character of dynlib_dir is a slash
        if (!dynlib_dir.empty() && !(dynlib_dir.back() == '/')) {
            dynlib_dir += "/";
        }

        dynlib_fullpath = dynlib_dir + dynlib_name;

        if (argc == 4) {
            output_path = argv[3];
        } else
            output_path = dynlib_fullpath + "/../../";

    } else {
        std::cout << "Usage: " << argv[0]
                  << " <FMU binaries folder location> <FMU library name> <modelDescription output dir (optional)>"
                  << std::endl;
        std::cout << "Return 1: Cannot link to library or library not found." << std::endl;
        std::cout << "Return 2: Cannot call modelDescription generation function." << std::endl;
        std::cout << "Return 3: Please unzip the fmu first and point to the binaries directory." << std::endl;
        std::cout << "Return 4: this call; wrong set of arguments." << std::endl;
        std::cerr << "ERROR: executable called with wrong set of arguments." << std::endl;
        return 4;
    }

    DYNLIB_HANDLE dynlib_handle = RuntimeLinkLibrary(dynlib_dir, dynlib_fullpath);
    if (!dynlib_handle) {
        std::cerr << "ERROR: Cannot link to library: " << dynlib_fullpath << std::endl;
        return 1;
    }

    typedef bool (*createModelDescriptionPtrType)(const std::string& path, std::string& err_msg);
    createModelDescriptionPtrType createModelDescriptionPtr =
        (createModelDescriptionPtrType)get_function_ptr(dynlib_handle, "createModelDescription");

    std::string err_msg;
    if (!createModelDescriptionPtr(output_path, err_msg)) {
        std::cerr << "ERROR: " << err_msg << std::endl;
        return 2;
    }

    return 0;
}
