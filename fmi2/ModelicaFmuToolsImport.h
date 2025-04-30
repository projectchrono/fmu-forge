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
// Modelica wrappers to fmu-forge FMU import classes for FMI standard 2.0
// =============================================================================

#pragma once

#include "fmi2/FmuToolsImport.h"

namespace fmu_forge {
namespace fmi2 {

/// Class holding a set of scalar variables for the coordinate system of a visualizer in the FMU.
struct FmuModelicaBody {
    unsigned int pos_references[3];
    unsigned int rot_references[9];

    std::string name;
};

// =============================================================================

/// Visual shape for the FMU.
/// The visualizer could be a cylinder, a sphere, a mesh, etc.
struct FmuModelicaVisualShape {
    FmuVariableTreeNode* visualizer_node = nullptr;

    unsigned int pos_references[3] = {0, 0, 0};
    unsigned int rot_references[9] = {0, 0, 0};
    unsigned int pos_shape_references[3] = {0, 0, 0};
    unsigned int shapetype_reference = 0;
    unsigned int l_references[3] = {0, 0, 0};
    unsigned int w_references[3] = {0, 0, 0};
    unsigned int color_references[3] = {0, 0, 0};
    unsigned int width_reference = 0;
    unsigned int height_reference = 0;
    unsigned int length_reference = 0;

    std::string type;
    std::string filename;
};

// =============================================================================

/// Extension of FmuUnit class for Modelica FMUs.
class FmuModelicaUnit : public FmuUnit {
  public:
    FmuModelicaUnit() : FmuUnit() {}

    virtual void LoadUnzipped(Type type, const std::string& directory) override;

    void BuildBodyList(FmuVariableTreeNode* mynode) {}
    void BuildVisualizersList(FmuVariableTreeNode* mynode);

    std::vector<FmuModelicaVisualShape> visualizers;
    std::vector<FmuModelicaBody> bodies;
};

// -----------------------------------------------------------------------------

void FmuModelicaUnit::LoadUnzipped(Type type, const std::string& directory) {
    FmuUnit::LoadUnzipped(type, directory);

    BuildBodyList(&tree_variables);
    BuildVisualizersList(&tree_variables);
}

void FmuModelicaUnit::BuildBodyList(FmuVariableTreeNode* mynode) {
    //// TODO
}

void FmuModelicaUnit::BuildVisualizersList(FmuVariableTreeNode* mynode) {
    auto found_shtype = mynode->children.find("shapeType");
    auto found_R = mynode->children.find("R");
    auto found_r1 = mynode->children.find("r[1]");
    auto found_r2 = mynode->children.find("r[2]");
    auto found_r3 = mynode->children.find("r[3]");
    auto found_rshape1 = mynode->children.find("r_shape[1]");
    auto found_rshape2 = mynode->children.find("r_shape[2]");
    auto found_rshape3 = mynode->children.find("r_shape[3]");
    auto found_l1 = mynode->children.find("lengthDirection[1]");
    auto found_l2 = mynode->children.find("lengthDirection[2]");
    auto found_l3 = mynode->children.find("lengthDirection[3]");
    auto found_w1 = mynode->children.find("widthDirection[1]");
    auto found_w2 = mynode->children.find("widthDirection[2]");
    auto found_w3 = mynode->children.find("widthDirection[3]");
    auto found_color1 = mynode->children.find("color[1]");
    auto found_color2 = mynode->children.find("color[2]");
    auto found_color3 = mynode->children.find("color[3]");
    auto found_length = mynode->children.find("length");
    auto found_width = mynode->children.find("width");
    auto found_height = mynode->children.find("height");
    if (found_shtype != mynode->children.end() && found_R != mynode->children.end() &&
        found_r1 != mynode->children.end() && found_r2 != mynode->children.end() &&
        found_r3 != mynode->children.end() && found_l1 != mynode->children.end() &&
        found_l2 != mynode->children.end() && found_l3 != mynode->children.end() &&
        found_w1 != mynode->children.end() && found_w2 != mynode->children.end() &&
        found_w3 != mynode->children.end()) {
        auto found_T11 = found_R->second.children.find("T[1,1]");
        auto found_T12 = found_R->second.children.find("T[1,2]");
        auto found_T13 = found_R->second.children.find("T[1,3]");
        auto found_T21 = found_R->second.children.find("T[2,1]");
        auto found_T22 = found_R->second.children.find("T[2,2]");
        auto found_T23 = found_R->second.children.find("T[2,3]");
        auto found_T31 = found_R->second.children.find("T[3,1]");
        auto found_T32 = found_R->second.children.find("T[3,2]");
        auto found_T33 = found_R->second.children.find("T[3,3]");
        if (found_T11 != found_R->second.children.end() && found_T12 != found_R->second.children.end() &&
            found_T13 != found_R->second.children.end() && found_T21 != found_R->second.children.end() &&
            found_T22 != found_R->second.children.end() && found_T23 != found_R->second.children.end() &&
            found_T31 != found_R->second.children.end() && found_T32 != found_R->second.children.end() &&
            found_T33 != found_R->second.children.end()) {
            FmuModelicaVisualShape my_v;
            my_v.pos_references[0] = found_r1->second.leaf->GetValueReference();
            my_v.pos_references[1] = found_r2->second.leaf->GetValueReference();
            my_v.pos_references[2] = found_r3->second.leaf->GetValueReference();
            my_v.pos_shape_references[0] = found_rshape1->second.leaf->GetValueReference();
            my_v.pos_shape_references[1] = found_rshape2->second.leaf->GetValueReference();
            my_v.pos_shape_references[2] = found_rshape3->second.leaf->GetValueReference();
            my_v.rot_references[0] = found_T11->second.leaf->GetValueReference();
            my_v.rot_references[1] = found_T12->second.leaf->GetValueReference();
            my_v.rot_references[2] = found_T13->second.leaf->GetValueReference();
            my_v.rot_references[3] = found_T21->second.leaf->GetValueReference();
            my_v.rot_references[4] = found_T22->second.leaf->GetValueReference();
            my_v.rot_references[5] = found_T23->second.leaf->GetValueReference();
            my_v.rot_references[6] = found_T31->second.leaf->GetValueReference();
            my_v.rot_references[7] = found_T32->second.leaf->GetValueReference();
            my_v.rot_references[8] = found_T33->second.leaf->GetValueReference();
            my_v.shapetype_reference = found_shtype->second.leaf->GetValueReference();
            my_v.l_references[0] = found_l1->second.leaf->GetValueReference();
            my_v.l_references[1] = found_l2->second.leaf->GetValueReference();
            my_v.l_references[2] = found_l3->second.leaf->GetValueReference();
            my_v.w_references[0] = found_w1->second.leaf->GetValueReference();
            my_v.w_references[1] = found_w2->second.leaf->GetValueReference();
            my_v.w_references[2] = found_w3->second.leaf->GetValueReference();
            my_v.color_references[0] = found_color1->second.leaf->GetValueReference();
            my_v.color_references[1] = found_color2->second.leaf->GetValueReference();
            my_v.color_references[2] = found_color3->second.leaf->GetValueReference();
            my_v.width_reference = found_width->second.leaf->GetValueReference();
            my_v.length_reference = found_length->second.leaf->GetValueReference();
            my_v.height_reference = found_height->second.leaf->GetValueReference();
            my_v.visualizer_node = mynode;

            visualizers.push_back(my_v);
        }
    }
    // recursion
    for (auto& in : mynode->children) {
        BuildVisualizersList(&in.second);
    }
}

}  // namespace fmi2
}  // namespace fmu_forge
