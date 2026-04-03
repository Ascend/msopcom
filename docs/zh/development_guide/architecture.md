# MindStudio Ops Common 架构设计说明

## 目录设计

针对该软件仓，整体目录设计思路如下：

```text
MindStudio-Ops-Common
|-- .gitsubmodules          # 管理依赖的submodule文件
|-- build.py                # 一键式构建脚本入口
|-- csrc                    # 提供针对CPU侧接口的劫持能力
  |-- include               # 劫持对象的原始头文件
  |-- core                  # 核心功能：通信(控制，数据-数据流/文件，支持父子孙进程);接口注册;接口绑定;
  |-- runtime               # 提供runtime下的接口的劫持函数 和 原始接口的别名
    |-- RuntimeOrigin.h     # 提供原始接口的别名
    |-- injectionOfxxx      # 提供runtime下的xx接口的劫持函数
  |-- ...
  |-- bind                  # 将接口与具体的工具绑定，并支持进程间控制和数据传输能力
    |-- BindCoverage.cpp    # 将decorated function和具体的runtime接口绑定,每个工具一个
  |-- kernel_injection      # 提供针对kernel侧的劫持能力, 内容过少
|-- test                    # 测试用例看护
|-- thirdparty              # 三方依赖存放目录
|-- docs                    # 项目文档介绍
└── README.md               # 整体仓代码说明
```

说明：

- csrc中提供了如下功能：

  + (对于劫持目标) 提供原生接口的管理
  + (统一共性插件) 提供注入(修饰)函数的管理
  + (对于特定工具) 提供劫持接口的统一管理，并支持注入函数的通信控制
  + 具体工具直接引用该仓库，完成二进制的编译，提供submodule功能支持特定工具injection的单独编译。
- 此处kernel_injection仅针对动态插桩能力(静态插桩能力由各组件自身承载)
  + 现支持bisheng-tune，未来需要支持msbit的能力
  + 本身需要配套csrc中的kernel替换能力使能，并由其作为出口
  + 除了特定插件外，后续插件能力优先集成到mstracekit中，特定插件可以集成到特定工具。比如：监控插件到msprof，检测插件到mssanitizer。

## 工具限制与注意事项

### 框架说明

如果要实现代码桩，以coverage工具添加runtime模块为例，主要需要在如下内容中进行添加和删减工作：

+ csrc/include 此处是劫持函数的头文件，最好一次性放全 (即使没用也可以放着)
+ csrc/xxx/xxxOrigin 此处是劫持接口的别名，使用时如果没有则添加（只要有工具实现过，就会加一个）
+ csrc/xxx/InjectionOfxxxx 此处是注入函数的实现，如果没有变化，则仅需修改CMakeLists.txt引入
+ bind/Bindxxxx 每次加新接口时，必然需要添加该部分内容
+ csrc/xxx/CMakeLists.txt 如果存在新增文件，需要添加到对应目录中

说明：

1. 不能在CMakeLists.txt中全匹配，需要写精确匹配，从而支持增量编译。
2. 如果存在同一个接口，不同的桩实现的话，差异较大，可以以工具分类进行文件夹隔离，如果差异较小，可以仅仅宏隔离。
3. 对于同名接口在不同的工具上有不同的实现，这个InjectionOfxxx命名有差异，建议末尾添加上ForXXX，通过编译隔离；不建议宏隔离，因为我们只有1个UT进程。此外，建议代码实现在同一主干上。

### 定位&周边配合

针对host injection部分：

1. 该仓库仅提供完整的二进制库，不同工具的劫持诉求由编译选项进行区分。
2. 针对同个接口，不同工具的注入内容存在差异，该差异在bind文件夹中进行关联。

针对kernel injection部分:

1. include文件夹提供对外的头文件接口, 目前msbit的生成控制信息能力只有msbit一个文件，暂不建立文件夹。而msbit解析so生成新的kernel能力目前放置于customDBI类里。后续对外的接口可以实现在core里。想增加使用示例，可以新增tools文件夹下放置不同的劫持案例。各个工具自己的劫持函数以及bind调用实现在自己工具里，工具构建提供.so，不放在基础组件内。
2. 不同插件感知不同的指令进行操作。
3. 集成模块需要负责host的结构体的解析处理。
4. 本身需要与host injection协同，自身通过头文件方式引入到host injection的decorated function中。
