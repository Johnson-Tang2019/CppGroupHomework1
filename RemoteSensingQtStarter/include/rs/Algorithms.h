#pragma once

#include "rs/ProcessingAlgorithm.h"

#include <memory>
#include <stdexcept>

namespace rs {

class HistogramAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;              // 返回算法名称："灰度直方图"
    QString category() const override;           // 返回算法分类："影像统计"
    std::vector<AlgorithmParameter> parameterSchema() const override;  // 返回参数列表（分箱数、忽略 NoData 等）
    ProcessingResult execute(const RasterLayer& input,
                             const ProcessingContext& context) const override;  // 执行直方图统计
};

// ============================================================================
// HistogramEqualizationAlgorithm：直方图均衡化增强算法
// 对选中影像进行直方图均衡化，改善影像对比度
// ============================================================================
class HistogramEqualizationAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;              // 返回算法名称："直方图均衡化"
    QString category() const override;           // 返回算法分类："影像增强"
    std::vector<AlgorithmParameter> parameterSchema() const override;  // 返回参数列表（处理波段、CLAHE 限幅等）
    ProcessingResult execute(const RasterLayer& input,
                             const ProcessingContext& context) const override;  // 执行均衡化处理
};

// ============================================================================
// FeatureExtractionAlgorithm：ORB/SIFT 特征提取算法
// 对选中影像提取关键点和描述子，支持多种特征检测方法
// ============================================================================
class FeatureExtractionAlgorithm final : public ProcessingAlgorithm {
public:
    QString name() const override;              // 返回算法名称："ORB/SIFT 特征提取"
    QString category() const override;           // 返回算法分类："摄影测量"
    std::vector<AlgorithmParameter> parameterSchema() const override;  // 返回参数列表（方法、最大特征数等）
    ProcessingResult execute(const RasterLayer& input,
                             const ProcessingContext& context) const override;  // 执行特征提取
};

// ============================================================================
// DemReconstructionPipeline：DEM 重建流程
// 使用两张有重叠的立体像对影像，通过特征匹配和视差计算重建 DEM
// ============================================================================
class DemReconstructionPipeline {
public:
    // 流程输入参数集合
    struct Inputs {
        QString leftImagePath;        // 左影像文件路径
        QString rightImagePath;       // 右影像文件路径
        QString cameraFilePath;       // 相机参数文件路径（内参和外参）
        QString controlPointFilePath; // 地面控制点文件路径（用于绝对定向）
        QString outputDirectory;      // DEM 输出目录
    };

    // 执行 DEM 重建流程，输入左右影像和相关参数，返回生成的 DEM 图层
    std::shared_ptr<DemLayer> reconstruct(const Inputs& inputs) const;
};

// ============================================================================
// OrthorectificationPipeline：正射影像校正流程
// 使用影像和对应的 DEM 消除几何畸变，生成正射影像
// ============================================================================
class OrthorectificationPipeline {
public:
    // 执行正射校正
    // image - 待校正的遥感影像图层
    // dem   - 对应的数字高程模型图层
    // 返回：处理结果（正射影像 + 消息文本）
    ProcessingResult rectify(const RasterLayer& image, const DemLayer& dem) const;
};

} // namespace rs
