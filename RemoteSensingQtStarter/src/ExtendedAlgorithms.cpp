#include "rs/ExtendedAlgorithms.h"

#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

#ifdef RS_WITH_OPENCV
#include <opencv2/calib3d.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/ml.hpp>
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

cv::Mat bandToMat(const RasterBand &band) {
    if (!band.hasSamples())
        return {};
    cv::Mat mat(band.height, band.width, CV_32F);
    std::memcpy(mat.data, band.samples.constData(),
                static_cast<size_t>(band.samples.size()) * sizeof(float));
    return mat;
}

cv::Mat bandToGray8U(const RasterBand &band) {
    cv::Mat f = bandToMat(band);
    if (f.empty())
        return {};
    double minV = band.minValue, maxV = band.maxValue;
    if (maxV - minV < 1e-10)
        maxV = minV + 1.0;
    cv::Mat out;
    f.convertTo(out, CV_8U, 255.0 / (maxV - minV), -minV * 255.0 / (maxV - minV));
    return out;
}

int resolveBandIndex(const RasterLayer &input, const ProcessingContext &ctx, int fallback = 0) {
    int idx = ctx.bandIndex;
    if (idx < 0 || idx >= input.bandCount())
        idx = fallback;
    return idx;
}

std::pair<float, float> percentileRange(const RasterBand &band, float loPct, float hiPct) {
    std::vector<float> vals;
    vals.reserve(band.samples.size());
    for (float v : band.samples) {
        if (band.hasNoDataValue && std::abs(v - band.noDataValue) < 1e-6f)
            continue;
        vals.push_back(v);
    }
    if (vals.empty())
        return {band.minValue, band.maxValue};
    std::sort(vals.begin(), vals.end());
    const int iLo = std::clamp(static_cast<int>(vals.size() * loPct / 100.0f), 0,
                               static_cast<int>(vals.size()) - 1);
    const int iHi = std::clamp(static_cast<int>(vals.size() * hiPct / 100.0f), iLo,
                               static_cast<int>(vals.size()) - 1);
    return {vals[static_cast<size_t>(iLo)], vals[static_cast<size_t>(iHi)]};
}

QImage matToQImage(const cv::Mat &mat) {
    if (mat.type() == CV_8UC1) {
        QImage img(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Grayscale8);
        return img.copy();
    }
    if (mat.type() == CV_8UC3) {
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
        return img.copy();
    }
    return {};
}

cv::Mat rasterToGray8U(const RasterLayer &input, int bandIdx) {
    if (input.bandCount() == 0 && !input.currentDisplayImage().isNull()) {
        const QImage gray = input.currentDisplayImage().convertToFormat(QImage::Format_Grayscale8);
        if (gray.isNull())
            return {};
        cv::Mat mat(gray.height(), gray.width(), CV_8UC1, const_cast<uchar *>(gray.constBits()),
                    static_cast<size_t>(gray.bytesPerLine()));
        return mat.clone();
    }
    if (input.bandCount() >= 3 && bandIdx < 0) {
        cv::Mat b = bandToGray8U(input.band(0));
        cv::Mat g = bandToGray8U(input.band(1));
        cv::Mat r = bandToGray8U(input.band(2));
        if (!b.empty() && !g.empty() && !r.empty()) {
            std::vector<cv::Mat> ch = {b, g, r};
            cv::Mat color, gray;
            cv::merge(ch, color);
            cv::cvtColor(color, gray, cv::COLOR_BGR2GRAY);
            return gray;
        }
    }
    const int idx = resolveBandIndex(input, ProcessingContext{}, bandIdx >= 0 ? bandIdx : 0);
    return bandToGray8U(input.band(idx));
}

