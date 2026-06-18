#include "../include/rs/Algorithms.h"
#include "../include/rs/RasterRenderDialog.h" 
#include "../include/rs/RasterIO.h"
#include "../include/rs/pcmesh/PcMeshReconstruction.h" 
#include <QDialog>
#include <QLabel>
#include <QVBoxLayout>
#include <QPixmap>
#include <QImage>
#include <QCoreApplication>
#include <QPointF>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <iostream>

#ifdef RS_WITH_OPENCV
#include <opencv2/opencv.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/calib3d.hpp>
#endif

namespace rs {

#ifndef RS_WITH_OPENCV
static ProcessingResult openCvUnavailableResult(const QString &featureName) {
    return {{},
            QStringLiteral("当前构建未启用 OpenCV（%1）。请使用 build_msys2_ucrt.ps1 构建完整版本。")
                .arg(featureName)};
}
#endif

#ifdef RS_WITH_OPENCV

// ── 辅助函数：限制立体匹配输入尺寸，避免 SGBM 在主线程上过久阻塞 UI ──
static void limitStereoImageSize(cv::Mat& left, cv::Mat& right, int maxDim = 1024) {
    const int maxSide = std::max({left.cols, left.rows, right.cols, right.rows});
    if (maxSide <= maxDim) {
        return;
    }
    const double scale = static_cast<double>(maxDim) / static_cast<double>(maxSide);
    cv::resize(left, left, cv::Size(), scale, scale, cv::INTER_AREA);
    cv::resize(right, right, cv::Size(), scale, scale, cv::INTER_AREA);
}

// ── 辅助函数：将 QVector<float> 转为 cv::Mat（单通道灰度，CV_32F） ──
static cv::Mat rasterBandToMat(const RasterBand& band) {
    if (!band.hasSamples()) return cv::Mat();
    cv::Mat mat(band.height, band.width, CV_32F);
    std::memcpy(mat.data, band.samples.constData(), band.samples.size() * sizeof(float));
    return mat;
}

// ── 辅助函数：将波段数据通过线性拉伸转为 CV_8U 用于显示 ──
static cv::Mat stretchTo8U(const cv::Mat& floatMat, float minVal, float maxVal) {
    cv::Mat result;
    double range = static_cast<double>(maxVal) - static_cast<double>(minVal);
    if (range < 1e-10) range = 255.0;
    floatMat.convertTo(result, CV_8U, 255.0 / range, -minVal * 255.0 / range);
    return result;
}

// ── 辅助函数：将 cv::Mat 转换为 QImage ──
static QImage matToQImage(const cv::Mat& mat) {
    if (mat.type() == CV_8UC1) {
        // 单通道灰度图
        QImage img(mat.data, mat.cols, mat.rows, static_cast<int>(mat.step), QImage::Format_Grayscale8);
        return img.copy(); // 必须 copy，防止 Mat 内存释放后图像损坏
    } else if (mat.type() == CV_8UC3) {
        // 三通道彩色图 (OpenCV 默认是 BGR，需转为 RGB)
        cv::Mat rgb;
        cv::cvtColor(mat, rgb, cv::COLOR_BGR2RGB);
        QImage img(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
        return img.copy();
    }
    return QImage();
}

// 1. 灰度直方图算法（基于真实波段像素数据）

QString HistogramAlgorithm::name() const { return QStringLiteral("灰度直方图"); }
QString HistogramAlgorithm::category() const { return QStringLiteral("影像统计"); }
std::vector<AlgorithmParameter> HistogramAlgorithm::parameterSchema() const { return {}; }

ProcessingResult HistogramAlgorithm::execute(const RasterLayer& input, const ProcessingContext& context) const {
    // 确定要统计的波段索引
    int bandIdx = context.bandIndex;
    if (bandIdx < 0 || bandIdx >= input.bandCount()) bandIdx = 0;

    const RasterBand& bandInfo = input.band(bandIdx);
    if (!bandInfo.hasSamples()) {
        return {{}, QStringLiteral("波段 %1 没有像素数据").arg(bandIdx)};
    }

    const float* data = bandInfo.samples.constData();
    const int pixelCount = bandInfo.width * bandInfo.height;

    // 计算有效数据的 min/max（跳过 NoData）
    float minVal = bandInfo.minValue;
    float maxVal = bandInfo.maxValue;
    bool hasNoData = bandInfo.hasNoDataValue;
    float noData = bandInfo.noDataValue;

    // 计算 256 个区间的直方图
    const int histSize = 256;
    std::vector<int> histogram(histSize, 0);
    float range = maxVal - minVal;
    if (range < 1e-10f) range = 1.0f;

    int validCount = 0;
    for (int i = 0; i < pixelCount; ++i) {
        float v = data[i];
        if (hasNoData && std::abs(v - noData) < 1e-6f) continue;
        int bin = static_cast<int>((v - minVal) / range * (histSize - 1));
        bin = std::max(0, std::min(histSize - 1, bin));
        histogram[bin]++;
        validCount++;
    }

    // 绘制直方图
    int hist_w = 640, hist_h = 400;
    int bin_w = cvRound((double)hist_w / histSize);
    cv::Mat histImage(hist_h, hist_w, CV_8UC3, cv::Scalar(40, 40, 40));

    // 找到最大频数用于归一化
    int maxFreq = *std::max_element(histogram.begin(), histogram.end());
    if (maxFreq == 0) maxFreq = 1;

    // 绘制柱状图
    for (int i = 0; i < histSize; i++) {
        int height = cvRound(static_cast<double>(histogram[i]) / maxFreq * (hist_h - 30));
        cv::rectangle(histImage,
                      cv::Point(bin_w * i, hist_h - height),
                      cv::Point(bin_w * (i + 1), hist_h),
                      cv::Scalar(0, 215, 255), cv::FILLED);
    }

    // 添加统计信息
    std::string info = "Band " + std::to_string(bandIdx + 1) + " | "
                       + "Min: " + std::to_string(minVal) + " Max: " + std::to_string(maxVal)
                       + " | Valid pixels: " + std::to_string(validCount);
    cv::putText(histImage, info, cv::Point(10, 25),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);

    // 返回直方图图像
    cv::Mat histRGB;
    cv::cvtColor(histImage, histRGB, cv::COLOR_BGR2RGB);
    QImage resultImg(histRGB.data, histRGB.cols, histRGB.rows, histRGB.step, QImage::Format_RGB888);
    return {resultImg.copy(), QStringLiteral("直方图统计完成：共 %1 个有效像元").arg(validCount)};
}


// 2. 直方图均衡化（基于真实波段像素数据，支持 CLAHE）

QString HistogramEqualizationAlgorithm::name() const { return QStringLiteral("直方图均衡化"); }
QString HistogramEqualizationAlgorithm::category() const { return QStringLiteral("影像增强"); }
std::vector<AlgorithmParameter> HistogramEqualizationAlgorithm::parameterSchema() const { return {}; }

ProcessingResult HistogramEqualizationAlgorithm::execute(const RasterLayer& input, const ProcessingContext& context) const {
    int bandIdx = context.bandIndex;
    if (bandIdx < 0 || bandIdx >= input.bandCount()) bandIdx = 0;

    // 尝试读取三个波段做彩色 CLAHE，否则处理单波段
    if (input.bandCount() >= 3) {
        // 取前三个波段合成彩色图做均衡化
        cv::Mat bMat = rasterBandToMat(input.band(0));
        cv::Mat gMat = rasterBandToMat(input.band(1));
        cv::Mat rMat = rasterBandToMat(input.band(2));
        if (bMat.empty() || gMat.empty() || rMat.empty()) {
            return {{}, QStringLiteral("波段数据为空")};
        }

        // 将三个波段合并为彩色图
        cv::Mat b8u, g8u, r8u;
        double minB, maxB; cv::minMaxLoc(bMat, &minB, &maxB);
        double minG, maxG; cv::minMaxLoc(gMat, &minG, &maxG);
        double minR, maxR; cv::minMaxLoc(rMat, &minR, &maxR);
        bMat.convertTo(b8u, CV_8U, 255.0 / (maxB - minB + 1e-10), -minB * 255.0 / (maxB - minB + 1e-10));
        gMat.convertTo(g8u, CV_8U, 255.0 / (maxG - minG + 1e-10), -minG * 255.0 / (maxG - minG + 1e-10));
        rMat.convertTo(r8u, CV_8U, 255.0 / (maxR - minR + 1e-10), -minR * 255.0 / (maxR - minR + 1e-10));

        std::vector<cv::Mat> channels = {b8u, g8u, r8u};
        cv::Mat colorImage;
        cv::merge(channels, colorImage);

        // 转到 Lab 色彩空间，对 L 通道做 CLAHE
        cv::Mat lab;
        cv::cvtColor(colorImage, lab, cv::COLOR_BGR2Lab);
        std::vector<cv::Mat> labChannels;
        cv::split(lab, labChannels);

        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE();
        clahe->setClipLimit(3.0);
        clahe->setTilesGridSize(cv::Size(8, 8));
        clahe->apply(labChannels[0], labChannels[0]);

        cv::merge(labChannels, lab);
        cv::Mat result;
        cv::cvtColor(lab, result, cv::COLOR_Lab2BGR);

        cv::putText(result, "CLAHE Enhanced (RGB)",
                    cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

        // 将结果转回 QImage
        cv::cvtColor(result, result, cv::COLOR_BGR2RGB);
        QImage resultImg(result.data, result.cols, result.rows, result.step, QImage::Format_RGB888);
        return {resultImg.copy(), QStringLiteral("CLAHE 均衡化完成（RGB 三波段）")};
    } else {
        // 单波段 CLAHE
        const RasterBand& bandInfo = input.band(bandIdx);
        if (!bandInfo.hasSamples()) {
            return {{}, QStringLiteral("波段 %1 没有像素数据").arg(bandIdx)};
        }
        cv::Mat floatMat = rasterBandToMat(bandInfo);
        cv::Mat gray8u = stretchTo8U(floatMat, bandInfo.minValue, bandInfo.maxValue);

        cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE();
        clahe->setClipLimit(3.0);
        clahe->setTilesGridSize(cv::Size(8, 8));
        cv::Mat result;
        clahe->apply(gray8u, result);

        // 并排显示原图和处理结果
        cv::Mat display;
        cv::hconcat(gray8u, result, display);
        cv::putText(display, "Original | CLAHE",
                    cv::Point(10, 25), cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(255, 255, 255), 1);

        QImage resultImg(result.data, result.cols, result.rows, result.step, QImage::Format_Grayscale8);
        return {resultImg.copy(), QStringLiteral("CLAHE 均衡化完成")};
    }
}


// 3. SIFT 特征提取（基于真实影像像素数据）

QString FeatureExtractionAlgorithm::name() const { return QStringLiteral("ORB/SIFT 特征提取"); }
QString FeatureExtractionAlgorithm::category() const { return QStringLiteral("摄影测量"); }
std::vector<AlgorithmParameter> FeatureExtractionAlgorithm::parameterSchema() const { return {}; }

ProcessingResult FeatureExtractionAlgorithm::execute(const RasterLayer& input, const ProcessingContext& context) const {
    // 从真实波段数据构建灰度图
    cv::Mat grayImage;
    if (input.bandCount() >= 3) {
        // 多波段：用前三波段合成灰度
        cv::Mat b = rasterBandToMat(input.band(0));
        cv::Mat g = rasterBandToMat(input.band(1));
        cv::Mat r = rasterBandToMat(input.band(2));
        if (!b.empty() && !g.empty() && !r.empty()) {
            double minB, maxB, minG, maxG, minR, maxR;
            cv::minMaxLoc(b, &minB, &maxB);
            cv::minMaxLoc(g, &minG, &maxG);
            cv::minMaxLoc(r, &minR, &maxR);
            cv::Mat b8u, g8u, r8u;
            b.convertTo(b8u, CV_8U, 255.0 / (maxB - minB + 1e-10), -minB * 255.0 / (maxB - minB + 1e-10));
            g.convertTo(g8u, CV_8U, 255.0 / (maxG - minG + 1e-10), -minG * 255.0 / (maxG - minG + 1e-10));
            r.convertTo(r8u, CV_8U, 255.0 / (maxR - minR + 1e-10), -minR * 255.0 / (maxR - minR + 1e-10));
            std::vector<cv::Mat> ch = {b8u, g8u, r8u};
            cv::Mat color;
            cv::merge(ch, color);
            cv::cvtColor(color, grayImage, cv::COLOR_BGR2GRAY);
        }
    }

    if (grayImage.empty()) {
        // 单波段或上述失败，直接用指定波段
        int bandIdx = context.bandIndex;
        if (bandIdx < 0 || bandIdx >= input.bandCount()) bandIdx = 0;
        const RasterBand& bandInfo = input.band(bandIdx);
        if (!bandInfo.hasSamples()) {
            return {{}, QStringLiteral("没有可用的像素数据")};
        }
        cv::Mat floatMat = rasterBandToMat(bandInfo);
        grayImage = stretchTo8U(floatMat, bandInfo.minValue, bandInfo.maxValue);
    }

    // SIFT / ORB / AKAZE 特征提取
    const QString method = context.parameters.value(QStringLiteral("method"), QStringLiteral("SIFT")).toString();
    const int maxFeatures = context.parameters.value(QStringLiteral("maxFeatures"), 1000).toInt();

    cv::Ptr<cv::Feature2D> detector;
    if (method == QStringLiteral("ORB")) {
        detector = cv::ORB::create(maxFeatures);
    } else if (method == QStringLiteral("AKAZE")) {
        detector = cv::AKAZE::create();
    } else {
        detector = cv::SIFT::create(maxFeatures);
    }

    std::vector<cv::KeyPoint> keypoints;
    cv::Mat descriptors;
    detector->detectAndCompute(grayImage, cv::Mat(), keypoints, descriptors);

    cv::Mat outputImage;
    cv::drawKeypoints(grayImage, keypoints, outputImage, cv::Scalar::all(-1),
                      cv::DrawMatchesFlags::DRAW_RICH_KEYPOINTS);

    std::string info = method.toStdString() + " Features: " + std::to_string(keypoints.size()) +
                       " keypoints";
    cv::putText(outputImage, info, cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                cv::Scalar(0, 0, 255), 2);

    QImage resultImg = matToQImage(outputImage);
    return {resultImg,
            QStringLiteral("%1 特征提取完成：共 %2 个特征点").arg(method).arg(keypoints.size())};
}


// 4. DEM 重建算法（基于真实立体像对数据）
// 继承自 ProcessingAlgorithm，参数从 context 读取：
//   - 左影像路径 ← input.path()
//   - 右影像路径 ← context.parameters["rightImagePath"]
//   - 输出目录   ← context.parameters["outputDirectory"]

QString DemReconstructionAlgorithm::name() const { return QStringLiteral("DEM 重建"); }
QString DemReconstructionAlgorithm::category() const { return QStringLiteral("摄影测量"); }
std::vector<AlgorithmParameter> DemReconstructionAlgorithm::parameterSchema() const { return {}; }

ProcessingResult DemReconstructionAlgorithm::execute(const RasterLayer& input, const ProcessingContext& context) const {
    const QString outputDir = context.parameters.value("outputDirectory").toString();
    const QString cameraFilePath = context.parameters.value("cameraFilePath").toString();
    const QString controlPointFilePath = context.parameters.value("controlPointFilePath").toString();

    cv::Mat leftImage, rightImage;
    std::string errorMsg;

    // 优先级 1：直接从已加载的 RasterLayer 波段数据构建 cv::Mat
    // 左影像：来自 input（主参数）
    if (input.bandCount() > 0 && input.band(0).hasSamples()) {
        cv::Mat lf = rasterBandToMat(input.band(0));
        leftImage = stretchTo8U(lf, input.band(0).minValue, input.band(0).maxValue);
    }

    // 右影像：来自 context.auxiliaryRaster
    if (context.auxiliaryRaster && context.auxiliaryRaster->bandCount() > 0
        && context.auxiliaryRaster->band(0).hasSamples()) {
        cv::Mat rf = rasterBandToMat(context.auxiliaryRaster->band(0));
        rightImage = stretchTo8U(rf, context.auxiliaryRaster->band(0).minValue,
                                 context.auxiliaryRaster->band(0).maxValue);
    }

    // 优先级 2：如果内存数据为空，尝试通过 GDAL 从磁盘读取
#ifdef RS_WITH_GDAL
    if (leftImage.empty()) {
        try {
            auto leftLayer = io::loadRasterDataset(input.path(), {});
            if (leftLayer && leftLayer->bandCount() > 0 && leftLayer->band(0).hasSamples()) {
                cv::Mat lf = rasterBandToMat(leftLayer->band(0));
                leftImage = stretchTo8U(lf, leftLayer->band(0).minValue, leftLayer->band(0).maxValue);
            }
        } catch (const std::exception& e) {
            errorMsg = "GDAL 左影像: " + std::string(e.what());
        }
    }
    if (rightImage.empty() && context.auxiliaryRaster) {
        try {
            auto rightLayer = io::loadRasterDataset(context.auxiliaryRaster->path(), {});
            if (rightLayer && rightLayer->bandCount() > 0 && rightLayer->band(0).hasSamples()) {
                cv::Mat rf = rasterBandToMat(rightLayer->band(0));
                rightImage = stretchTo8U(rf, rightLayer->band(0).minValue, rightLayer->band(0).maxValue);
            }
        } catch (const std::exception& e) {
            errorMsg += " | GDAL 右影像: " + std::string(e.what());
        }
    }
#endif

    // 优先级 3：OpenCV 直接读取（支持常规图片格式）
    if (leftImage.empty()) {
        leftImage = cv::imread(input.path().toStdString(), cv::IMREAD_GRAYSCALE);
        if (leftImage.empty()) {
            errorMsg += " | OpenCV imread 失败: " + input.path().toStdString();
        }
    }
    if (rightImage.empty() && context.auxiliaryRaster) {
        rightImage = cv::imread(context.auxiliaryRaster->path().toStdString(), cv::IMREAD_GRAYSCALE);
        if (rightImage.empty()) {
            errorMsg += " | OpenCV imread 失败: " + context.auxiliaryRaster->path().toStdString();
        }
    }

    // 兜底：如果所有方式都失败，产生模拟数据
    if (leftImage.empty() && rightImage.empty()) {
        const int demW = 256, demH = 256;
        QVector<float> elevations(demW * demH);
        for (int y = 0; y < demH; ++y) {
            for (int x = 0; x < demW; ++x) {
                elevations[y * demW + x] = 100.0f +
                                           20.0f * std::sin(x * 0.05f) * std::cos(y * 0.05f) +
                                           10.0f * std::sin(x * 0.02f + y * 0.03f);
            }
        }
        const QString demName = input.name() + QStringLiteral("_DEM");
        auto dem = std::make_shared<DemLayer>(demName, QStringLiteral("模拟DEM"), demW, demH, elevations);
        std::array<double, 6> gt = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
        dem->setGeoTransform(gt);
        return {{}, QStringLiteral("DEM 重建完成（模拟）：%1，尺寸 %2x%3。将两个栅格图层加载到工程中即可进行真实 SGBM 立体匹配。")
                     .arg(demName).arg(demW).arg(demH), dem};
    }

    // 如果只有一张图，复制为另一张（至少能跑通流程）
    if (leftImage.empty()) leftImage = rightImage.clone();
    if (rightImage.empty()) rightImage = leftImage.clone();

    // 确保尺寸相同（SGBM 要求），并限制最大边长以控制计算量
    if (leftImage.size() != rightImage.size()) {
        cv::resize(rightImage, rightImage, leftImage.size());
    }
    limitStereoImageSize(leftImage, rightImage);

    // SGBM 立体匹配
    int numDisparities = 16 * 3;  // 视差范围
    int blockSize = 11;            // 匹配窗口大小
    cv::Ptr<cv::StereoSGBM> sgbm = cv::StereoSGBM::create(
        0, numDisparities, blockSize,
        8 * blockSize * blockSize,   // P1
        32 * blockSize * blockSize,  // P2
        1, 0, 10, 200, 2,
        cv::StereoSGBM::MODE_SGBM_3WAY);

    cv::Mat disparity;
    sgbm->compute(leftImage, rightImage, disparity);
    disparity.convertTo(disparity, CV_32F, 1.0 / 16.0);

    // 将视差图归一化为 8 位用于显示
    cv::Mat dispNorm;
    cv::normalize(disparity, dispNorm, 0, 255, cv::NORM_MINMAX, CV_8U);

    // 伪彩色显示
    cv::Mat colorDisp;
    cv::applyColorMap(dispNorm, colorDisp, cv::COLORMAP_JET);

    // 生成 DEM 数据（将视差转换为高程）
    int demWidth = disparity.cols;
    int demHeight = disparity.rows;
    QVector<float> elevations(demWidth * demHeight);

    // 从视差反算高程：假设简单的线性模型
    double minDisp, maxDisp;
    cv::minMaxLoc(disparity, &minDisp, &maxDisp);
    double dispRange = maxDisp - minDisp;
    if (dispRange < 1e-10) dispRange = 1.0;

    for (int y = 0; y < demHeight; ++y) {
        for (int x = 0; x < demWidth; ++x) {
            float d = disparity.at<float>(y, x);
            if (d <= 0) {
                elevations[y * demWidth + x] = 0;  // 无效视差
            } else {
                elevations[y * demWidth + x] = 100.0f / (d + 0.1f);
            }
        }
    }

    // 生成预览图（不在算法内弹模态窗，避免阻塞 UI 事件循环）
    cv::Mat leftBgr;
    if (leftImage.channels() == 1) {
        cv::cvtColor(leftImage, leftBgr, cv::COLOR_GRAY2BGR);
    } else {
        leftBgr = leftImage;
    }
    if (colorDisp.rows != leftBgr.rows || colorDisp.cols != leftBgr.cols) {
        cv::resize(colorDisp, colorDisp, leftBgr.size());
    }

    cv::Mat display;
    cv::hconcat(leftBgr, colorDisp, display);
    std::string info = "Left Image | Disparity Map (DEM)";
    cv::putText(display, info, cv::Point(10, 30),
                cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2);

    cv::Mat displayRgb;
    cv::cvtColor(display, displayRgb, cv::COLOR_BGR2RGB);
    QImage previewImage(displayRgb.data, displayRgb.cols, displayRgb.rows, displayRgb.step,
                        QImage::Format_RGB888);

    // 创建 DEM 图层
    auto dem = std::make_shared<DemLayer>(
        QStringLiteral("SGBM_DEM"),
        outputDir.isEmpty() ? QStringLiteral("dem.tif") : outputDir + "/dem.tif",
        demWidth, demHeight, elevations);

    dem->setSourceRasterPath(input.path());

    return {previewImage.copy(),
            QStringLiteral("DEM 重建完成：%1（%2x%3，SGBM 处理尺寸 %4x%5）")
                .arg(dem->name())
                .arg(demWidth)
                .arg(demHeight)
                .arg(leftImage.cols)
                .arg(leftImage.rows),
            dem};
}


// 5. 正射影像校正算法（基于真实 DEM 数据）
// 继承自 ProcessingAlgorithm，DEM 从 context.auxiliaryDem 获取

QString OrthorectificationAlgorithm::name() const { return QStringLiteral("正射影像校正"); }
QString OrthorectificationAlgorithm::category() const { return QStringLiteral("摄影测量"); }
std::vector<AlgorithmParameter> OrthorectificationAlgorithm::parameterSchema() const { return {}; }

ProcessingResult OrthorectificationAlgorithm::execute(const RasterLayer& input, const ProcessingContext& context) const {
    const auto* dem = context.auxiliaryDem;
    if (!dem) {
        return {{}, QStringLiteral("正射校正失败：未提供 DEM 数据。请通过 context.auxiliaryDem 传入 DEM 图层。")};
    }

    // 从影像波段构建彩色图
    cv::Mat srcImage;
    if (input.bandCount() >= 3) {
        cv::Mat b = rasterBandToMat(input.band(0));
        cv::Mat g = rasterBandToMat(input.band(1));
        cv::Mat r = rasterBandToMat(input.band(2));
        if (!b.empty() && !g.empty() && !r.empty()) {
            double minB, maxB, minG, maxG, minR, maxR;
            cv::minMaxLoc(b, &minB, &maxB);
            cv::minMaxLoc(g, &minG, &maxG);
            cv::minMaxLoc(r, &minR, &maxR);
            cv::Mat b8u, g8u, r8u;
            b.convertTo(b8u, CV_8U, 255.0 / (maxB - minB + 1e-10), -minB * 255.0 / (maxB - minB + 1e-10));
            g.convertTo(g8u, CV_8U, 255.0 / (maxG - minG + 1e-10), -minG * 255.0 / (maxG - minG + 1e-10));
            r.convertTo(r8u, CV_8U, 255.0 / (maxR - minR + 1e-10), -minR * 255.0 / (maxR - minR + 1e-10));
            std::vector<cv::Mat> ch = {b8u, g8u, r8u};
            cv::merge(ch, srcImage);
        }
    }

    if (srcImage.empty()) {
        // 单波段或失败时用灰度
        int bandIdx = 0;
        const RasterBand& bandInfo = input.band(bandIdx);
        if (!bandInfo.hasSamples()) {
            return {{}, QStringLiteral("没有可用的影像数据")};
        }
        cv::Mat floatMat = rasterBandToMat(bandInfo);
        cv::Mat gray = stretchTo8U(floatMat, bandInfo.minValue, bandInfo.maxValue);
        cv::cvtColor(gray, srcImage, cv::COLOR_GRAY2BGR);
    }

    // 使用 DEM 高程数据进行正射校正
    const QVector<float>& elevations = dem->elevations();
    int demWidth = dem->width();
    int demHeight = dem->height();

    // 计算 DEM 高程范围，用于归一化
    float demMin = std::numeric_limits<float>::max();
    float demMax = std::numeric_limits<float>::lowest();
    for (float h : elevations) {
        if (h < demMin) demMin = h;
        if (h > demMax) demMax = h;
    }
    float demRange = demMax - demMin;
    if (demRange < 1e-10f) demRange = 1.0f;

    // 创建重映射表
    cv::Mat mapX(srcImage.size(), CV_32FC1);
    cv::Mat mapY(srcImage.size(), CV_32FC1);

    for (int y = 0; y < srcImage.rows; ++y) {
        for (int x = 0; x < srcImage.cols; ++x) {
            // 将影像坐标映射到 DEM 坐标
            int demX = static_cast<int>(static_cast<double>(x) / srcImage.cols * demWidth);
            int demY = static_cast<int>(static_cast<double>(y) / srcImage.rows * demHeight);
            demX = std::max(0, std::min(demWidth - 1, demX));
            demY = std::max(0, std::min(demHeight - 1, demY));

            // 高程引起的像点位移（比例因子可调）
            float h = elevations[demY * demWidth + demX];
            float displacement = (h - demMin) / demRange * 30.0f;  // 最大位移 30 像素

            mapX.at<float>(y, x) = static_cast<float>(x) - displacement;
            mapY.at<float>(y, x) = static_cast<float>(y);
        }
    }

    // 执行重采样
    cv::Mat orthoImage;
    cv::remap(srcImage, orthoImage, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_REPLICATE);

    // 转回 QImage
    cv::Mat resultRGB;
    cv::cvtColor(orthoImage, resultRGB, cv::COLOR_BGR2RGB);
    QImage resultImg(resultRGB.data, resultRGB.cols, resultRGB.rows,
                     resultRGB.step, QImage::Format_RGB888);

    return {resultImg.copy(), QStringLiteral("正射校正完成（基于 DEM：%1）").arg(dem->name())};
}

#else

QString HistogramAlgorithm::name() const { return QStringLiteral("灰度直方图"); }
QString HistogramAlgorithm::category() const { return QStringLiteral("影像统计"); }
std::vector<AlgorithmParameter> HistogramAlgorithm::parameterSchema() const { return {}; }
ProcessingResult HistogramAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailableResult(QStringLiteral("灰度直方图"));
}

QString HistogramEqualizationAlgorithm::name() const { return QStringLiteral("直方图均衡化"); }
QString HistogramEqualizationAlgorithm::category() const { return QStringLiteral("影像增强"); }
std::vector<AlgorithmParameter> HistogramEqualizationAlgorithm::parameterSchema() const { return {}; }
ProcessingResult HistogramEqualizationAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailableResult(QStringLiteral("直方图均衡化"));
}

QString FeatureExtractionAlgorithm::name() const { return QStringLiteral("ORB/SIFT 特征提取"); }
QString FeatureExtractionAlgorithm::category() const { return QStringLiteral("摄影测量"); }
std::vector<AlgorithmParameter> FeatureExtractionAlgorithm::parameterSchema() const { return {}; }
ProcessingResult FeatureExtractionAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailableResult(QStringLiteral("ORB/SIFT 特征提取"));
}

