# RemoteSensingQtStarter

推荐先阅读 [docs/student_tasks.md](docs/student_tasks.md)。

这是发给学生的极简可扩展 Qt/C++ 工程骨架，不是完整答案 。它保留了主界面、菜单、图层树、日志区、核心头文件和算法接口，便于学生在统一规范下继续实现。

## 构建说明

### 环境要求

- Qt 6（或 Qt 5）
- 编译器：MSVC（推荐）、MinGW、Clang 等
- CMake ≥ 3.16
- （可选）OpenCV
- （可选）GDAL

### 构建的步骤

### 生成 Windows 安装包

代码修改完成后，可以在仓库根目录运行一键打包脚本：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File RemoteSensingQtStarter\package_release.ps1
```

脚本会自动执行以下步骤：

- 将源码复制到纯英文临时目录，避免中文路径影响 CMake/Qt 构建。
- 使用 Visual Studio 的 `vcvars64.bat` 和 `NMake Makefiles` 配置 Release 构建。
- 编译 `RemoteSensingQtStarter.exe`。
- 运行 `windeployqt` 收集 Qt、OpenCV 和 VC 运行库。
- 复制 vcpkg 运行库目录中的 DLL，包括 `gdal.dll` 及其依赖。
- 生成便携运行目录和 Windows 自解压安装包。

生成结果位于仓库根目录的 `dist` 目录：

- `dist\RemoteSensingQtStarter-Setup.exe`：安装包。
- `dist\RemoteSensingQtStarter`：便携版运行目录。
- `dist\RemoteSensingQtStarter-payload.zip`：安装包使用的压缩载荷。
- `dist\package_release_last.log`：最近一次打包日志。

安装包会安装到当前用户的 `%LOCALAPPDATA%\RemoteSensingQtStarter`，并创建桌面和开始菜单快捷方式。

