#pragma once
//2026.6.6
#include "rs/ProcessingAlgorithm.h"
#include <memory>

namespace rs {

// 前向声明，防止因为包含顺序问题导致编译报错
class RasterLayer;
class DemLayer;

// HistogramAlgorithm：灰度直方图统计算法
// 功能：统计指定波段像元值分布，并绘制成 2D 柱状图/折线图。
class HistogramAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    
    // 执行直方图统计，返回绘制好的直方图图像结果
    ProcessingResult execute(const RasterLayer& input,
                             const ProcessingContext& context) const override;
};

// HistogramEqualizationAlgorithm：直方图均衡化增强算法 (支持 CLAHE)
// 功能：通过自适应直方图均衡化 (CLAHE) 改善影像对比度，使暗部和亮部细节更清晰。
class HistogramEqualizationAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    
    // 执行均衡化处理，返回增强后的影像结果
    ProcessingResult execute(const RasterLayer& input,
                             const ProcessingContext& context) const override;
};

// FeatureExtractionAlgorithm：ORB/SIFT 特征提取算法
// 功能：对影像提取关键点(KeyPoints)和描述子(Descriptors)，用于后续影像匹配。
class FeatureExtractionAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    
    // 执行特征提取，并在原图上绘制出关键点
    ProcessingResult execute(const RasterLayer& input,
                             const ProcessingContext& context) const override;
};

// DemReconstructionAlgorithm：DEM 重建算法
// 功能：输入立体像对，利用 SGBM 算法进行稠密立体匹配，计算视差并重建 DEM。
// 输入参数映射：
//   - 左影像 ← input（主输入参数）
//   - 右影像路径 ← context.parameters["rightImagePath"]
//   - 输出目录 ← context.parameters["outputDirectory"]
//   - 相机文件（可选） ← context.parameters["cameraFilePath"]
//   - 控制点文件（可选） ← context.parameters["controlPointFilePath"]
class DemReconstructionAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input,
                             const ProcessingContext& context) const override;
};

// OrthorectificationAlgorithm：正射影像校正算法
// 功能：结合原始影像与 DEM 高程数据，消除地形起伏造成的投影差，生成正射影像。
// 输入参数映射：
//   - 影像 ← input（主输入参数）
//   - DEM ← context.auxiliaryDem
class OrthorectificationAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input,
                             const ProcessingContext& context) const override;
};

// PointCloudVoxelDownsampleAlgorithm：点云体素降采样算法
// 功能：对点云数据进行体素网格降采样。
// 输入参数映射：
//   - 点云数据 ← context.pointCloudData
//   - 体素大小 ← context.parameters["voxelSize"]（默认 0.1）
//   - input 参数忽略（本算法不操作栅格数据）
class PointCloudVoxelDownsampleAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input,
                             const ProcessingContext& context) const override;
};

// PointCloudStatisticalFilterAlgorithm：点云统计滤波算法
// 功能：基于统计方法去除点云中的离群点。
// 输入参数映射：
//   - 点云数据 ← context.pointCloudData
//   - 邻居数 ← context.parameters["meanK"]（默认 20）
//   - 标准差阈值 ← context.parameters["stddevThreshold"]（默认 2.0）
//   - input 参数忽略（本算法不操作栅格数据）
class PointCloudStatisticalFilterAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input,
                             const ProcessingContext& context) const override;
};

// PointCloudToDemAlgorithm：点云转 DEM 算法
// 功能：将点云投影到规则格网，用格网内最高/最低/平均点生成 DEM。
// 输入参数映射：
//   - 点云数据 ← context.pointCloudData
//   - 格网分辨率 ← context.parameters["gridResolution"]（默认 1.0）
//   - 使用最高点 ← context.parameters["useMaxZ"]（默认 true）
//   - 图层名称 ← context.parameters["layerName"]
//   - input 参数忽略（本算法不操作栅格数据）
class PointCloudToDemAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;
    QString category() const override;
    std::vector<AlgorithmParameter> parameterSchema() const override;
    ProcessingResult execute(const RasterLayer& input,
                             const ProcessingContext& context) const override;
};

} // namespace rs