cv::Mat labelsToColorMap(const cv::Mat &labels, int k) {
    cv::Mat color(labels.size(), CV_8UC3);
    static const cv::Scalar palette[] = {
        {0, 0, 255},   {0, 255, 0},   {255, 0, 0},   {0, 255, 255}, {255, 0, 255},
        {255, 255, 0}, {128, 0, 255}, {255, 128, 0}, {0, 128, 255}, {128, 255, 0}};
    for (int y = 0; y < labels.rows; ++y) {
        for (int x = 0; x < labels.cols; ++x) {
            int lbl = labels.at<int>(y, x);
            color.at<cv::Vec3b>(y, x) =
                cv::Vec3b(static_cast<uchar>(palette[lbl % k][0]),
                          static_cast<uchar>(palette[lbl % k][1]),
                          static_cast<uchar>(palette[lbl % k][2]));
        }
    }
    return color;
}

#endif

} // namespace

#ifdef RS_WITH_OPENCV

QString StretchEnhancementAlgorithm::name() const { return QStringLiteral("拉伸增强"); }
QString StretchEnhancementAlgorithm::category() const { return QStringLiteral("影像增强"); }
std::vector<AlgorithmParameter> StretchEnhancementAlgorithm::parameterSchema() const {
    return {{QStringLiteral("mode"), QStringLiteral("模式"), QStringLiteral("percent"),
             QStringLiteral("linear 或 percent")},
            {QStringLiteral("lowPercent"), QStringLiteral("低百分位"), QStringLiteral("2"),
             QStringLiteral("百分比拉伸下限")},
            {QStringLiteral("highPercent"), QStringLiteral("高百分位"), QStringLiteral("98"),
             QStringLiteral("百分比拉伸上限")}};
}

ProcessingResult StretchEnhancementAlgorithm::execute(const RasterLayer &input,
                                                      const ProcessingContext &context) const {
    const int bandIdx = resolveBandIndex(input, context, 0);
    const RasterBand &band = input.band(bandIdx);
    if (!band.hasSamples())
        return {{}, QStringLiteral("拉伸失败：波段无像素数据")};

    const QString mode = context.parameters.value(QStringLiteral("mode"), QStringLiteral("percent")).toString();
    float minV = band.minValue, maxV = band.maxValue;
    if (mode == QStringLiteral("percent")) {
        const float lo = context.parameters.value(QStringLiteral("lowPercent"), 2.0).toFloat();
        const float hi = context.parameters.value(QStringLiteral("highPercent"), 98.0).toFloat();
        const auto range = percentileRange(band, lo, hi);
        minV = range.first;
        maxV = range.second;
    }

    cv::Mat gray = bandToGray8U(band);
    if (gray.empty())
        return {{}, QStringLiteral("拉伸失败")};
    cv::Mat stretched;
    double range = maxV - minV;
    if (range < 1e-10)
        range = 1.0;
    bandToMat(band).convertTo(stretched, CV_8U, 255.0 / range, -minV * 255.0 / range);

    cv::Mat display;
    cv::hconcat(gray, stretched, display);
    return {matToQImage(stretched),
            QStringLiteral("%1 拉伸完成（Band %2，范围 %3~%4）")
                .arg(mode == QStringLiteral("percent") ? QStringLiteral("百分比") : QStringLiteral("线性"))
                .arg(bandIdx + 1)
                .arg(minV, 0, 'f', 2)
                .arg(maxV, 0, 'f', 2)};
}

QString ClaheEnhancementAlgorithm::name() const { return QStringLiteral("CLAHE 增强"); }
QString ClaheEnhancementAlgorithm::category() const { return QStringLiteral("影像增强"); }
std::vector<AlgorithmParameter> ClaheEnhancementAlgorithm::parameterSchema() const {
    return {{QStringLiteral("clipLimit"), QStringLiteral("对比度限制"), QStringLiteral("2.0"), {}},
            {QStringLiteral("tileSize"), QStringLiteral("分块大小"), QStringLiteral("8"), {}}};
}

