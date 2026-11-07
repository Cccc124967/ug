# ug

Windows 上的 **Siemens NX（UG）C++ 插件工程生成器**，当前以 NX 2007 为目标。通过交互向导选择开发环境和代码模板，生成可用 VS Code 打开的 CMake 工程。

## 功能

- **扫描开发环境**：查找 MSVC 编译器、NX SDK 和 Windows SDK，也可手动指定路径。
- **交互式创建工程**：设置项目名称与位置，支持快速部署或逐项选择配置。
- **按需生成模板**：提供插件入口、调用守卫与资源管理、向量、矩阵、点和四元数模块，自动补齐模块依赖。
- **生成工程配置**：生成 CMake 构建文件、构建预设和 VS Code 设置；源码可选 GB2312 / UTF-8 编码、CRLF / LF 换行。
- **初始化 Git**：可选创建 Git 仓库，并生成忽略文件与行尾配置。
- **辅助排查与管理**：查看工具链、清除扫描缓存、诊断目录写入问题，以及注册或移除用户 PATH 中的 `ug` 命令。

## 构建与使用

构建 `ug` 需要 Windows、CMake 3.21+ 和支持 C++20 的 MSVC。生成并构建插件还需要 NX SDK 与 Windows SDK；如需初始化 Git，请先安装 Git。

在仓库根目录执行（以安装了 C++ 开发工具的 Visual Studio 2022 为例）：

```powershell
cmake -S . -B agent_build/build_ug -G "Visual Studio 17 2022" -A x64
cmake --build agent_build/build_ug --config Release
.\bin\ug.exe new
```

按向导完成配置。若选择了“入口代码”模板，进入生成的工程目录后即可构建：

```powershell
cmake --preset nx
cmake --build --preset nx-release
```

插件 DLL 输出到生成工程的 `build/` 目录。空工程不含入口源码，也不会生成 `CMakeLists.txt`。

常用辅助命令（在本仓库根目录执行）：

```powershell
.\bin\ug.exe --chain-list   # 重新扫描并显示工具链
.\bin\ug.exe --register     # 注册到用户 PATH，新开终端后可直接使用 ug
.\bin\ug.exe --help         # 查看全部命令
```

## 开发者

- **大肥鱼** — DeekSeek V4.1Flash 1017
- **大肥龙** — GPT6 astra

## 许可证

[MIT](LICENSE)
