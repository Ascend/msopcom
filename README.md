<h1 align="center">MindStudio Ops Common</h1>

<div align="center">
<h2>昇腾 AI 算子工具基础组件</h2>

 [![Ascend](https://img.shields.io/badge/Community-MindStudio-blue.svg)](https://www.hiascend.com/developer/software/mindstudio) 
 [![License](https://badgen.net/badge/License/MulanPSL-2.0/blue)](./LICENSE)

</div>

## ✨ 最新消息

<span style="font-size:14px;">

🔹 **[2025.12.31]**：MindStudio Ops Common 项目全面开源

</span>

## ️ ℹ️ 简介

MindStudio Ops Common（算子工具基础组件，msOpCom）是算子工具基础组件，对于昇腾产品工具而言，在开发维测环境下，需要将运行时进行改变，以支持"隐式"行为的隐藏。

## ⚙️ 功能介绍

msOpCom主要为算子工具提供统一的劫持能力，使得算子工具可以完成桩函数注入、接口劫持等功能。此处仅针对动态插桩能力，静态插桩能力由各组件自身承载。

| 功能名称 | 功能描述 |
|---------|--------|
| **原生接口管理** | 针对劫持目标，提供原生接口的统一管理能力。 |
| **注入函数管理** | 作为统一共性插件，提供对注入（修饰）函数的注册、配置与生命周期管理。 |
| **劫持接口与通信控制** | 面向特定工具，提供劫持接口的集中管理，并支持注入函数间的通信控制机制。 |
| **子模块化编译支持** | 支持具体工具以 submodule 形式引用本仓库，完成二进制编译，并实现特定工具 injection 模块的独立编译。 |

## 📦 安装指南

介绍msOpCom工具的环境依赖及安装方式，具体请参见 [《msOpCom 开发环境搭建及编译和UT方法》](./docs/zh/development_guide/develop_guide.md)。

## 📘 使用指南

msOpCom作为公共组件不具备独立功能，无法独立使用，请参考相关工具如 [msSanitizer](https://gitcode.com/Ascend/mssanitizer)或 [msOpProf](https://gitcode.com/Ascend/msopprof)相关内容。

## 💡 典型案例

msOpCom作为公共组件不具备独立功能，不具备功能案例，请参考相关工具如 [msSanitizer](https://gitcode.com/Ascend/mssanitizer)或 [msOpProf](https://gitcode.com/Ascend/msopprof)相关内容。

## 🛠️ 贡献指南

欢迎参与项目贡献，请参见 [《贡献指南》](./docs/zh/contributing/contributing_guide.md)。

## ⚖️ 相关说明

🔹 [《许可证声明》](./docs/zh/legal/license_notice.md)  
🔹 [《安全声明》](./docs/zh/legal/security_statement.md)  
🔹 [《免责声明》](./docs/zh/legal/disclaimer.md)  

## 🤝 建议与交流

欢迎大家为社区做贡献。如果有任何疑问或建议，请提交[Issues](https://gitcode.com/Ascend/msopcom/issues)，我们会尽快回复。感谢您的支持。

|                                      📱 关注 MindStudio 公众号                                       | 💬 更多交流与支持                                                                                                                                                                                                                                                                                                                                                                                                                     |
|:-----------------------------------------------------------------------------------------------:|:-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| <img src="https://gitcode.com/Ascend/msot/blob/master/docs/zh/figures/readme/officialAccount.png" width="120"><br><sub>*扫码关注获取最新动态*</sub> | 💡 **加入微信交流群**：<br>关注公众号，回复“交流群”即可获取入群二维码。<br><br>🛠️ **其他渠道**：<br>👉 昇腾助手：[![WeChat](https://img.shields.io/badge/WeChat-07C160?style=flat-square&logo=wechat&logoColor=white)](https://gitcode.com/Ascend/msot/blob/master/docs/zh/figures/readme/xiaozhushou.png)<br>👉 昇腾论坛：[![Website](https://img.shields.io/badge/Website-%231e37ff?style=flat-square&logo=RSS&logoColor=white)](https://www.hiascend.com/forum/) |

## 🙏 致谢

本工具由华为公司的下列部门联合贡献：    
🔹 昇腾计算MindStudio开发部  
🔹 昇腾计算生态使能部  
🔹 华为云昇腾云服务  
🔹 2012编译器实验室  
🔹 2012马尔科夫实验室  
感谢来自社区的每一个 PR，欢迎贡献！
