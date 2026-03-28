<h1 align="center">MindStudio Ops Common</h1>

<div align="center">
<h2>昇腾 AI 算子工具基础组件</h2>

 [![Ascend](https://img.shields.io/badge/Community-MindStudio-blue.svg)](https://www.hiascend.com/developer/software/mindstudio) 
 [![License](https://badgen.net/badge/License/MulanPSL-2.0/blue)](./docs/LICENSE)

</div>

## ✨ 最新消息

* [2025.12.30]：MindStudio Ops Common项目首次上线 

<br>

## ️ ℹ️ 简介

MindStudio Ops Common（算子工具基础组件，msOpCom）是算子工具基础组件，对于昇腾产品工具而言，在开发维测环境下，需要将运行时进行改变，以支持"隐式"行为的隐藏。

## ⚙️ 功能介绍

msOpCom主要为算子工具提供统一的劫持能力，使得算子工具可以完成桩函数注入、接口劫持等功能。此处仅针对动态插桩能力，静态插桩能力由各组件自身承载。

  - (对于劫持目标) 提供原生接口的管理
  - (统一共性插件) 提供注入(修饰)函数的管理
  - (对于特定工具) 提供劫持接口的统一管理，并支持注入函数的通信控制
  - 具体工具直接引用该仓库，完成二进制的编译，提供submodule功能支持特定工具injection的单独编译。

## 📦 安装指南

介绍msOpCom工具的环境依赖及安装方式，具体请参见[《msOpCom 开发环境搭建及编译和UT方法》](./docs/zh/development_guide/develop_guide.md)。

## 📘 使用指南

msOpCom作为公共组件不具备独立功能，无法独立使用，请参考相关工具如[msSanitizer](https://gitcode.com/Ascend/mssanitizer)或[msOpProf](https://gitcode.com/Ascend/msopprof)相关内容。

## 💡 典型案例

msOpCom作为公共组件不具备独立功能，不具备功能案例，请参考相关工具如[msSanitizer](https://gitcode.com/Ascend/mssanitizer)或[msOpProf](https://gitcode.com/Ascend/msopprof)相关内容。

## 🛠️ 贡献指南

若您有意参与项目贡献，请参见 [《贡献指南》](./docs/zh/contributing/contributing_guide.md)。

## ⚖️ 相关说明

* [《License声明》](./docs/zh/legal/license_notice.md) 
* [《安全声明》](./docs/zh/legal/security_statement.md) 
* [《免责声明》](./docs/zh/legal/disclaimer.md)

## 🤝 建议与交流

欢迎大家为社区做贡献。如果有任何疑问或建议，请提交[Issues](https://gitcode.com/Ascend/msopcom/issues)，我们会尽快回复。感谢您的支持。

## 🙏 致谢

本工具由华为公司 **计算产品线** 贡献。    
感谢来自社区的每一个PR，欢迎贡献。
