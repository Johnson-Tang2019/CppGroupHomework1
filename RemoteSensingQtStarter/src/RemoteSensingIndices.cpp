#include "rs/RemoteSensingIndices.h"

#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#ifdef RS_WITH_OPENCV
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#endif

namespace rs {
namespace {

#ifndef RS_WITH_OPENCV
ProcessingResult openCvUnavailable(const QString &name) {
    return {{},
            QStringLiteral("当前构建未启用 OpenCV（%1）。请使用 build_msys2_ucrt.ps1 构建。").arg(name)};
}
#endif

#ifdef RS_WITH_OPENCV

float bandValue(const RasterBand &band, int x, int y, bool &valid) {
    valid = false;
    if (!band.hasSamples() || x < 0 || y < 0 || x >= band.width || y >= band.height)
        return 0;
    const float v = band.samples[y * band.width + x];
    if (band.hasNoDataValue && std::abs(v - band.noDataValue) < 1e-6f)
        return 0;
    valid = true;
    return v;
}

QVector<float> computeIndexGrid(const RasterLayer &input, const QString &indexName, int bA, int bB) {
    const int w = input.band(0).width;
    const int h = input.band(0).height;
    QVector<float> grid(w * h, 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool v1 = false, v2 = false;
            const float a = bandValue(input.band(bA), x, y, v1);
            const float b = bandValue(input.band(bB), x, y, v2);
            float value = 0.0f;
            if (indexName == QStringLiteral("NDVI")) {
                if (v1 && v2 && (a + b) != 0.0f)
                    value = (a - b) / (a + b);
            } else if (indexName == QStringLiteral("NDWI")) {
                if (v1 && v2 && (a + b) != 0.0f)
                    value = (a - b) / (a + b);
            } else if (indexName == QStringLiteral("NDBI")) {
                if (v1 && v2 && (a + b) != 0.0f)
                    value = (a - b) / (a + b);
            }
            grid[y * w + x] = value;
        }
    }
    return grid;
}

QImage indexToColorImage(const QVector<float> &grid, int w, int h, float threshold,
                         bool applyThreshold) {
    float minV = 1e30f, maxV = -1e30f;
    for (float v : grid) {
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
    }
    if (maxV - minV < 1e-10f)
        maxV = minV + 1.0f;

    cv::Mat gray(h, w, CV_8U);
    cv::Mat mask(h, w, CV_8U, cv::Scalar(0));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float v = grid[y * w + x];
            const int g = static_cast<int>((v - minV) / (maxV - minV) * 255.0f);
            gray.at<uchar>(y, x) = static_cast<uchar>(std::clamp(g, 0, 255));
            if (applyThreshold && v >= threshold)
                mask.at<uchar>(y, x) = 255;
        }
    }
    cv::Mat color;
    cv::applyColorMap(gray, color, cv::COLORMAP_JET);
    if (applyThreshold) {
        cv::Mat overlay = color.clone();
        overlay.setTo(cv::Scalar(255, 255, 255), mask);
        cv::addWeighted(color, 0.7, overlay, 0.3, 0, color);
    }
    cv::Mat rgb;
    cv::cvtColor(color, rgb, cv::COLOR_BGR2RGB);
    QImage img(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
    return img.copy();
}

#endif

} // namespace

#ifdef RS_WITH_OPENCV

QString RemoteSensingIndexAlgorithm::name() const { return QStringLiteral("遥感指数"); }
QString RemoteSensingIndexAlgorithm::category() const { return QStringLiteral("遥感指数"); }
std::vector<AlgorithmParameter> RemoteSensingIndexAlgorithm::parameterSchema() const {
    return {{QStringLiteral("index"), QStringLiteral("指数类型"), QStringLiteral("NDVI"),
             QStringLiteral("NDVI / NDWI / NDBI")}};
}

