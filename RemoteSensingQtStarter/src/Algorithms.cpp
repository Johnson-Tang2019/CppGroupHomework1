#include "rs/Algorithms.h"

#include <stdexcept>

namespace rs {

QString HistogramAlgorithm::name() const {
    return QStringLiteral("灰度直方图");
}

QString HistogramAlgorithm::category() const {
    return QStringLiteral("影像统计");
}

std::vector<AlgorithmParameter> HistogramAlgorithm::parameterSchema() const {
    return {
        {QStringLiteral("bins"), QStringLiteral("分箱数"), QStringLiteral("256"), QStringLiteral("直方图统计区间数量")},
        {QStringLiteral("ignoreNoData"), QStringLiteral("忽略 NoData"), QStringLiteral("true"), QStringLiteral("是否跳过无效像元")}
    };
}

ProcessingResult HistogramAlgorithm::execute(const RasterLayer& input, const ProcessingContext& context) const {
    Q_UNUSED(input)
    Q_UNUSED(context)
    return {{}, QStringLiteral("TODO: 按指定波段统计直方图，并把结果放入“处理结果/直方图”分组。")};
}

QString HistogramEqualizationAlgorithm::name() const {
    return QStringLiteral("直方图均衡化");
}

QString HistogramEqualizationAlgorithm::category() const {
    return QStringLiteral("影像增强");
}

std::vector<AlgorithmParameter> HistogramEqualizationAlgorithm::parameterSchema() const {
    return {
        {QStringLiteral("band"), QStringLiteral("处理波段"), QStringLiteral("selected"), QStringLiteral("可处理当前波段或当前 RGB 显示影像")},
        {QStringLiteral("clipLimit"), QStringLiteral("CLAHE 限幅"), QStringLiteral("2.0"), QStringLiteral("进阶可实现 CLAHE")}
    };
}

ProcessingResult HistogramEqualizationAlgorithm::execute(const RasterLayer& input, const ProcessingContext& context) const {
    Q_UNUSED(input)
    Q_UNUSED(context)
    return {{}, QStringLiteral("TODO: 实现单波段或 RGB 显示图的直方图均衡化。")};
}

QString FeatureExtractionAlgorithm::name() const {
    return QStringLiteral("ORB/SIFT 特征提取");
}

QString FeatureExtractionAlgorithm::category() const {
    return QStringLiteral("摄影测量");
}

std::vector<AlgorithmParameter> FeatureExtractionAlgorithm::parameterSchema() const {
    return {
        {QStringLiteral("method"), QStringLiteral("方法"), QStringLiteral("ORB"), QStringLiteral("可选 ORB/SIFT/AKAZE")},
        {QStringLiteral("maxFeatures"), QStringLiteral("最大特征数"), QStringLiteral("2000"), QStringLiteral("特征点数量上限")}
    };
}

ProcessingResult FeatureExtractionAlgorithm::execute(const RasterLayer& input, const ProcessingContext& context) const {
    Q_UNUSED(input)
    Q_UNUSED(context)
    return {{}, QStringLiteral("TODO: 使用 OpenCV features2d 提取特征点并绘制结果。")};
}

std::shared_ptr<DemLayer> DemReconstructionPipeline::reconstruct(const Inputs& inputs) const {
    Q_UNUSED(inputs)
    throw std::runtime_error("TODO: use feature matching, stereo rectification and disparity/depth conversion to reconstruct DEM");
}

ProcessingResult OrthorectificationPipeline::rectify(const RasterLayer& image, const DemLayer& dem) const {
    Q_UNUSED(image)
    Q_UNUSED(dem)
    return {{}, QStringLiteral("TODO: 根据影像地理变换、相机模型和 DEM 重采样生成正射影像。")};
}

} // namespace rs
