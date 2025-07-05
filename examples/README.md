<p align="center">
<img src="https://github.com/projectchrono/fmu-forge/blob/main/fmu-forge.png" width="200">
</p>

<br><br>

# fmu-forge examples

To illustrate the export and import capabilities of fmu-forge, we provide the following FMU examples, all of which use the model of a planar rigid pendulum. The equations represent a two-state second roder ODE (converted to a 4-state first order ODE); the co-simulation FMUs use an RK4 integration scheme.

<details>
<summary> FMI-2.0 </summary>
  
  - CS FMU: [myFmuCosimulation_fmi2](https://github.com/projectchrono/fmu-forge/tree/main/examples/fmi2/cosimulation)
  - ME FMU: [myFmuModelExchange_fmi2](https://github.com/projectchrono/fmu-forge/tree/main/examples/fmi2/model_exchange)
    
</details>

<details>
<summary> FMI-3.0 </summary>
  
- CS FMU: [myFmuCosimulation_fmi3](https://github.com/projectchrono/fmu-forge/tree/main/examples/fmi3/cosimulation)
- ME FMU: [myFmuModelExchange_fmi3](https://github.com/projectchrono/fmu-forge/tree/main/examples/fmi3/model_exchange)
  
</details>

## FMU export compatibility information

During export, any FMU generated with fmu-forge can be automatically tested with the `fmusim` validation tool (see [Reference-FMUs](https://github.com/modelica/Reference-FMUs)).<br>
This option, available for both FMI-2.0 and FMI-3.0, can be enabled by setting the `FMU_TESTING` CMake variable to `ON`. See for example [CMakeLists.txt](https://github.com/projectchrono/fmu-forge/blob/01cda9654dc48adbd310267de1915a11f369e250/examples/fmi2/cosimulation/CMakeLists.txt#L10) for the FMI 2.0 co-simulation examples.<br>
All fmu-forge example FMUs provided here have been validated with `fmusim`. 

FMUs exported with fmu-forge have been imported and co-simulated using [Simulink](https://mathworks.com/products/simulink.html).<br>
See for example the Simulink-based co-simulation of a hydraulically activated crane using CS FMUs encapsulating [Chrono](https://projectchrono.org/) models at https://github.com/projectchrono/chrono/tree/main/data/fmi. 

## FMU import compatibility information

The example driver programs distributed with fmu-forge illustrate FMU import:
- FMI-2.0 CS: [demo_Cosimulation_fmi2](https://github.com/projectchrono/fmu-forge/blob/main/examples/fmi2/cosimulation/demo_Cosimulation_fmi2.cpp)
- FMI-2.0 ME: [demo_ModelExchange_fmi2](https://github.com/projectchrono/fmu-forge/blob/main/examples/fmi2/model_exchange/demo_ModelExchange_fmi2.cpp)
- FMI-3.0 CS: [demo_Cosimulation_fmi3](https://github.com/projectchrono/fmu-forge/blob/main/examples/fmi3/cosimulation/demo_Cosimulation_fmi3.cpp)
- FMI-3.0 ME: [demo_ModelExchange_fmi3](https://github.com/projectchrono/fmu-forge/blob/main/examples/fmi3/model_exchange/demo_ModelExchange_fmi3.cpp)

See also the FMI examples in the [Chrono](https://github.com/projectchrono/chrono/tree/main/src/demos/fmi) repository, all of which use fmu-forge.