ProcessingResult ClaheEnhancementAlgorithm::execute(const RasterLayer &input,
                                                    const ProcessingContext &context) const {
    const int bandIdx = resolveBandIndex(input, context, 0);
    cv::Mat gray = rasterToGray8U(input, bandIdx);
    if (gray.empty())
        return {{}, QStringLiteral("CLAHE 失败：无可用影像")};

    const double clip = context.parameters.value(QStringLiteral("clipLimit"), 2.0).toDouble();
    const int tile = context.parameters.value(QStringLiteral("tileSize"), 8).toInt();
    auto clahe = cv::createCLAHE(clip, cv::Size(tile, tile));
    cv::Mat result;
    clahe->apply(gray, result);
    return {matToQImage(result), QStringLiteral("CLAHE 增强完成（clip=%1, tile=%2）").arg(clip).arg(tile)};
}

QString DenoiseFilterAlgorithm::name() const { return QStringLiteral("滤波降噪"); }
QString DenoiseFilterAlgorithm::category() const { return QStringLiteral("影像增强"); }
std::vector<AlgorithmParameter> DenoiseFilterAlgorithm::parameterSchema() const {
    return {{QStringLiteral("filterType"), QStringLiteral("滤波类型"), QStringLiteral("gaussian"),
             QStringLiteral("gaussian / median / bilateral")}};
}

ProcessingResult DenoiseFilterAlgorithm::execute(const RasterLayer &input,
                                                 const ProcessingContext &context) const {
    cv::Mat gray = rasterToGray8U(input, resolveBandIndex(input, context, 0));
    if (gray.empty())
        return {{}, QStringLiteral("滤波失败")};

    const QString type = context.parameters.value(QStringLiteral("filterType"), QStringLiteral("gaussian")).toString();
    cv::Mat filtered;
    if (type == QStringLiteral("median")) {
        cv::medianBlur(gray, filtered, 5);
    } else if (type == QStringLiteral("bilateral")) {
        cv::bilateralFilter(gray, filtered, 9, 75, 75);
    } else {
        cv::GaussianBlur(gray, filtered, cv::Size(5, 5), 1.2);
    }
    return {matToQImage(filtered), QStringLiteral("%1 滤波完成").arg(type)};
}

QString SharpenEnhancementAlgorithm::name() const { return QStringLiteral("锐化增强"); }
QString SharpenEnhancementAlgorithm::category() const { return QStringLiteral("影像增强"); }
std::vector<AlgorithmParameter> SharpenEnhancementAlgorithm::parameterSchema() const {
    return {{QStringLiteral("method"), QStringLiteral("方法"), QStringLiteral("unsharp"),
             QStringLiteral("unsharp 或 laplacian")}};
}

ProcessingResult SharpenEnhancementAlgorithm::execute(const RasterLayer &input,
                                                      const ProcessingContext &context) const {
    cv::Mat gray = rasterToGray8U(input, resolveBandIndex(input, context, 0));
    if (gray.empty())
        return {{}, QStringLiteral("锐化失败")};

    const QString method = context.parameters.value(QStringLiteral("method"), QStringLiteral("unsharp")).toString();
    cv::Mat sharp;
    if (method == QStringLiteral("laplacian")) {
        cv::Mat lap;
        cv::Laplacian(gray, lap, CV_16S, 3);
        cv::convertScaleAbs(lap, lap);
        cv::addWeighted(gray, 1.0, lap, 0.8, 0, sharp);
    } else {
        cv::Mat blur;
        cv::GaussianBlur(gray, blur, cv::Size(0, 0), 3);
        cv::addWeighted(gray, 1.5, blur, -0.5, 0, sharp);
    }
    return {matToQImage(sharp),
            QStringLiteral("%1 锐化完成").arg(method == QStringLiteral("laplacian")
                                                   ? QStringLiteral("拉普拉斯")
                                                   : QStringLiteral("Unsharp"))};
}

