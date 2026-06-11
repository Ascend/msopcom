# MindStudio Ops Common Architecture Design

## Directory Design

The overall directory design approach for this repository is as follows:

```text
MindStudio-Ops-Common
|-- .gitsubmodules          # Manage dependency submodule files
|-- build.py                # One-click build script entry
|-- csrc                    # Provides hooking capabilities for CPU-side interfaces
  |-- include               # Original header files of the hooked objects
  |-- core                  # Core functions: communication (control, data - data stream/file, supporting parent-child-grandchild processes); interface registration; interface binding
  |-- runtime               # Provides hooking functions for runtime interfaces and aliases for original interfaces
    |-- RuntimeOrigin.h     # Provides aliases for original interfaces
    |-- injectionOfxxx      # Provides hooking functions for xx interfaces under runtime
  |-- ...
  |-- bind                  # Binds interfaces to specific tools and supports inter-process control and data transfer capabilities
    |-- BindCoverage.cpp    # Binds the decorated function to a specific runtime interface, one per tool
  |-- kernel_injection      # Provides hooking capabilities for the kernel side, with minimal content
|-- test                    # Test case maintenance
|-- thirdparty              # Third-party dependency storage directory
|-- docs                    # Project documentation introduction
└── README.md               # Overall repository code description
```

NOTE:

- csrc provides the following functions:

  + (For the hooking target) Provides management of native interfaces
  + (Unified common plugin) Provides management of injected (decorated) functions
  + (For specific tools) Provides unified management of hooked interfaces and supports communication control for injected functions
  + Specific tools directly reference this repository to complete binary compilation, providing submodule functionality to support separate compilation of injections for specific tools.
- Here, kernel_injection only targets dynamic instrumentation capabilities (static instrumentation capabilities are carried by each component itself)
  + Currently supports bisheng-tune, and will need to support msbit capabilities in the future
  + It needs to be enabled in conjunction with the kernel replacement capability in csrc, and uses it as the exit point.
  + Except for specific plugins, subsequent plugin capabilities will be integrated into mstracekit first. Specific plugins can be integrated into specific tools. For example: monitoring plugins go to msprof, and detection plugins go to mssanitizer.

## Tool Limitations and Notes

### Framework Description

To implement code stubs, taking the addition of a runtime module for the coverage tool as an example, you need to add and remove content in the following areas:

+ csrc/include: This is the header file for the hooked functions. It is best to include all of them at once (even if they are not used, they can be kept here).
+ csrc/xxx/xxxOrigin: This is the alias for the hooked interface. If it does not exist when used, add it (as long as a tool has implemented it, one will be added).
+ csrc/xxx/InjectionOfxxxx: This is the implementation of the injection function. If there are no changes, you only need to modify CMakeLists.txt to include it.
+ bind/Bindxxxx: This part must be added every time a new interface is added.
+ csrc/xxx/CMakeLists.txt: If there are newly added files, they need to be added to the corresponding directory.

NOTE:

1. Do not use wildcard matching in `CMakeLists.txt`. Use precise matching to support incremental compilation.
2. If different stub implementations exist for the same interface and the differences are significant, isolate them by tool category in separate folders. If the differences are minor, use macro isolation only.
3. For interfaces with the same name but different implementations across tools, the `InjectionOfXXX` naming should differ. It is recommended to append `ForXXX` at the end and use compilation isolation. Macro isolation is not recommended because we have only one UT process. Additionally, it is recommended to implement the code on the same main branch.

### Positioning & Peripheral Collaboration

For host injection:

1. This repository provides only complete binary libraries. The hooking requirements of different tools are distinguished by compilation options.
2. For the same interface, the injection content differs between tools. This difference is associated within the bind folder.

For kernel injection:

1. The include folder provides external header file interfaces. Currently, msbit's capability to generate control information consists of only one file, so no folder is created for now. The capability of msbit to parse .so files and generate new kernels is currently placed within the customDBI class. Future external interfaces can be implemented in core. To add usage examples, you can create a new tools folder to store different hooking cases. Each tool's own hooking functions and bind calls are implemented within the tool itself. The tool build provides .so files and does not place them in the base component.
2. Different plugins perceive different instructions to perform operations.
3. The integration module is responsible for parsing and processing the host's structures.
4. It needs to collaborate with host injection, and is introduced into the decorated function of host injection through header files.
