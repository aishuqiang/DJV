# DJV Windows 10 免安装包

## 环境要求（在一台 Windows 10/11 电脑上打包）

- Visual Studio 2022（使用 **x64 Native Tools Command Prompt for VS 2022**）
- CMake 3.31+
- NASM（编译 FFmpeg / libjpeg）
- MSYS2（编译 FFmpeg）
- Strawberry Perl（网络相关依赖）
- Python 3.11（若开启 USD）
- NSIS 仅在你需要 `.exe` 安装包时才必须；**免安装 ZIP 不需要 NSIS**

详见仓库根目录 [README.md](../README.md) 的 “Building on Windows” 一节。

## 一键打包（推荐）

在 VS 2022 x64 命令提示符中，进入本仓库根目录后执行：

```bat
package-win.bat
```

脚本会依次：

1. `sbuild-win.bat` — 编译依赖与 DJV（首次很慢，数小时属正常）
2. `etc\Windows\windows-portable-package.bat` — 生成免安装 ZIP

成功后，在 `build-Release\` 下得到类似：

```text
djv-3.4.3-dev-windows-amd64.zip
```

## 仅重新打 ZIP（已编译过）

若已完成 super-build，只需：

```bat
set PATH=%CD%\install-Release\bin;%PATH%
etc\Windows\windows-portable-package.bat %CD% Release
```

## 在 Win10 上使用

1. 将 ZIP 解压到任意目录，例如 `D:\DJV`
2. 双击 **`DJV.bat`**，或运行 `bin\djv.exe`
3. 卸载：删除整个文件夹即可

包内包含 `README-portable-zh.txt` 中文说明。

## 说明

- 当前开发机若为 macOS，**无法直接交叉编译出 Windows 包**，必须在 Windows 上执行上述步骤，或使用具备 `windows-latest` 的 CI。
- 官方预编译发布见：<https://github.com/grizzlypeak3d/DJV/releases>