QString CannyEdgeAlgorithm::name() const { return QStringLiteral("Canny 边缘检测"); }
QString CannyEdgeAlgorithm::category() const { return QStringLiteral("特征与检测"); }
std::vector<AlgorithmParameter> CannyEdgeAlgorithm::parameterSchema() const {
    return {{QStringLiteral("threshold1"), QStringLiteral("低阈值"), QStringLiteral("50"), {}},
            {QStringLiteral("threshold2"), QStringLiteral("高阈值"), QStringLiteral("150"), {}}};
}

ProcessingResult CannyEdgeAlgorithm::execute(const RasterLayer &input,
                                             const ProcessingContext &context) const {
    cv::Mat gray = rasterToGray8U(input, resolveBandIndex(input, context, 0));
    if (gray.empty())
        return {{}, QStringLiteral("Canny 失败")};
    const double t1 = context.parameters.value(QStringLiteral("threshold1"), 50).toDouble();
    const double t2 = context.parameters.value(QStringLiteral("threshold2"), 150).toDouble();
    cv::Mat edges;
    cv::Canny(gray, edges, t1, t2);
    return {matToQImage(edges), QStringLiteral("Canny 边缘检测完成（%1/%2）").arg(t1).arg(t2)};
}

QString KMeansClassificationAlgorithm::name() const { return QStringLiteral("K-Means 分类"); }
QString KMeansClassificationAlgorithm::category() const { return QStringLiteral("分类与检测"); }
std::vector<AlgorithmParameter> KMeansClassificationAlgorithm::parameterSchema() const {
    return {{QStringLiteral("k"), QStringLiteral("类别数"), QStringLiteral("3"), {}}};
}

ProcessingResult KMeansClassificationAlgorithm::execute(const RasterLayer &input,
                                                        const ProcessingContext &context) const {
    const int k = context.parameters.value(QStringLiteral("k"), 3).toInt();
    if (k < 2 || input.bandCount() < 1)
        return {{}, QStringLiteral("K-Means 失败：类别数或波段无效")};

    const int bands = std::min(3, input.bandCount());
    const int w = input.band(0).width;
    const int h = input.band(0).height;
    cv::Mat samples(w * h, bands, CV_32F);
    for (int b = 0; b < bands; ++b) {
        cv::Mat f = bandToMat(input.band(b));
        if (f.empty())
            return {{}, QStringLiteral("K-Means 失败：波段数据缺失")};
        for (int i = 0; i < w * h; ++i)
            samples.at<float>(i, b) = f.at<float>(i / w, i % w);
    }

    cv::Mat labels, centers;
    cv::kmeans(samples, k, labels,
               cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::COUNT, 20, 0.5), 3,
               cv::KMEANS_PP_CENTERS, centers);

    cv::Mat labelImg(h, w, CV_32S);
    for (int i = 0; i < w * h; ++i)
        labelImg.at<int>(i / w, i % w) = labels.at<int>(i);
    cv::Mat color = labelsToColorMap(labelImg, k);
    return {matToQImage(color), QStringLiteral("K-Means 分类完成（K=%1）").arg(k)};
}

QString SvmClassificationAlgorithm::name() const { return QStringLiteral("SVM 地物分类"); }
QString SvmClassificationAlgorithm::category() const { return QStringLiteral("分类与检测"); }
std::vector<AlgorithmParameter> SvmClassificationAlgorithm::parameterSchema() const {
    return {{QStringLiteral("classes"), QStringLiteral("类别数"), QStringLiteral("3"), {}},
            {QStringLiteral("trainSamples"), QStringLiteral("训练样本数"), QStringLiteral("500"), {}}};
}

