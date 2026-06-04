# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

这是一个发给学生的**遥感影像处理系统 Qt/C++ 工程骨架**，不是完整答案。它保留了主界面、菜单、图层树、日志区、核心头文件和算法接口，供学生在统一规范下继续实现。详见 `docs/student_tasks.md`。

## 构建

```
# 配置（在项目根目录下）
cmake -B build -DCMAKE_PREFIX_PATH=<Qt6 安装路径>

# 编译
cmake --build build

# 运行
./build/RemoteSensingQtStarter
```

依赖：Qt6 (Widgets)、GDAL（必需）、OpenCV（可选，通过 `RS_WITH_OPENCV` 宏控制）。MSVC 编译器下启用了 `/W4 /permissive- /utf-8`。

无现成的 lint 或测试目标——CMakeLists.txt 未配置 CTest 或静态分析工具。

## 架构

### 分层设计

- **`rs::DataObject`** (`include/rs/DataObject.h`) — 所有数据类型的抽象基类，持有 name/path/type/visible。`DataType` 枚举：`Raster, PointCloud, Mesh, Dem, Result`。
- **`rs::Geometry.h`** — 具体数据层类型：`RasterLayer`（含 `QVector<RasterBand>`，每个 `RasterBand` 通过 GDAL 加载后的降采样样本）、`PointCloudLayer`、`MeshLayer`、`DemLayer`。`RasterLayer` 同时持有 `displayImage_`（经设色渲染后的 QImage）和 `renderDescription_`。
- **`rs::LayerManager<T>`** (`include/rs/LayerManager.h`) — 模板化的图层容器，以 `std::vector<std::shared_ptr<T>>` 存储，MainWindow 使用 `LayerManager<DataObject>` 统一管理所有图层。
- **`rs::io` 命名空间** (`include/rs/RasterIO.h`, `src/RasterIO.cpp`) — GDAL 实现：`loadRasterDataset()` 读取遥感影像并降采样预览、`renderSingleBandGray()` / `renderRgbComposite()` 生成 QImage、`exportDemAsGeoTiff()` 导出 DEM。

### 算法框架

- **`rs::ProcessingAlgorithm`** (`include/rs/ProcessingAlgorithm.h`) — 纯虚基类，定义 `name()`、`category()`、`parameterSchema()`、`execute()`。所有影像处理算法继承此类，体现多态。
- **`rs::Algorithms.h`** / `src/Algorithms.cpp` — 三个 `final` 算法类：`HistogramAlgorithm`、`HistogramEqualizationAlgorithm`、`FeatureExtractionAlgorithm`，均为 TODO 骨架。另有 `DemReconstructionPipeline` 和 `OrthorectificationPipeline` 是独立管线类（不继承 ProcessingAlgorithm），仅方法签名存在。

### UI 结构

- **`rs::MainWindow`** (`include/rs/MainWindow.h`, `src/MainWindow.cpp`) — QMainWindow 子类。左侧 `QTreeWidget` 按"源数据/遥感影像、点云、Mesh、DEM"和"处理结果/直方图、均衡化"分组；右侧 `QTabWidget` 包含二维 `QGraphicsView` 和三维占位符；底部只读 `QTextEdit` 日志区。
- 菜单栏：数据（加载/删除/清空）、影像处理（波段设色/直方图/均衡化/特征提取）、摄影测量/三维（DEM 重建/正射校正）。
- 图层树节点使用自定义角色（`kLayerIndexRole`, `kBandIndexRole`, `kNodeKindRole`）绑定数据；选中波段时自动显示单波段灰度，选中图层时显示已渲染的 displayImage。
- `selectedLayerIndices()` / `selectedRaster()` / `selectedBandIndex()` 从树控件中解析当前选中项。
- **`rs::RasterRenderDialog`** (`include/rs/RasterRenderDialog.h`, `src/RasterRenderDialog.cpp`) — `askRasterRenderRequest()` 目前是桩函数，直接返回默认 `RasterRenderRequest{}`（AutoRgb 模式）。

### 关键约定

- 所有 `rs` 命名空间下的类使用 `std::shared_ptr` 管理所有权；对外通过 `LayerManager` 访问。
- 日志统一通过 `MainWindow::appendLog()` 输出，带 HH:mm:ss 时间戳。
- 错误处理：加载/处理/导出失败时抛 `std::runtime_error`，由调用方 catch 并写入日志。
- 三维视图目前是 `QLabel` 占位符，预留了 `QOpenGLWidget / Qt3D / VTK` 集成点。

## 项目状态

当前分支 `main` 上有未提交的修改（CMakeLists.txt 和多个 .h/.cpp 文件）。工程大量函数为 TODO 桩实现，学生需补全 GDAL 波段读取、算法执行、参数对话框、三维渲染、点云/Mesh 加载等功能。
