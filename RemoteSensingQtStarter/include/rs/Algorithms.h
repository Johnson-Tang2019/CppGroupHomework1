#pragma once

#include "rs/ProcessingAlgorithm.h"
#include <memory>
#include <QString>
#include <vector>

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

// DemReconstructionPipeline：DEM 重建核心管线
// 功能：输入立体像对，利用 SGBM 算法进行稠密立体匹配，计算视差并重建 DEM。
class DemReconstructionPipeline {
public:
    // 流程所需的所有外部输入参数集合
    struct Inputs {
        QString leftImagePath;        // 左影像文件绝对路径
        QString rightImagePath;       // 右影像文件绝对路径
        QString cameraFilePath;       // 相机参数文件路径（内参和外参）
        QString controlPointFilePath; // 地面控制点文件路径（用于绝对定向）
        QString outputDirectory;      // DEM 最终输出目录
    };

    // 执行 DEM 重建流程，返回生成的 DEM 图层智能指针
    std::shared_ptr<DemLayer> reconstruct(const Inputs& inputs) const;
};

// OrthorectificationPipeline：正射影像校正管线
// 功能：结合原始影像与 DEM 高程数据，消除地形起伏造成的投影差，生成正射影像。
class OrthorectificationPipeline {
public:
    // 执行正射校正，返回校正后的影像图层与状态信息
    ProcessingResult rectify(const RasterLayer& image, const DemLayer& dem) const;
};

} // namespace rs

// PointCloudFilterAlgorithm：点云降采样与滤波算法
// 功能：体素降采样 + 统计滤波去噪
class PointCloudFilterAlgorithm final {
public:
    struct Parameters {
        double voxelSize {0.1};       // 体素网格大小（降采样间隔）
        int meanK {20};               // 统计滤波邻居数
        double stddevThreshold {2.0}; // 标准差倍数阈值
    };

    QString name() const;
    QString category() const;

    // 执行体素降采样
    QVector<QVector3D> voxelDownsample(const QVector<QVector3D>& input,
                                       double voxelSize) const;
    // 执行统计滤波（去除离群点）
    QVector<QVector3D> statisticalOutlierRemoval(const QVector<QVector3D>& input,
                                                  int meanK,
                                                  double stddevThreshold) const;
};

// PointCloudToDemAlgorithm：点云转 DEM 算法
// 将点云投影到规则格网，用格网内最高/最低/平均点生成 DEM。
class PointCloudToDemAlgorithm final {
public:
    struct Parameters {
        double gridResolution {1.0};   // DEM 格网分辨率
        bool useMaxZ {true};           // true=DSM(最高点), false=DTM(平均)
    };

    QString name() const;
    QString category() const;

        // 执行点云转 DEM
    std::shared_ptr<rs::DemLayer> convert(const QVector<QVector3D>& points,
                                      const Parameters& params,
                                      const QString& layerName) const;
};