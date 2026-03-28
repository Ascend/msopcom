# MindStudio Ops Common 开发环境搭建及编译和UT方法

<br>

## 1. 预备知识

请参考[《MindStudio Ops Common 架构设计说明》](./architecture.md)学习代码框架。

## 2. 开发环境准备

- 硬件环境请参见《[昇腾产品形态说明](https://www.hiascend.com/document/detail/zh/AscendFAQ/ProduTech/productform/hardwaredesc_0001.html)》。

- 请按照以下文档进行环境配置：[《算子工具开发环境安装指导》](https://gitcode.com/Ascend/msot/blob/master/docs/zh/common/dev_env_setup.md)。

## 3. 编译打包

分为如下两种方式，优缺点如下：

| 方法 | 适用场景 | 优点 | 缺点 |
|------|---------|------|------|
| 一键式脚本 | 首次构建、CI/CD 流水线 | 零配置，一步到位 | 无法单独执行某一步骤 |
| 分步骤脚本 | 日常开发、增量编译 | 灵活，效率高 | 需要多步操作 |

### 3.1 方法一：一键式脚本

```shell
python build.py
```

### 3.2 方法二：分步骤脚本

#### 3.2.1 下载依赖

```shell
python download_dependencies.py
```

#### 3.2.2 编译

##### 3.2.2.1 启动编译

执行如下命令启动编译：

```shell
mkdir build
cd build
cmake ..
make -j$(nproc) && make install  # -j 是并行编译的 job 数量，可自行指定；nproc 不可用时请手动填数字（例如 -j8）。
```

>[!NOTE] 说明    
> **debug 版本编译方法**    
> 如果想进行 gdb 或 vscode 图形化断点调试等，需要编译 debug 版本，方法如下：   
> 在执行如上 cmake 命令时增加参数 -DCMAKE_BUILD_TYPE=Debug，例如：`cmake ../cmake -DCMAKE_BUILD_TYPE=Debug`

若 `output` 目录下的各文件的生成时间已更新为当前编译时间，则表明编译已成功完成。

##### 3.2.2.2 编译结果说明

编译结果生成到 output 目录下：

```text
output/
|-- bin                                                  # 可执行bin文件
|-- lib                                                  # 静态库文件
|-- lib64                                                # 各种动态库和.o文件
```

#### 3.2.3 清理/重新编译

删除构建目录，重新执行[第 3.2.2.1 节](#3221-启动编译)：   

```shell
rm -rf build
```

## 4. 执行UT测试

### 4.1 方法一：一键式脚本

```shell
python build.py test
```

### 4.2 方法二：分步骤脚本

#### 4.2.1 下载依赖

```shell
python download_dependencies.py test
```

#### 4.2.2 执行UT测试

```shell
mkdir build_ut
cd build_ut
cmake .. -DBUILD_TESTS=ON
make -j$(nproc) injectionTest # -j 是并行编译的 job 数量，可自行指定；nproc 不可用时请手动填数字（例如 -j8）。
export LD_LIBRARY_PATH=$PWD/test/stub:$LD_LIBRARY_PATH
./test/injectionTest
```

输出类似如下跑的用例数和通过用例数相同即表示成功：

```text
[==========] 912 tests from 139 test cases ran. (46693 ms total)
[  PASSED  ] 912 tests.
```

#### 4.2.3 清理/重新编译

删除构建目录，重新执行[第 4.2.2 节](#422-执行ut测试)：   

```shell
rm -rf build_ut  
```
