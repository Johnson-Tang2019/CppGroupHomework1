/**
 * @file Algorithms.cpp
 * @brief 遥感处理算法的骨架实现
 *
 * 本文件定义了各类算法的参数与执行接口，当前为 TODO 骨架，
 * 学生需要填充实际的算法逻辑（直方图统计、均衡化、特征提取、
 * DEM 重建、正射校正）。
 */

#include "rs/Algorithms.h"


namespace rs {

// ============================================================================
// 灰度直方图算法
// ============================================================================

QString HistogramAlgorithm::name() const {
    // 算法显示名称，会出现在 UI 菜单和日志中
    return QStringLiteral("灰度直方图");
}

QString HistogramAlgorithm::category() const {
    // 算法所属分类，用于菜单分组
    return QStringLiteral("影像统计");
}

std::vector<AlgorithmParameter> HistogramAlgorithm::parameterSchema() const {
    // 参数定义列表：{参数名, 显示标签, 默认值, 说明}
    return {
        {QStringLiteral("bins"),         // 内部参数名
         QStringLiteral("分箱数"),        // 界面上显示的标签
         QStringLiteral("256"),           // 默认值
         QStringLiteral("直方图统计区间数量")},  // 工具提示说明

        {QStringLiteral("ignoreNoData"),
         QStringLiteral("忽略 NoData"),
         QStringLiteral("true"),
         QStringLiteral("是否跳过无效像元")}
    };
}

ProcessingResult HistogramAlgorithm::execute(const RasterLayer& input,
                                             const ProcessingContext& context) const {
    // TODO: 按指定波段统计直方图，并把结果放入"处理结果/直方图"分组
    Q_UNUSED(input)
    Q_UNUSED(context)
    return {{}, QStringLiteral("TODO: 按指定波段统计直方图，并把结果放入“处理结果/直方图”分组。")};
}


// ============================================================================
// 直方图均衡化算法
// ============================================================================

QString HistogramEqualizationAlgorithm::name() const {
    return QStringLiteral("直方图均衡化");
}

QString HistogramEqualizationAlgorithm::category() const {
    return QStringLiteral("影像增强");
}

std::vector<AlgorithmParameter> HistogramEqualizationAlgorithm::parameterSchema() const {
    return {
        {QStringLiteral("band"),
         QStringLiteral("处理波段"),
         QStringLiteral("selected"),
         QStringLiteral("可处理当前波段或当前 RGB 显示影像")},

        {QStringLiteral("clipLimit"),
         QStringLiteral("CLAHE 限幅"),
         QStringLiteral("2.0"),
         QStringLiteral("进阶可实现 CLAHE（自适应直方图均衡化）")}
    };
}

ProcessingResult HistogramEqualizationAlgorithm::execute(const RasterLayer& input,
                                                         const ProcessingContext& context) const {
    // TODO: 实现单波段或 RGB 显示图的直方图均衡化
    Q_UNUSED(input)
    Q_UNUSED(context)
    return {{}, QStringLiteral("TODO: 实现单波段或 RGB 显示图的直方图均衡化。")};
}


// ============================================================================
// ORB/SIFT 特征提取算法
// ============================================================================

QString FeatureExtractionAlgorithm::name() const {
    return QStringLiteral("ORB/SIFT 特征提取");
}

QString FeatureExtractionAlgorithm::category() const {
    return QStringLiteral("摄影测量");
}

std::vector<AlgorithmParameter> FeatureExtractionAlgorithm::parameterSchema() const {
    return {
        {QStringLiteral("method"),
         QStringLiteral("方法"),
         QStringLiteral("ORB"),
         QStringLiteral("可选 ORB / SIFT / AKAZE")},

        {QStringLiteral("maxFeatures"),
         QStringLiteral("最大特征数"),
         QStringLiteral("2000"),
         QStringLiteral("特征点数量上限")}
    };
}

ProcessingResult FeatureExtractionAlgorithm::execute(const RasterLayer& input,
                                                     const ProcessingContext& context) const {
    // TODO: 使用 OpenCV features2d 提取特征点并绘制结果
    Q_UNUSED(input)
    Q_UNUSED(context)
    return {{}, QStringLiteral("TODO: 使用 OpenCV features2d 提取特征点并绘制结果。")};
}


// ============================================================================
// DEM 重建流程
// ============================================================================

std::shared_ptr<DemLayer> DemReconstructionPipeline::reconstruct(const Inputs& inputs) const {
    // TODO: use feature matching, stereo rectification and disparity/depth
    //       conversion to reconstruct DEM
    Q_UNUSED(inputs)
    throw std::runtime_error(
        "TODO: use feature matching, stereo rectification and disparity/depth "
        "conversion to reconstruct DEM");
}


// ============================================================================
// 正射影像校正流程
// ============================================================================
ProcessingResult OrthorectificationPipeline::rectify(const RasterLayer& image,
                                                     const DemLayer& dem) const {
    // TODO: 根据影像地理变换、相机模型和 DEM 重采样生成正射影像
    Q_UNUSED(image)
    Q_UNUSED(dem)
    return {{}, QStringLiteral("TODO: 根据影像地理变换、相机模型和 DEM 重采样生成正射影像。")};
}

} // namespace rs

