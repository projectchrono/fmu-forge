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
// Example FMU instantiation for co-simulation (FMI 3.0 standard)
// Illustrates the FMU importing capabilities in fmu-forge (FmuToolsImport.h)
// =============================================================================

#include <iostream>
#include <fstream>
#include <cstddef>

#include "fmi3/FmuToolsImport.h"

using namespace fmu_forge::fmi3;

std::string unzipped_fmu_folder = FMU_UNPACK_DIRECTORY;

int main(int argc, char* argv[]) {
    FmuUnit my_fmu;
    my_fmu.SetVerbose(true);

    try {
        my_fmu.Load(FmuType::COSIMULATION, FMU_FILENAME, FMU_UNPACK_DIRECTORY);
        ////my_fmu.Load(FmuType::COSIMULATION, FMU_FILENAME);                 // unpack in  /tmp
        ////my_fmu.LoadUnzipped(FmuType::COSIMULATION, unzipped_fmu_folder);  // already unpacked
    } catch (std::exception& my_exception) {
        std::cout << "ERROR loading FMU: " << my_exception.what() << std::endl;
        return 1;
    }

    try {
        my_fmu.Instantiate("FmuComponent");  // use default resources dir
        ////my_fmu.Instantiate("FmuComponent", my_fmu.GetUnzippedFolder() + "resources");  // specify resources dir
    } catch (std::exception& my_exception) {
        std::cout << "ERROR instantiating FMU: " << my_exception.what() << std::endl;
        return 1;
    }

    std::vector<std::string> categoriesVector = {"logAll"};
    my_fmu.SetDebugLogging(fmi3True, categoriesVector);

    // alternatively, with native interface:
    // std::vector<const char*> categoriesArray;
    // for (const auto& category : categoriesVector) {
    //    categoriesArray.push_back(category.c_str());
    //}

    // my_fmu._fmi3SetDebugLogging(my_fmu.component, fmi3True, categoriesVector.size(), categoriesArray.data());

    // Initialize FMU
    my_fmu.EnterInitializationMode(fmi3False,  // no tolerance defined
                                   0.0,        // tolerance (dummy)
                                   0.0,        // start time
                                   fmi3False,  // do not use stop time
                                   1.0         // stop time (dummy)
    );
    {
        fmi3ValueReference valref = 3;
        fmi3Float64 m_in = 1.5;
        my_fmu.SetVariable(valref, m_in);

        fmi3Float64 m_out;
        my_fmu.GetVariable(valref, m_out);
        std::cout << "m_out: " << m_out << std::endl;

        // String array as std::vector<std::string>
        std::vector<std::string> val_string_array_input = {"sayonara", "zdravo"};
        my_fmu.SetVariable("stringarrayinput", val_string_array_input);
    }
    my_fmu.ExitInitializationMode();

    // Prepare output file
    std::ofstream ofile("results.out");

    // Co-simulation loop
    double time = 0;
    double time_end = 10;
    double dt = 0.01;

    while (time < time_end) {
        double x, theta;
        my_fmu.GetVariable("x", x);
        my_fmu.GetVariable("theta", theta);
        ofile << time << " " << x << " " << theta << std::endl;

        // Set next communication time
        time += dt;

        // Advance FMU state to new time
        my_fmu.DoStep(time, dt, fmi3True);
    }

    // Close output file
    ofile.close();

    // Getting FMU scalar variables through valueRef
    fmi3Float64 val_real;
    fmi3ValueReference valrefs_float64[11] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15};
    for (auto valref : valrefs_float64) {
        my_fmu.GetVariable(valref, val_real);
        std::cout << "Float64: valref: " << valref << " | val: " << val_real << std::endl;
    }

    // Getting FMU array variables through valueRef
    std::vector<fmi3Float64> val_real_array;
    fmi3ValueReference valref_float64_array = 10;

    val_real_array.resize(my_fmu.GetVariableSize(valref_float64_array));
    my_fmu.GetVariable(valref_float64_array, *val_real_array.data());

    auto dims = my_fmu.GetVariableDimensions(valref_float64_array);
    std::cout << "Float64 (array): valref: " << valref_float64_array << " | val: \n";
    for (auto row_sel = 0; row_sel < dims[0]; row_sel++) {
        for (auto col_sel = 0; col_sel < dims[1]; col_sel++) {
            std::cout << val_real_array[row_sel * dims[1] + col_sel] << " ";
        }
        std::cout << std::endl;
    }

    // Getting FMU variables through custom fmu-tools functions, directly using the variable name
    auto status = my_fmu.GetVariable("m", val_real);

    // Boolean data
    fmi3Boolean val_bool;
    fmi3ValueReference valrefs_bool[1] = {16};
    for (auto valref : valrefs_bool) {
        my_fmu.GetVariable(valref, val_bool);
        std::cout << "Boolean: valref: " << valref << " | val: " << val_bool << std::endl;
    }

    // String scalar
    std::vector<std::string> val_string;
    fmi3ValueReference valrefs_string[1] = {11};
    my_fmu.GetVariable(valrefs_string[0], val_string);
    std::cout << "String scalar: valref: " << valrefs_string[0] << " | val: ";
    for (auto el = 0; el < val_string.size(); ++el) {
        std::cout << val_string[el] << std::endl;
    }
    std::cout << std::endl;

    std::vector<std::string> val_string_array_input_asoutput;
    fmi3ValueReference valrefs_string_array[1] = {12};
    my_fmu.GetVariable(valrefs_string_array[0], val_string_array_input_asoutput);
    std::cout << "String array: valref: " << valrefs_string_array[0] << " | val: " << std::endl;
    for (auto el = 0; el < val_string_array_input_asoutput.size(); ++el) {
        std::cout << val_string_array_input_asoutput[el] << std::endl;
    }
    std::cout << std::endl;

    // Binary Data with direct access
    const size_t nValueReferences = 2;
    fmi3ValueReference valrefs_bin[nValueReferences] = {13, 14};

    // FMI native interface
    {
        const size_t nValues = my_fmu.GetVariableSize(valrefs_bin[0]) + my_fmu.GetVariableSize(valrefs_bin[1]);
        std::vector<size_t> valueSizes(nValues);
        std::vector<fmi3Binary> values(nValues);
        my_fmu._fmi3GetBinary(my_fmu.instance, valrefs_bin, nValueReferences, valueSizes.data(), values.data(),
                              nValues);
        std::vector<std::vector<fmi3Byte>> binary_values;
        binary_values.resize(nValues);
        std::cout << std::dec << "Binary (multiple valueRefs): valref: " << valrefs_bin[0] << "+" << valrefs_bin[1]
                  << " | val: " << std::endl;

        for (size_t i = 0; i < nValues; i++) {
            binary_values[i].resize(valueSizes[i]);
            for (size_t j = 0; j < valueSizes[i]; j++) {
                binary_values[i][j] = values[i][j];
                std::cout << std::hex << (int)binary_values[i][j];
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;
    }

    // fmi3Binary scalar as std::vector<fmi3Byte>
    {
        std::vector<fmi3Byte> fmi3Binary_scalar_vectfmi3Byte;
        my_fmu.GetVariable(valrefs_bin[0], fmi3Binary_scalar_vectfmi3Byte);
        std::cout << std::dec << "Binary scalar as std::vector<fmi3Byte>: valref: " << valrefs_bin[0] << " | val: ";
        for (auto byte : fmi3Binary_scalar_vectfmi3Byte) {
            std::cout << std::hex << (int)byte;
        }
        std::cout << std::endl;
    }

    // fmi3Binary scalar as fmi3Binary
    {
        fmi3Binary fmi3Binary_scalar_fmi3Binary;
        size_t valueSize;
        my_fmu.GetVariable(valrefs_bin[0], fmi3Binary_scalar_fmi3Binary, valueSize);
        std::cout << std::dec << "Binary scalar as fmi3Binary: valref: " << valrefs_bin[0] << " | val: ";
        for (auto el = 0; el < valueSize; ++el) {
            std::cout << std::hex << (int)fmi3Binary_scalar_fmi3Binary[el];
        }
        std::cout << std::endl;
    }

    // fmi3Binary array as std::vector<std::vector<fmi3Byte>>
    {
        std::vector<std::vector<fmi3Byte>> vector_vector_fmi3Byte;
        my_fmu.GetVariable(valrefs_bin[1], vector_vector_fmi3Byte);
        std::cout << std::dec << "Binary array as std::vector<std::vector<fmi3Byte>>: valref: " << valrefs_bin[1]
                  << " | val: " << std::endl;
        for (auto el = 0; el < vector_vector_fmi3Byte.size(); ++el) {
            for (auto byte : vector_vector_fmi3Byte[el]) {
                std::cout << std::hex << (int)byte;
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;
    }

    // fmi3Binary array as std::vector<fmi3Binary>
    {
        std::vector<fmi3Binary> vector_fmi3Binary;
        std::vector<size_t> valueSizes;
        my_fmu.GetVariable(valrefs_bin[1], vector_fmi3Binary, valueSizes);
        std::cout << std::dec << "Binary array as std::vector<fmi3Binary>: valref: " << valrefs_bin[1]
                  << " | val: " << std::endl;
        for (auto el = 0; el < vector_fmi3Binary.size(); ++el) {
            for (auto s = 0; s < valueSizes[el]; ++s) {
                std::cout << std::hex << (int)vector_fmi3Binary[el][s];
            }
            std::cout << std::endl;
        }
        std::cout << std::endl;
    }

    return 0;
}