ProcessingResult SvmClassificationAlgorithm::execute(const RasterLayer &input,
                                                     const ProcessingContext &context) const {
    const int classes = context.parameters.value(QStringLiteral("classes"), 3).toInt();
    const int trainN = context.parameters.value(QStringLiteral("trainSamples"), 500).toInt();
    const int bands = std::min(3, input.bandCount());
    if (bands < 1 || classes < 2)
        return {{}, QStringLiteral("SVM 失败：参数无效")};

    const int w = input.band(0).width;
    const int h = input.band(0).height;
    std::vector<cv::Mat> feat(bands);
    for (int b = 0; b < bands; ++b)
        feat[b] = bandToMat(input.band(b));

    cv::Mat trainData(trainN, bands, CV_32F);
    cv::Mat trainLabels(trainN, 1, CV_32S);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> rx(0, w - 1), ry(0, h - 1), rc(0, classes - 1);
    for (int i = 0; i < trainN; ++i) {
        const int x = rx(rng), y = ry(rng);
        for (int b = 0; b < bands; ++b)
            trainData.at<float>(i, b) = feat[b].at<float>(y, x);
        trainLabels.at<int>(i, 0) = rc(rng);
    }

    auto svm = cv::ml::SVM::create();
    svm->setType(cv::ml::SVM::C_SVC);
    svm->setKernel(cv::ml::SVM::RBF);
    svm->train(trainData, cv::ml::ROW_SAMPLE, trainLabels);

    cv::Mat samples(w * h, bands, CV_32F);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int i = y * w + x;
            for (int b = 0; b < bands; ++b)
                samples.at<float>(i, b) = feat[b].at<float>(y, x);
        }
    }
    cv::Mat pred;
    svm->predict(samples, pred);
    cv::Mat labelImg(h, w, CV_32S);
    for (int i = 0; i < w * h; ++i)
        labelImg.at<int>(i / w, i % w) = static_cast<int>(pred.at<float>(i));
    cv::Mat color = labelsToColorMap(labelImg, classes);
    return {matToQImage(color),
            QStringLiteral("SVM 分类完成（%1 类，%2 训练样本）").arg(classes).arg(trainN)};
}

QString ContourDetectionAlgorithm::name() const { return QStringLiteral("轮廓目标检测"); }
QString ContourDetectionAlgorithm::category() const { return QStringLiteral("分类与检测"); }
std::vector<AlgorithmParameter> ContourDetectionAlgorithm::parameterSchema() const { return {}; }

ProcessingResult ContourDetectionAlgorithm::execute(const RasterLayer &input,
                                                    const ProcessingContext &context) const {
    cv::Mat gray = rasterToGray8U(input, resolveBandIndex(input, context, 0));
    if (gray.empty())
        return {{}, QStringLiteral("轮廓检测失败")};
    cv::Mat blur, edges;
    cv::GaussianBlur(gray, blur, cv::Size(5, 5), 0);
    cv::Canny(blur, edges, 50, 150);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    cv::Mat display;
    cv::cvtColor(gray, display, cv::COLOR_GRAY2BGR);
    int idx = 1;
    for (const auto &c : contours) {
        if (cv::contourArea(c) < 100)
            continue;
        cv::Rect box = cv::boundingRect(c);
        cv::rectangle(display, box, cv::Scalar(0, 255, 0), 2);
        cv::putText(display, std::to_string(idx), box.tl(), cv::FONT_HERSHEY_SIMPLEX, 0.5,
                    cv::Scalar(0, 0, 255), 1);
        ++idx;
    }
    return {matToQImage(display), QStringLiteral("轮廓检测完成：%1 个目标").arg(idx - 1)};
}

QString ConnectedComponentsAlgorithm::name() const { return QStringLiteral("连通域检测"); }
QString ConnectedComponentsAlgorithm::category() const { return QStringLiteral("分类与检测"); }
std::vector<AlgorithmParameter> ConnectedComponentsAlgorithm::parameterSchema() const {
    return {{QStringLiteral("minArea"), QStringLiteral("最小面积"), QStringLiteral("100"), {}}};
}