ProcessingResult RemoteSensingIndexAlgorithm::execute(const RasterLayer &input,
                                                    const ProcessingContext &context) const {
    if (input.bandCount() < 2)
        return {{}, QStringLiteral("指数计算失败：至少需要 2 个波段")};

    const QString index = context.parameters.value(QStringLiteral("index"), QStringLiteral("NDVI")).toString();
    int bRed = context.parameters.value(QStringLiteral("redBand"), 0).toInt();
    int bGreen = context.parameters.value(QStringLiteral("greenBand"), 1).toInt();
    int bNir = context.parameters.value(QStringLiteral("nirBand"), 2).toInt();
    int bSwir = context.parameters.value(QStringLiteral("swirBand"), 2).toInt();
    if (bNir >= input.bandCount())
        bNir = std::min(2, input.bandCount() - 1);
    if (bSwir >= input.bandCount())
        bSwir = std::min(2, input.bandCount() - 1);

    int ba = bNir, bb = bRed;
    if (index == QStringLiteral("NDWI")) {
        ba = bGreen;
        bb = bNir;
    } else if (index == QStringLiteral("NDBI")) {
        ba = bSwir;
        bb = bNir;
    }

    const int w = input.band(0).width;
    const int h = input.band(0).height;
    const QVector<float> grid = computeIndexGrid(input, index, ba, bb);
    const float threshold = context.parameters.value(QStringLiteral("threshold"), 0.2).toFloat();
    const bool useThreshold = context.parameters.value(QStringLiteral("applyThreshold"), false).toBool();

    double sum = 0;
    int count = 0;
    for (float v : grid) {
        sum += v;
        ++count;
    }
    const double mean = count > 0 ? sum / count : 0.0;

    const QString csvPath = context.parameters.value(QStringLiteral("csvPath")).toString();
    if (!csvPath.isEmpty()) {
        QFile file(csvPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "index,mean,pixels\n" << index << "," << mean << "," << count << "\n";
        }
    }

    QImage preview = indexToColorImage(grid, w, h, threshold, useThreshold);
    auto resultLayer = std::make_shared<RasterLayer>(
        input.name() + QStringLiteral("_") + index, QString(),
        QVector<RasterBand>{}, preview);
    resultLayer->setRenderDescription(index + QStringLiteral(" 伪彩色"));

    ProcessingResult result;
    result.image = preview;
    result.rasterResult = resultLayer;
    result.message =
        QStringLiteral("%1 计算完成，均值=%2").arg(index).arg(mean, 0, 'f', 4);
    return result;
}

QString IndexTemporalCompareAlgorithm::name() const { return QStringLiteral("多时相指数对比"); }
QString IndexTemporalCompareAlgorithm::category() const { return QStringLiteral("遥感指数"); }
std::vector<AlgorithmParameter> IndexTemporalCompareAlgorithm::parameterSchema() const { return {}; }

ProcessingResult IndexTemporalCompareAlgorithm::execute(const RasterLayer &input,
                                                        const ProcessingContext &context) const {
    const auto *other = context.auxiliaryRaster;
    if (!other || other->bandCount() < 2)
        return {{}, QStringLiteral("多时相对比失败：请选中两个时相的栅格图层")};

    const QString index = context.parameters.value(QStringLiteral("index"), QStringLiteral("NDVI")).toString();
    RemoteSensingIndexAlgorithm idxAlgo;
    ProcessingContext c1 = context;
    ProcessingContext c2 = context;
    const auto r1 = idxAlgo.execute(input, c1);
    const auto r2 = idxAlgo.execute(*other, c2);
    if (r1.message.contains(QStringLiteral("失败")) || r2.message.contains(QStringLiteral("失败")))
        return {{}, QStringLiteral("多时相对比失败：指数计算错误")};

    const int w = std::min(input.band(0).width, other->band(0).width);
    const int h = std::min(input.band(0).height, other->band(0).height);
    const QVector<float> g1 = computeIndexGrid(input, index, 2, 0);
    const QVector<float> g2 = computeIndexGrid(*other, index, 2, 0);

    cv::Mat diff(h, w, CV_32F);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            diff.at<float>(y, x) = g1[y * input.band(0).width + x] - g2[y * other->band(0).width + x];
        }
    }
    cv::Mat norm;
    cv::normalize(diff, norm, 0, 255, cv::NORM_MINMAX, CV_8U);
    cv::Mat color;
    cv::applyColorMap(norm, color, cv::COLORMAP_COOL);
    cv::Mat rgb;
    cv::cvtColor(color, rgb, cv::COLOR_BGR2RGB);
    QImage img(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
    return {img.copy(), QStringLiteral("多时相 %1 差值对比完成").arg(index)};
}

#else

QString RemoteSensingIndexAlgorithm::name() const { return QStringLiteral("遥感指数"); }
QString RemoteSensingIndexAlgorithm::category() const { return QStringLiteral("遥感指数"); }
std::vector<AlgorithmParameter> RemoteSensingIndexAlgorithm::parameterSchema() const { return {}; }
ProcessingResult RemoteSensingIndexAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("遥感指数"));
}

QString IndexTemporalCompareAlgorithm::name() const { return QStringLiteral("多时相指数对比"); }
QString IndexTemporalCompareAlgorithm::category() const { return QStringLiteral("遥感指数"); }
std::vector<AlgorithmParameter> IndexTemporalCompareAlgorithm::parameterSchema() const { return {}; }
ProcessingResult IndexTemporalCompareAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("多时相指数对比"));
}

#endif

} // namespace rs
