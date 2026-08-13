<h1 align="center">MindStudio Ops Common</h1>

<div align="center">
<h2>Ascend AI Operator Tool Basic Component</h2>

 [![Ascend](https://img.shields.io/badge/Community-MindStudio-blue.svg)](https://www.hiascend.com/en/developer/software/mindstudio) 
 [![License](https://badgen.net/badge/License/MulanPSL-2.0/blue)](./LICENSE)

</div>

[简体中文](./README.md) | English

## ✨ What's New

<span style="font-size:14px;">

🔹 **[2025.12.31]**: The MindStudio Ops Common project is now fully open source

</span>

## ️ ℹ️ Introduction

MindStudio Ops Common (msOpCom) is a basic component of Ascend AI operator tools. For Ascend products, it modifies the runtime environment in development and debugging scenarios to support "implicit" behavior injection.

## ⚙️ Features

msOpCom provides a unified hooking capability for operator tools, enabling features such as stub function injection and API hooking. This applies only to dynamic instrumentation. Static instrumentation is handled by individual components themselves.

| Function | Description |
|---------|--------|
| **Native API management** | Provides unified management of native APIs for hooking targets. |
| **Injection function management** | As a unified common plugin, provides registration, configuration, and lifecycle management for injection (decoration) functions. |
| **API hooking and communication control** | Provides centralized management of hooked APIs for specific tools, and supports communication control mechanisms between injected functions. |
| **Submodule Compilation Support** | Supports specific tools referencing this repository as a submodule to complete binary compilation, and enables independent compilation of injection modules for specific tools. |

## 📦 Installation Guide

Describes the environment dependencies and how to install msOpCom. For details, see [MindStudio Ops Common Development Environment Setup, Compilation, and Unit Testing](./docs/en/development_guide/develop_guide.md).

## 📘 Usage Guide

msOpCom is a common component and does not have independent functionality. It cannot be used independently. For details, see related tools such as [msSanitizer](https://gitcode.com/Ascend/mssanitizer) or [msOpProf](https://gitcode.com/Ascend/msopprof).

## 💡 Typical Cases

msOpCom is a common component and does not have independent functionality. It does not have functional cases. For details, see related tools such as [msSanitizer](https://gitcode.com/Ascend/mssanitizer) or [msOpProf](https://gitcode.com/Ascend/msopprof).

## 🛠️ Contribution Guide

Welcome to contribute to the project. For details, see the [Contribution Guide](./docs/en/contributing/contributing_guide.md).

## ⚖️ Related Statements

🔹 [License Statement](./docs/en/legal/license_notice.md)  
🔹 [Security Statement](./docs/en/legal/security_statement.md)  
🔹 [Disclaimer](./docs/en/legal/disclaimer.md)  

## 🤝 Suggestions and Communication

You are welcome to contribute to the community. If you have any questions or suggestions, please submit an [Issue](https://gitcode.com/Ascend/msopcom/issues). We will respond as soon as possible. Thank you for your support.

|                                      📱 Follow MindStudio Official Account                                       | 💬 More Communication & Support                                                                                                                                                                                                                                                                                                                                                                                                                     |
|:-----------------------------------------------------------------------------------------------:|:-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| <img src="https://gitcode.com/Ascend/msot/blob/master/docs/zh/figures/readme/officialAccount.png" width="120"><br><sub>*Scan the QR code for the latest updates*</sub> | 💡 **Join WeChat Group**:<br>Follow the official account and reply "communication group" to get the group QR code.<br><br>🛠️ **Other Channels**:<br>👉 Ascend Assistant: [![WeChat](https://img.shields.io/badge/WeChat-07C160?style=flat-square&logo=wechat&logoColor=white)](https://gitcode.com/Ascend/msot/blob/master/docs/zh/figures/readme/xiaozhushou.png)<br>👉 Ascend Forum: [![Website](https://img.shields.io/badge/Website-%231e37ff?style=flat-square&logo=RSS&logoColor=white)](https://www.hiascend.com/forum/) |

## 🙏 Acknowledgments

This tool is jointly contributed by the following departments of Huawei:    
🔹 Ascend Computing MindStudio Development Dept  
🔹 Ascend Computing Ecosystem Enablement Dept  
🔹 Ascend AI Cloud Service  
🔹 2012 Compiler Lab  
🔹 2012 Markov Lab  
Thanks for every PR from the community. Contributions are welcome.
