# MindStudio Ops Common Development Environment Setup, Compilation, and Unit Testing

<br>

## 1. Prerequisites

Refer to [MindStudio Ops Common Architecture Design](./architecture.md) to learn about the code framework.

## 2. Development Environment Preparation

- Hardware environment: See [Ascend Product Overview](https://www.hiascend.com/document/detail/en/AscendFAQ/ProduTech/productform/hardwaredesc_0001.html).

- Follow the document below for environment configuration: [Operator Tool Development Environment Setup Guide](https://gitcode.com/Ascend/msot/blob/master/docs/en/common/dev_env_setup.md).

## 3. Build and Packaging

There are two methods, with the following advantages and disadvantages:

| Method | Applicable Scenario | Advantages | Disadvantages |
|------|---------|------|------|
| One-click script | Initial build, CI/CD pipeline | Zero configuration, one-step completion | Cannot execute a single step independently |
| Step-by-step script | Daily development, incremental compilation | Flexible, high efficiency | Requires multiple steps |

### 3.1 Method 1: One-click Script

```shell
python build.py
```

### 3.2 Method 2: Step-by-step Script

#### 3.2.1 Dependency Download

```shell
python download_dependencies.py
```

#### 3.2.2 Compilation

##### 3.2.2.1 Compilation Startup

Run the following command to start compilation:

```shell
mkdir build
cd build
cmake ..
make -j$(nproc) && make install  # -j specifies the number of parallel compilation jobs, which you can set as needed. If nproc is unavailable, manually enter a number (for example, -j8).
```

>[!NOTE] NOTE  
> **Debug Version Compilation Method**  
> If you need to perform gdb or vscode graphical breakpoint debugging, you must compile the debug version. The method is as follows:  
> Add the parameter `-DCMAKE_BUILD_TYPE=Debug` when running the cmake command above, for example: `cmake ../cmake -DCMAKE_BUILD_TYPE=Debug`

If the generation time of each file in the `output` directory has been updated to the current compilation time, the compilation has completed successfully.

##### 3.2.2.2 Compilation Result Description

The compilation results are generated in the output directory:

```text
output/
|-- bin                                                  # Executable bin files
|-- lib                                                  # Static library files
|-- lib64                                                # Various dynamic libraries and .o files
```

#### 3.2.3 Cleanup/Recompilation

Delete the build directory and re-execute [Section 3.2.2.1](#3221-compilation-startup):   

```shell
rm -rf build
```

## 4. Unit Testing

### 4.1 Method 1: One-Click Script

```shell
python build.py test
```

### 4.2 Method 2: Step-by-Step Script

#### 4.2.1 Dependency Download

```shell
python download_dependencies.py test
```

#### 4.2.2 Unit Testing Execution

```shell
mkdir build_ut
cd build_ut
cmake .. -DBUILD_TESTS=ON
make -j$(nproc) injectionTest # -j specifies the number of parallel compilation jobs, which can be set as needed; if nproc is unavailable, manually enter a number (e.g., -j8).
export LD_LIBRARY_PATH=$PWD/test/stub:$LD_LIBRARY_PATH
./test/injectionTest
```

Output similar to the following, where the number of run cases equals the number of passed cases, indicates success:

```text
[==========] 912 tests from 139 test cases ran. (46693 ms total)
[  PASSED  ] 912 tests.
```

#### 4.2.3 Cleanup/Recompilation

Delete the build directory and re-execute [Section 4.2.2](#422-unit-testing-execution):

```shell
rm -rf build_ut  
```