ProcessingResult ConnectedComponentsAlgorithm::execute(const RasterLayer &input,
                                                       const ProcessingContext &context) const {
    cv::Mat gray = rasterToGray8U(input, resolveBandIndex(input, context, 0));
    if (gray.empty())
        return {{}, QStringLiteral("连通域检测失败")};
    cv::Mat binary;
    cv::threshold(gray, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    cv::Mat labels, stats, centroids;
    const int n = cv::connectedComponentsWithStats(binary, labels, stats, centroids);
    const int minArea = context.parameters.value(QStringLiteral("minArea"), 100).toInt();
    cv::Mat color = cv::Mat::zeros(gray.size(), CV_8UC3);
    int kept = 0;
    for (int i = 1; i < n; ++i) {
        if (stats.at<int>(i, cv::CC_STAT_AREA) < minArea)
            continue;
        color.setTo(cv::Scalar((i * 37) % 255, (i * 73) % 255, (i * 113) % 255), labels == i);
        ++kept;
    }
    return {matToQImage(color), QStringLiteral("连通域检测完成：%1 个区域").arg(kept)};
}

QString ConfusionMatrixAlgorithm::name() const { return QStringLiteral("混淆矩阵精度评价"); }
QString ConfusionMatrixAlgorithm::category() const { return QStringLiteral("分类与检测"); }
std::vector<AlgorithmParameter> ConfusionMatrixAlgorithm::parameterSchema() const { return {}; }

ProcessingResult ConfusionMatrixAlgorithm::execute(const RasterLayer &input,
                                                   const ProcessingContext &context) const {
    const auto *ref = context.auxiliaryRaster;
    if (!ref || ref->bandCount() < 1 || input.bandCount() < 1)
        return {{}, QStringLiteral("混淆矩阵失败：请同时选中预测图层与参考图层")};

    const int w = std::min(input.band(0).width, ref->band(0).width);
    const int h = std::min(input.band(0).height, ref->band(0).height);
    const int k = 4;
    std::vector<std::vector<int>> matrix(k, std::vector<int>(k, 0));
    int total = 0, agree = 0;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int i = y * input.band(0).width + x;
            const int j = y * ref->band(0).width + x;
            const int pred =
                static_cast<int>(input.band(0).samples[i]) % k;
            const int truth =
                static_cast<int>(ref->band(0).samples[j]) % k;
            matrix[truth][pred]++;
            ++total;
            if (pred == truth)
                ++agree;
        }
    }

    double oa = total > 0 ? static_cast<double>(agree) / total : 0.0;
    std::vector<int> rowSum(k, 0), colSum(k, 0);
    for (int i = 0; i < k; ++i) {
        for (int j = 0; j < k; ++j) {
            rowSum[i] += matrix[i][j];
            colSum[j] += matrix[i][j];
        }
    }
    double pe = 0;
    for (int i = 0; i < k; ++i)
        pe += static_cast<double>(rowSum[i]) * colSum[i];
    pe = total > 0 ? pe / (static_cast<double>(total) * total) : 0;
    const double kappa = (oa - pe) / (1.0 - pe + 1e-10);

    const QString csvPath = context.parameters.value(QStringLiteral("csvPath")).toString();
    if (!csvPath.isEmpty()) {
        QFile file(csvPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "truth\\pred";
            for (int j = 0; j < k; ++j)
                out << "," << j;
            out << "\n";
            for (int i = 0; i < k; ++i) {
                out << i;
                for (int j = 0; j < k; ++j)
                    out << "," << matrix[i][j];
                out << "\n";
            }
            out << "OA," << oa << "\nKappa," << kappa << "\n";
        }
    }

    cv::Mat table(320, 480, CV_8UC3, cv::Scalar(40, 40, 40));
    cv::putText(table, "Confusion Matrix", cv::Point(20, 30), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                cv::Scalar(255, 255, 255), 2);
    cv::putText(table, ("OA: " + std::to_string(oa)).c_str(), cv::Point(20, 70),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);
    cv::putText(table, ("Kappa: " + std::to_string(kappa)).c_str(), cv::Point(20, 110),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 2);

    return {matToQImage(table),
            QStringLiteral("混淆矩阵：OA=%1，Kappa=%2").arg(oa, 0, 'f', 4).arg(kappa, 0, 'f', 4)};
}

#else