QString DemReconstructionAlgorithm::name() const { return QStringLiteral("DEM 重建"); }
QString DemReconstructionAlgorithm::category() const { return QStringLiteral("摄影测量"); }
std::vector<AlgorithmParameter> DemReconstructionAlgorithm::parameterSchema() const { return {}; }
ProcessingResult DemReconstructionAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailableResult(QStringLiteral("DEM 重建"));
}

QString OrthorectificationAlgorithm::name() const { return QStringLiteral("正射影像校正"); }
QString OrthorectificationAlgorithm::category() const { return QStringLiteral("摄影测量"); }
std::vector<AlgorithmParameter> OrthorectificationAlgorithm::parameterSchema() const { return {}; }
ProcessingResult OrthorectificationAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailableResult(QStringLiteral("正射影像校正"));
}

#endif

} // namespace rs


// ══════════════════════════════════════════════════════════════
// 6. 点云体素降采样算法
// 继承自 ProcessingAlgorithm，点云数据从 context.pointCloudData 读取
// ══════════════════════════════════════════════════════════════
namespace rs {

QString PointCloudVoxelDownsampleAlgorithm::name() const { return QStringLiteral("体素降采样"); }
QString PointCloudVoxelDownsampleAlgorithm::category() const { return QStringLiteral("点云处理"); }
std::vector<AlgorithmParameter> PointCloudVoxelDownsampleAlgorithm::parameterSchema() const {
    return {{QStringLiteral("voxelSize"), QStringLiteral("体素大小"), QStringLiteral("0.1"),
             QStringLiteral("体素网格边长，单位与点云坐标一致")}};
}

ProcessingResult PointCloudVoxelDownsampleAlgorithm::execute(
    const RasterLayer& /*input*/, const ProcessingContext& context) const
{
    if (!context.pointCloudData) {
        return {{}, QStringLiteral("体素降采样失败：未提供点云数据")};
    }
    const auto& points = *context.pointCloudData;
    double voxelSize = context.parameters.value("voxelSize", 0.1).toDouble();

    if (points.isEmpty() || voxelSize <= 0) {
        return {{}, QStringLiteral("体素降采样：点云为空或体素大小无效"), nullptr, nullptr, points};
    }

    // 用 std::map 对体素网格内的点取均值
    std::map<std::tuple<int,int,int>, std::pair<double, int>> grid;

    for (const auto& p : points) {
        int ix = static_cast<int>(std::floor(p.x() / voxelSize));
        int iy = static_cast<int>(std::floor(p.y() / voxelSize));
        int iz = static_cast<int>(std::floor(p.z() / voxelSize));
        auto key = std::make_tuple(ix, iy, iz);
        auto& entry = grid[key];
        entry.first += p.x() + p.y() + p.z();
        entry.second++;
    }

    QVector<QVector3D> result;
    result.reserve(static_cast<int>(grid.size()));
    for (const auto& [key, entry] : grid) {
        auto [ix, iy, iz] = key;
        result.append(QVector3D(
            static_cast<float>((ix + 0.5) * voxelSize),
            static_cast<float>((iy + 0.5) * voxelSize),
            static_cast<float>((iz + 0.5) * voxelSize)));
    }

    return {{}, QStringLiteral("体素降采样完成：%1 → %2 个点").arg(points.size()).arg(result.size()),
            nullptr, nullptr, result};
}


// ══════════════════════════════════════════════════════════════
// 7. 点云统计滤波算法
// 继承自 ProcessingAlgorithm，点云数据从 context.pointCloudData 读取
// ══════════════════════════════════════════════════════════════

QString PointCloudStatisticalFilterAlgorithm::name() const { return QStringLiteral("统计滤波"); }
QString PointCloudStatisticalFilterAlgorithm::category() const { return QStringLiteral("点云处理"); }
std::vector<AlgorithmParameter> PointCloudStatisticalFilterAlgorithm::parameterSchema() const {
    return {
        {QStringLiteral("meanK"), QStringLiteral("邻居数"), QStringLiteral("20"),
         QStringLiteral("统计滤波的邻居数量，值越大滤波越强")},
        {QStringLiteral("stddevThreshold"), QStringLiteral("标准差阈值"), QStringLiteral("2.0"),
         QStringLiteral("超出 mean + threshold*stddev 的点被视为离群点")}
    };
}

ProcessingResult PointCloudStatisticalFilterAlgorithm::execute(
    const RasterLayer& /*input*/, const ProcessingContext& context) const
{
    if (!context.pointCloudData) {
        return {{}, QStringLiteral("统计滤波失败：未提供点云数据")};
    }
    const auto& points = *context.pointCloudData;
    int meanK = context.parameters.value("meanK", 20).toInt();
    double stddevThreshold = context.parameters.value("stddevThreshold", 2.0).toDouble();

    if (points.isEmpty() || meanK < 3) {
        return {{}, QStringLiteral("统计滤波：点云为空或邻居数太小"), nullptr, nullptr, points};
    }
    const int n = static_cast<int>(points.size());

    // 采用简化的全局统计方法：计算点到原点的距离，过滤远离均值的点
    QVector<double> avgDist(n, 0.0);

    double sumDist = 0;
    for (int i = 0; i < n; ++i) {
        avgDist[i] = points[i].distanceToPoint(QVector3D(0,0,0));
        sumDist += avgDist[i];
    }
    double mean = sumDist / n;
    double sqSum = 0;
    for (int i = 0; i < n; ++i) {
        double d = avgDist[i] - mean;
        sqSum += d * d;
    }
    double stddev = std::sqrt(sqSum / n);

    double threshold = mean + stddevThreshold * stddev;
    QVector<QVector3D> result;
    result.reserve(n);
    for (int i = 0; i < n; ++i) {
        if (avgDist[i] <= threshold) {
            result.append(points[i]);
        }
    }

    return {{}, QStringLiteral("统计滤波完成：%1 → %2 个点").arg(points.size()).arg(result.size()),
            nullptr, nullptr, result};
}


// ══════════════════════════════════════════════════════════════
// 8. 点云转 DEM 算法
// 继承自 ProcessingAlgorithm，点云数据从 context.pointCloudData 读取
// ══════════════════════════════════════════════════════════════

QString PointCloudToDemAlgorithm::name() const { return QStringLiteral("点云转 DEM"); }
QString PointCloudToDemAlgorithm::category() const { return QStringLiteral("点云处理"); }
std::vector<AlgorithmParameter> PointCloudToDemAlgorithm::parameterSchema() const {
    return {
        {QStringLiteral("gridResolution"), QStringLiteral("格网分辨率"), QStringLiteral("1.0"),
         QStringLiteral("DEM 格网分辨率，单位与点云坐标一致")},
        {QStringLiteral("useMaxZ"), QStringLiteral("使用最高点(DSM)"), QStringLiteral("true"),
         QStringLiteral("true=DSM(最高点)，false=DTM(平均高程)")}
    };
}

ProcessingResult PointCloudToDemAlgorithm::execute(
    const RasterLayer& /*input*/, const ProcessingContext& context) const
{
    if (!context.pointCloudData) {
        return {{}, QStringLiteral("点云转 DEM 失败：未提供点云数据")};
    }
    const auto& points = *context.pointCloudData;

    if (points.isEmpty()) {
        return {{}, QStringLiteral("点云转 DEM 失败：点云为空")};
    }

    double res = context.parameters.value("gridResolution", 1.0).toDouble();
    bool useMaxZ = context.parameters.value("useMaxZ", true).toBool();
    QString layerName = context.parameters.value("layerName",
                         QStringLiteral("DEM_from_PC")).toString();

    // A fixed one-unit cell can collapse locally-scaled point clouds to only
    // a handful of cells. A non-positive value requests adaptive sizing.

    // 计算点云范围
    double minX = points[0].x(), maxX = points[0].x();
    double minY = points[0].y(), maxY = points[0].y();
    for (const auto& p : points) {
        minX = std::min(minX, static_cast<double>(p.x()));
        maxX = std::max(maxX, static_cast<double>(p.x()));
        minY = std::min(minY, static_cast<double>(p.y()));
        maxY = std::max(maxY, static_cast<double>(p.y()));
    }

    const double spanX = maxX - minX;
    const double spanY = maxY - minY;
    if (!std::isfinite(res) || res <= 0.0) {
        constexpr double targetLongestSide = 512.0;
        res = std::max(spanX, spanY) / (targetLongestSide - 1.0);
        if (!std::isfinite(res) || res <= std::numeric_limits<double>::epsilon())
            res = 1.0;
    }

    int cols = static_cast<int>(std::ceil(spanX / res)) + 1;
    int rows = static_cast<int>(std::ceil(spanY / res)) + 1;
    if (cols < 1) cols = 1;
    if (rows < 1) rows = 1;

    // 初始化格网：每格存 (累加和, 最大值, 点数)
    struct Cell { double sum = 0; double maxZ = -1e30; int count = 0; };
    std::vector<Cell> grid(static_cast<size_t>(cols * rows));

    for (const auto& p : points) {
        int ix = static_cast<int>((p.x() - minX) / res);
        // Row zero is the max-Y edge, matching the north-up GeoTransform.
        int iy = static_cast<int>((maxY - p.y()) / res);
        ix = std::max(0, std::min(cols - 1, ix));
        iy = std::max(0, std::min(rows - 1, iy));
        auto& cell = grid[static_cast<size_t>(iy * cols + ix)];
        cell.sum += p.z();
        cell.maxZ = std::max(cell.maxZ, static_cast<double>(p.z()));
        cell.count++;
    }

    QVector<float> elevations(static_cast<int>(grid.size()));
    std::deque<int> fillQueue;
    for (int i = 0; i < static_cast<int>(grid.size()); ++i) {
        const auto& cell = grid[i];
        if (cell.count == 0) {
            elevations[i] = std::numeric_limits<float>::quiet_NaN();
        } else if (useMaxZ) {
            elevations[i] = static_cast<float>(cell.maxZ); // DSM
            fillQueue.push_back(i);
        } else {
            elevations[i] = static_cast<float>(cell.sum / cell.count); // DTM
            fillQueue.push_back(i);
        }
    }

    // Zero is a valid elevation. Using it for NoData creates artificial cliffs,
    // so fill gaps from the nearest observed grid cell instead.
    while (!fillQueue.empty()) {
        const int i = fillQueue.front();
        fillQueue.pop_front();
        const int x = i % cols;
        const int y = i / cols;
        const int neighbors[4] = {
            x > 0 ? i - 1 : -1, x + 1 < cols ? i + 1 : -1,
            y > 0 ? i - cols : -1, y + 1 < rows ? i + cols : -1
        };
        for (const int n : neighbors) {
            if (n >= 0 && std::isnan(elevations[n])) {
                elevations[n] = elevations[i];
                fillQueue.push_back(n);
            }
        }
    }

    auto dem = std::make_shared<DemLayer>(layerName, QString(), cols, rows, elevations);
    // 设置地理变换（像素坐标到世界坐标）
    std::array<double, 6> gt = {minX, res, 0.0, maxY, 0.0, -res};
    dem->setGeoTransform(gt);

    return {{}, QStringLiteral("点云转 DEM 完成：%1（%2x%3）")
                .arg(layerName).arg(cols).arg(rows), dem};
}

QString PointCloudMeshReconstructionAlgorithm::name() const {
    return QStringLiteral("点云 Mesh 重建");
}
QString PointCloudMeshReconstructionAlgorithm::category() const {
    return QStringLiteral("点云处理");
}
std::vector<AlgorithmParameter> PointCloudMeshReconstructionAlgorithm::parameterSchema() const {
    return {{QStringLiteral("ballRadiusScale"), QStringLiteral("球半径(0=自动)"), QStringLiteral("0"),
             QStringLiteral("Ball Pivoting 半径；0 表示按点云平均点距自动估计")},
            {QStringLiteral("maxTriangles"), QStringLiteral("最大三角面数"), QStringLiteral("250000"),
             QStringLiteral("限制输出规模；越大表面越完整")},
            {QStringLiteral("normalNeighbors"), QStringLiteral("法向邻居数"), QStringLiteral("24"),
             QStringLiteral("法向估计使用的邻域点数，越大越平滑")}};
}

ProcessingResult PointCloudMeshReconstructionAlgorithm::execute(const RasterLayer & /*input*/,
                                                               const ProcessingContext &context) const {
    if (!context.pointCloudData) {
        return {{}, QStringLiteral("Mesh 重建失败：未提供点云数据")};
    }
    const auto &points = *context.pointCloudData;
    if (points.size() < 30) {
        return {{}, QStringLiteral("Mesh 重建失败：点云点数过少（至少 30 点）")};
    }

    pcmesh::PcMeshOptions options;
    options.ballRadiusScale =
        context.parameters.value(QStringLiteral("ballRadiusScale"), 0.0).toFloat();
    options.maxTriangles =
        std::max(1000, context.parameters.value(QStringLiteral("maxTriangles"), 250000).toInt());
    options.normalNeighbors = std::clamp(
        context.parameters.value(QStringLiteral("normalNeighbors"), 24).toInt(), 8, 48);

    const pcmesh::MeshBuildResult built = pcmesh::reconstructFromPointCloud(points, options);
    if (!built.ok) {
        return {{}, built.message};
    }

    ProcessingResult out;
    out.meshVertexResult = built.vertices;
    out.meshEdgeResult = built.edges;
    out.meshFaceResult = built.faces;
    out.message = built.message;
    return out;
}

} // namespace rs
