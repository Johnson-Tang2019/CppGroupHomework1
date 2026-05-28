# RemoteSensingQtStarter

这是发给学生的极简可扩展 Qt/C++ 工程骨架，不是完整答案。它保留了主界面、菜单、图层树、日志区、核心头文件和算法接口，便于学生在统一规范下继续实现。

## 当前骨架已包含

- Qt Widgets 主窗口：左侧工程图层，右侧二维影像/三维场景切换，下方工作日志。
- 图层管理接口：影像、点云、Mesh、DEM、处理结果分组。
- 遥感影像接口：`RasterLayer` 支持多波段、投影、地理变换、显示设色描述。
- GDAL 读取扩展点：`rs::io::loadRasterDataset`、单波段渲染、RGB 合成、DEM GeoTIFF 导出。
- 参数化算法接口：所有算法继承 `ProcessingAlgorithm`，通过 `parameterSchema()` 暴露参数。
- 摄影测量扩展点：DEM 重建、正射影像校正、ORB/SIFT/AKAZE 特征提取。
- C++ 课程知识点示范：继承、多态、模板、智能指针、异常处理、STL 容器。

## 学生需要完成

- 用 GDAL 正式读取 GeoTIFF/IMG/JP2 等遥感数据，而不是只读普通图片。
- 在影像图层下显示 Band 1..N，并支持单波段灰度、RGB 波段组合、伪彩色设色。
- 对当前 RGB 显示影像或指定单波段执行直方图、均衡化、分类、目标检测等处理。
- 支持二维缩放、拖动、适应窗口，以及三维场景基础交互。
- 选中两张影像后完成 DEM 重建流程；选中影像和对应 DEM 后完成正射校正。

推荐先阅读 [docs/student_tasks.md](docs/student_tasks.md)。