QString StretchEnhancementAlgorithm::name() const { return QStringLiteral("拉伸增强"); }
QString StretchEnhancementAlgorithm::category() const { return QStringLiteral("影像增强"); }
std::vector<AlgorithmParameter> StretchEnhancementAlgorithm::parameterSchema() const { return {}; }
ProcessingResult StretchEnhancementAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("拉伸增强"));
}

QString ClaheEnhancementAlgorithm::name() const { return QStringLiteral("CLAHE 增强"); }
QString ClaheEnhancementAlgorithm::category() const { return QStringLiteral("影像增强"); }
std::vector<AlgorithmParameter> ClaheEnhancementAlgorithm::parameterSchema() const { return {}; }
ProcessingResult ClaheEnhancementAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("CLAHE 增强"));
}

QString DenoiseFilterAlgorithm::name() const { return QStringLiteral("滤波降噪"); }
QString DenoiseFilterAlgorithm::category() const { return QStringLiteral("影像增强"); }
std::vector<AlgorithmParameter> DenoiseFilterAlgorithm::parameterSchema() const { return {}; }
ProcessingResult DenoiseFilterAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("滤波降噪"));
}

QString SharpenEnhancementAlgorithm::name() const { return QStringLiteral("锐化增强"); }
QString SharpenEnhancementAlgorithm::category() const { return QStringLiteral("影像增强"); }
std::vector<AlgorithmParameter> SharpenEnhancementAlgorithm::parameterSchema() const { return {}; }
ProcessingResult SharpenEnhancementAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("锐化增强"));
}

QString CannyEdgeAlgorithm::name() const { return QStringLiteral("Canny 边缘检测"); }
QString CannyEdgeAlgorithm::category() const { return QStringLiteral("特征与检测"); }
std::vector<AlgorithmParameter> CannyEdgeAlgorithm::parameterSchema() const { return {}; }
ProcessingResult CannyEdgeAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("Canny 边缘检测"));
}

QString KMeansClassificationAlgorithm::name() const { return QStringLiteral("K-Means 分类"); }
QString KMeansClassificationAlgorithm::category() const { return QStringLiteral("分类与检测"); }
std::vector<AlgorithmParameter> KMeansClassificationAlgorithm::parameterSchema() const { return {}; }
ProcessingResult KMeansClassificationAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("K-Means 分类"));
}

QString SvmClassificationAlgorithm::name() const { return QStringLiteral("SVM 地物分类"); }
QString SvmClassificationAlgorithm::category() const { return QStringLiteral("分类与检测"); }
std::vector<AlgorithmParameter> SvmClassificationAlgorithm::parameterSchema() const { return {}; }
ProcessingResult SvmClassificationAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("SVM 地物分类"));
}

QString ContourDetectionAlgorithm::name() const { return QStringLiteral("轮廓目标检测"); }
QString ContourDetectionAlgorithm::category() const { return QStringLiteral("分类与检测"); }
std::vector<AlgorithmParameter> ContourDetectionAlgorithm::parameterSchema() const { return {}; }
ProcessingResult ContourDetectionAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("轮廓目标检测"));
}

QString ConnectedComponentsAlgorithm::name() const { return QStringLiteral("连通域检测"); }
QString ConnectedComponentsAlgorithm::category() const { return QStringLiteral("分类与检测"); }
std::vector<AlgorithmParameter> ConnectedComponentsAlgorithm::parameterSchema() const { return {}; }
ProcessingResult ConnectedComponentsAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("连通域检测"));
}

QString ConfusionMatrixAlgorithm::name() const { return QStringLiteral("混淆矩阵精度评价"); }
QString ConfusionMatrixAlgorithm::category() const { return QStringLiteral("分类与检测"); }
std::vector<AlgorithmParameter> ConfusionMatrixAlgorithm::parameterSchema() const { return {}; }
ProcessingResult ConfusionMatrixAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("混淆矩阵精度评价"));
}

#endif

} // namespace rs
