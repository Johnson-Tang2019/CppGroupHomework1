#include "rs/RemoteSensingIndices.h"

#include <QFile>
#include <QFont>
#include <QPainter>
#include <QStringList>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#ifdef RS_WITH_OPENCV
#include <opencv2/imgproc.hpp>
#include <opencv2/opencv.hpp>
#endif

namespace rs {
namespace {

const QStringList kSupportedIndices = {
    QStringLiteral("NDVI"), QStringLiteral("NDWI"), QStringLiteral("NDBI"),
    QStringLiteral("MNDWI"), QStringLiteral("NDCI"), QStringLiteral("NDTI"),
    QStringLiteral("TSS"), QStringLiteral("TLI"),
    QStringLiteral("FUI"), QStringLiteral("PC"), QStringLiteral("CDOM")
};

#ifndef RS_WITH_OPENCV
ProcessingResult openCvUnavailable(const QString &name) {
    return {{}, QStringLiteral("%1 failed: OpenCV is not enabled in this build.").arg(name)};
}
#endif

#ifdef RS_WITH_OPENCV

struct BandMap {
    int red = 0;
    int green = 1;
    int blue = 2;
    int nir = 2;
    int swir = 2;
    int redEdge = 2;
};

struct IndexGrid {
    QVector<float> values;
    QVector<uchar> valid;
    int width = 0;
    int height = 0;
};

struct IndexStats {
    float minValue = 0.0f;
    float maxValue = 0.0f;
    double mean = 0.0;
    int validPixels = 0;
};

int clampBand(int value, int bandCount) {
    if (bandCount <= 0)
        return 0;
    return std::clamp(value, 0, bandCount - 1);
}

BandMap bandMapFromContext(const RasterLayer &input, const ProcessingContext &context) {
    BandMap bands;
    bands.red = context.parameters.value(QStringLiteral("redBand"), 0).toInt();
    bands.green = context.parameters.value(QStringLiteral("greenBand"), 1).toInt();
    bands.blue = context.parameters.value(QStringLiteral("blueBand"), 2).toInt();
    bands.nir = context.parameters.value(QStringLiteral("nirBand"), 2).toInt();
    bands.swir = context.parameters.value(QStringLiteral("swirBand"), 2).toInt();
    bands.redEdge = context.parameters.value(QStringLiteral("redEdgeBand"), bands.nir).toInt();

    const int count = input.bandCount();
    bands.red = clampBand(bands.red, count);
    bands.green = clampBand(bands.green, count);
    bands.blue = clampBand(bands.blue, count);
    bands.nir = clampBand(bands.nir, count);
    bands.swir = clampBand(bands.swir, count);
    bands.redEdge = clampBand(bands.redEdge, count);
    return bands;
}

float bandValue(const RasterBand &band, int x, int y, bool &valid) {
    valid = false;
    if (!band.hasSamples() || x < 0 || y < 0 || x >= band.width || y >= band.height)
        return 0.0f;
    const float v = band.samples[y * band.width + x];
    if (band.hasNoDataValue && std::abs(v - static_cast<float>(band.noDataValue)) < 1e-6f)
        return 0.0f;
    if (!std::isfinite(v))
        return 0.0f;
    valid = true;
    return v;
}

float normalizedDifference(float a, float b, bool va, bool vb, bool &valid) {
    valid = va && vb && std::abs(a + b) > 1e-6f;
    return valid ? (a - b) / (a + b) : 0.0f;
}

IndexGrid computeIndexGrid(const RasterLayer &input, const QString &indexName, const BandMap &bands) {
    IndexGrid grid;
    grid.width = input.band(0).width;
    grid.height = input.band(0).height;
    grid.values.fill(0.0f, grid.width * grid.height);
    grid.valid.fill(0, grid.width * grid.height);

    for (int y = 0; y < grid.height; ++y) {
        for (int x = 0; x < grid.width; ++x) {
            bool redOk = false, greenOk = false, blueOk = false, nirOk = false, swirOk = false, redEdgeOk = false;
            const float red = bandValue(input.band(bands.red), x, y, redOk);
            const float green = bandValue(input.band(bands.green), x, y, greenOk);
            const float blue = bandValue(input.band(bands.blue), x, y, blueOk);
            const float nir = bandValue(input.band(bands.nir), x, y, nirOk);
            const float swir = bandValue(input.band(bands.swir), x, y, swirOk);
            const float redEdge = bandValue(input.band(bands.redEdge), x, y, redEdgeOk);

            bool valid = false;
            float value = 0.0f;
            if (indexName == QStringLiteral("NDVI")) {
                value = normalizedDifference(nir, red, nirOk, redOk, valid);
            } else if (indexName == QStringLiteral("NDWI")) {
                value = normalizedDifference(green, nir, greenOk, nirOk, valid);
            } else if (indexName == QStringLiteral("NDBI")) {
                value = normalizedDifference(swir, nir, swirOk, nirOk, valid);
            } else if (indexName == QStringLiteral("MNDWI")) {
                value = normalizedDifference(green, swir, greenOk, swirOk, valid);
            } else if (indexName == QStringLiteral("NDMI")) {
                value = normalizedDifference(nir, swir, nirOk, swirOk, valid);
            } else if (indexName == QStringLiteral("NDCI")) {
                value = normalizedDifference(redEdge, red, redEdgeOk, redOk, valid);
            } else if (indexName == QStringLiteral("NDTI")) {
                value = normalizedDifference(red, green, redOk, greenOk, valid);
            } else if (indexName == QStringLiteral("TSS")) {
                valid = redOk && greenOk && std::abs(green) > 1e-6f;
                value = valid ? std::max(0.0f, red / green) : 0.0f;
            } else if (indexName == QStringLiteral("TLI")) {
                bool ndciOk = false, ndtiOk = false, mndwiOk = false;
                const float ndci = normalizedDifference(redEdge, red, redEdgeOk, redOk, ndciOk);
                const float ndti = normalizedDifference(red, green, redOk, greenOk, ndtiOk);
                const float mndwi = normalizedDifference(green, swir, greenOk, swirOk, mndwiOk);
                valid = ndciOk && ndtiOk && mndwiOk;
                value = valid ? (0.45f * ndci + 0.35f * ndti - 0.20f * mndwi) : 0.0f;
            } else if (indexName == QStringLiteral("FUI")) {
                valid = redOk && greenOk && blueOk && (red + green + blue) > 1e-6f;
                if (valid) {
                    const float maxC = std::max(red, std::max(green, blue));
                    const float minC = std::min(red, std::min(green, blue));
                    float hue = 0.0f;
                    if (maxC - minC > 1e-6f) {
                        if (maxC == red) {
                            hue = 60.0f * std::fmod(((green - blue) / (maxC - minC)), 6.0f);
                        } else if (maxC == green) {
                            hue = 60.0f * (((blue - red) / (maxC - minC)) + 2.0f);
                        } else {
                            hue = 60.0f * (((red - green) / (maxC - minC)) + 4.0f);
                        }
                        if (hue < 0.0f)
                            hue += 360.0f;
                    }
                    const float waterHue = std::clamp((hue - 60.0f) / 180.0f, 0.0f, 1.0f);
                    value = 1.0f + waterHue * 20.0f;
                }
            } else if (indexName == QStringLiteral("PC")) {
                value = normalizedDifference(redEdge, red, redEdgeOk, redOk, valid);
            } else if (indexName == QStringLiteral("CDOM")) {
                value = normalizedDifference(green, blue, greenOk, blueOk, valid);
            }

            const int offset = y * grid.width + x;
            grid.values[offset] = value;
            grid.valid[offset] = valid ? 1 : 0;
        }
    }
    return grid;
}

IndexStats computeStats(const IndexGrid &grid) {
    IndexStats stats;
    float minValue = std::numeric_limits<float>::max();
    float maxValue = std::numeric_limits<float>::lowest();
    double sum = 0.0;
    int count = 0;

    for (int i = 0; i < grid.values.size(); ++i) {
        if (!grid.valid.value(i))
            continue;
        const float value = grid.values[i];
        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
        sum += value;
        ++count;
    }

    stats.validPixels = count;
    if (count > 0) {
        stats.minValue = minValue;
        stats.maxValue = maxValue;
        stats.mean = sum / count;
    }
    return stats;
}

double pixelGroundAreaSqM(const std::array<double, 6> &gt) {
    return std::abs(gt[1] * gt[5] - gt[2] * gt[4]);
}

QString formatArea(double sqMeters) {
    if (sqMeters <= 0.0)
        return QStringLiteral("N/A (无地理参考)");
    if (sqMeters >= 1.0e6)
        return QStringLiteral("%1 km²").arg(sqMeters / 1.0e6, 0, 'f', 3);
    if (sqMeters >= 10000.0)
        return QStringLiteral("%1 公顷").arg(sqMeters / 10000.0, 0, 'f', 2);
    return QStringLiteral("%1 m²").arg(sqMeters, 0, 'f', 1);
}

struct MaskStats {
    int waterPixels = 0;
    int validPixels = 0;
    double waterRatio = 0.0;
};

MaskStats computeWaterMaskStats(const IndexGrid &grid, float threshold) {
    MaskStats stats;
    for (int i = 0; i < grid.values.size(); ++i) {
        if (!grid.valid.value(i))
            continue;
        ++stats.validPixels;
        if (grid.values[i] >= threshold)
            ++stats.waterPixels;
    }
    if (stats.validPixels > 0)
        stats.waterRatio = static_cast<double>(stats.waterPixels) / stats.validPixels;
    return stats;
}

QImage waterMaskToImage(const IndexGrid &grid, float threshold) {
    QImage img(grid.width, grid.height, QImage::Format_RGB888);
    for (int y = 0; y < grid.height; ++y) {
        for (int x = 0; x < grid.width; ++x) {
            const int offset = y * grid.width + x;
            QRgb color = qRgb(40, 40, 40);
            if (grid.valid.value(offset)) {
                if (grid.values[offset] >= threshold)
                    color = qRgb(30, 120, 255);
                else
                    color = qRgb(70, 110, 70);
            }
            img.setPixel(x, y, color);
        }
    }
    return img;
}

struct BandStats {
    float minValue = 0.0f;
    float maxValue = 0.0f;
    double mean = 0.0;
    double stddev = 0.0;
    int validPixels = 0;
};

BandStats computeBandStats(const RasterBand &band) {
    BandStats stats;
    if (!band.hasSamples())
        return stats;

    float minValue = std::numeric_limits<float>::max();
    float maxValue = std::numeric_limits<float>::lowest();
    double sum = 0.0;
    double sumSq = 0.0;
    int count = 0;

    for (int y = 0; y < band.height; ++y) {
        for (int x = 0; x < band.width; ++x) {
            bool ok = false;
            const float v = bandValue(band, x, y, ok);
            if (!ok)
                continue;
            minValue = std::min(minValue, v);
            maxValue = std::max(maxValue, v);
            sum += v;
            sumSq += static_cast<double>(v) * v;
            ++count;
        }
    }

    stats.validPixels = count;
    if (count > 0) {
        stats.minValue = minValue;
        stats.maxValue = maxValue;
        stats.mean = sum / count;
        const double variance = std::max(0.0, sumSq / count - stats.mean * stats.mean);
        stats.stddev = std::sqrt(variance);
    }
    return stats;
}

QImage bandToColorPreview(const RasterBand &band) {
    if (!band.hasSamples())
        return {};

    BandStats stats = computeBandStats(band);
    cv::Mat gray(band.height, band.width, CV_8U, cv::Scalar(0));
    cv::Mat validMask(band.height, band.width, CV_8U, cv::Scalar(0));
    float range = stats.maxValue - stats.minValue;
    if (range < 1e-10f)
        range = 1.0f;

    for (int y = 0; y < band.height; ++y) {
        for (int x = 0; x < band.width; ++x) {
            bool ok = false;
            const float v = bandValue(band, x, y, ok);
            if (!ok)
                continue;
            const int g = static_cast<int>((v - stats.minValue) / range * 255.0f);
            gray.at<uchar>(y, x) = static_cast<uchar>(std::clamp(g, 0, 255));
            validMask.at<uchar>(y, x) = 255;
        }
    }

    cv::Mat color;
    cv::applyColorMap(gray, color, cv::COLORMAP_INFERNO);
    color.setTo(cv::Scalar(35, 35, 35), validMask == 0);
    cv::Mat rgb;
    cv::cvtColor(color, rgb, cv::COLOR_BGR2RGB);
    QImage img(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
    return img.copy();
}

QImage renderInfraredReportImage(const QString &title, const QStringList &lines, const QImage &nirPreview) {
    const int panelW = nirPreview.isNull() ? 520 : 280;
    const int panelH = nirPreview.isNull() ? 360 : nirPreview.height();
    const int textW = 360;
    const int margin = 16;
    QImage canvas(panelW + textW + margin * 3, std::max(panelH, 360) + margin * 2, QImage::Format_RGB888);
    canvas.fill(qRgb(255, 250, 252));

    if (!nirPreview.isNull()) {
        QPainter p(&canvas);
        p.drawImage(margin, margin, nirPreview.scaled(panelW, panelH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    QPainter painter(&canvas);
    painter.setPen(QColor(QStringLiteral("#7a3f57")));
    QFont titleFont = painter.font();
    titleFont.setPointSize(13);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.drawText(QRect(margin * 2 + panelW, margin, textW, 28), Qt::AlignLeft | Qt::AlignVCenter, title);

    QFont bodyFont = painter.font();
    bodyFont.setPointSize(9);
    bodyFont.setBold(false);
    painter.setFont(bodyFont);
    painter.setPen(Qt::black);
    int y = margin + 36;
    for (const QString &line : lines) {
        painter.drawText(QRect(margin * 2 + panelW, y, textW, 20), Qt::AlignLeft | Qt::AlignVCenter, line);
        y += 20;
    }
    return canvas;
}

QString waterHealthHint(const QString &indexName, double mean) {
    if (indexName == QStringLiteral("MNDWI"))
        return mean > 0.0 ? QStringLiteral("water-highlighted") : QStringLiteral("weak-water-signal");
    if (indexName == QStringLiteral("NDCI")) {
        if (mean < 0.05) return QStringLiteral("low-chlorophyll-risk");
        if (mean < 0.20) return QStringLiteral("moderate-chlorophyll-signal");
        return QStringLiteral("high-chlorophyll-signal");
    }
    if (indexName == QStringLiteral("NDTI")) {
        if (mean < 0.10) return QStringLiteral("low-turbidity");
        if (mean < 0.30) return QStringLiteral("moderate-turbidity");
        return QStringLiteral("high-turbidity");
    }
    if (indexName == QStringLiteral("TSS")) {
        if (mean < 1.0) return QStringLiteral("low-suspended-solid-proxy");
        if (mean < 1.5) return QStringLiteral("moderate-suspended-solid-proxy");
        return QStringLiteral("high-suspended-solid-proxy");
    }
    if (indexName == QStringLiteral("TLI")) {
        if (mean < 0.05) return QStringLiteral("low-eutrophication-proxy");
        if (mean < 0.20) return QStringLiteral("moderate-eutrophication-proxy");
        return QStringLiteral("high-eutrophication-proxy");
    }
    if (indexName == QStringLiteral("FUI")) {
        if (mean < 6.0) return QStringLiteral("blue-clear-water-color");
        if (mean < 13.0) return QStringLiteral("green-moderate-water-color");
        return QStringLiteral("yellow-brown-high-color-water");
    }
    if (indexName == QStringLiteral("PC")) {
        if (mean < 0.05) return QStringLiteral("low-phycocyanin-proxy");
        if (mean < 0.20) return QStringLiteral("moderate-phycocyanin-proxy");
        return QStringLiteral("high-phycocyanin-proxy");
    }
    if (indexName == QStringLiteral("CDOM")) {
        if (mean < 0.05) return QStringLiteral("low-cdom-proxy");
        if (mean < 0.20) return QStringLiteral("moderate-cdom-proxy");
        return QStringLiteral("high-cdom-proxy");
    }
    return QString();
}

QImage indexToColorImage(const IndexGrid &grid, const IndexStats &stats, float threshold,
                         bool applyThreshold) {
    float minV = stats.minValue;
    float maxV = stats.maxValue;
    if (stats.validPixels <= 0 || maxV - minV < 1e-10f)
        maxV = minV + 1.0f;

    cv::Mat gray(grid.height, grid.width, CV_8U, cv::Scalar(0));
    cv::Mat validMask(grid.height, grid.width, CV_8U, cv::Scalar(0));
    cv::Mat thresholdMask(grid.height, grid.width, CV_8U, cv::Scalar(0));
    for (int y = 0; y < grid.height; ++y) {
        for (int x = 0; x < grid.width; ++x) {
            const int offset = y * grid.width + x;
            if (!grid.valid[offset])
                continue;
            const float v = grid.values[offset];
            const int g = static_cast<int>((v - minV) / (maxV - minV) * 255.0f);
            gray.at<uchar>(y, x) = static_cast<uchar>(std::clamp(g, 0, 255));
            validMask.at<uchar>(y, x) = 255;
            if (applyThreshold && v >= threshold)
                thresholdMask.at<uchar>(y, x) = 255;
        }
    }

    cv::Mat color;
    cv::applyColorMap(gray, color, cv::COLORMAP_JET);
    color.setTo(cv::Scalar(35, 35, 35), validMask == 0);
    if (applyThreshold) {
        cv::Mat overlay = color.clone();
        overlay.setTo(cv::Scalar(255, 255, 255), thresholdMask);
        cv::addWeighted(color, 0.70, overlay, 0.30, 0, color);
    }

    cv::Mat rgb;
    cv::cvtColor(color, rgb, cv::COLOR_BGR2RGB);
    QImage img(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
    return img.copy();
}

#endif

} // namespace

#ifdef RS_WITH_OPENCV

QString RemoteSensingIndexAlgorithm::name() const { return QStringLiteral("Remote Sensing Index"); }
QString RemoteSensingIndexAlgorithm::category() const { return QStringLiteral("Remote Sensing Index"); }
std::vector<AlgorithmParameter> RemoteSensingIndexAlgorithm::parameterSchema() const {
    return {{QStringLiteral("index"), QStringLiteral("index"), QStringLiteral("NDVI"),
             kSupportedIndices.join(QStringLiteral(" / "))}};
}

ProcessingResult RemoteSensingIndexAlgorithm::execute(const RasterLayer &input,
                                                      const ProcessingContext &context) const {
    if (input.bandCount() < 2)
        return {{}, QStringLiteral("Index calculation failed: at least 2 bands are required.")};

    QString index = context.parameters.value(QStringLiteral("index"), QStringLiteral("NDVI")).toString().toUpper();
    if (!kSupportedIndices.contains(index))
        index = QStringLiteral("NDVI");

    const BandMap bands = bandMapFromContext(input, context);
    const IndexGrid grid = computeIndexGrid(input, index, bands);
    const IndexStats stats = computeStats(grid);
    const float threshold = context.parameters.value(QStringLiteral("threshold"), 0.2).toFloat();
    const bool useThreshold = context.parameters.value(QStringLiteral("applyThreshold"), false).toBool();

    const QString csvPath = context.parameters.value(QStringLiteral("csvPath")).toString();
    if (!csvPath.isEmpty()) {
        QFile file(csvPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "index,min,max,mean,valid_pixels,total_pixels,red_band,green_band,blue_band,nir_band,swir_band,red_edge_band,health_hint\n";
            out << index << "," << stats.minValue << "," << stats.maxValue << "," << stats.mean << ","
                << stats.validPixels << "," << grid.values.size() << ","
                << bands.red + 1 << "," << bands.green + 1 << "," << bands.blue + 1 << ","
                << bands.nir + 1 << "," << bands.swir + 1 << "," << bands.redEdge + 1 << ","
                << waterHealthHint(index, stats.mean) << "\n";
        }
    }

    QImage preview = indexToColorImage(grid, stats, threshold, useThreshold);
    auto resultLayer = std::make_shared<RasterLayer>(
        input.name() + QStringLiteral("_") + index, QString(), QVector<RasterBand>{}, preview);
    resultLayer->setRenderDescription(index + QStringLiteral(" pseudo color"));
    resultLayer->setProjection(input.projection());
    resultLayer->setGeoTransform(input.geoTransform());

    const QString hint = waterHealthHint(index, stats.mean);
    ProcessingResult result;
    result.image = preview;
    result.rasterResult = resultLayer;
    result.message = QStringLiteral("%1 finished: mean=%2, range=[%3, %4], valid=%5/%6%7")
                         .arg(index)
                         .arg(stats.mean, 0, 'f', 4)
                         .arg(stats.minValue, 0, 'f', 4)
                         .arg(stats.maxValue, 0, 'f', 4)
                         .arg(stats.validPixels)
                         .arg(grid.values.size())
                         .arg(hint.isEmpty() ? QString() : QStringLiteral(", water-health=") + hint);
    return result;
}

QString IndexTemporalCompareAlgorithm::name() const { return QStringLiteral("Multi-temporal Index Compare"); }
QString IndexTemporalCompareAlgorithm::category() const { return QStringLiteral("Remote Sensing Index"); }
std::vector<AlgorithmParameter> IndexTemporalCompareAlgorithm::parameterSchema() const { return {}; }

ProcessingResult IndexTemporalCompareAlgorithm::execute(const RasterLayer &input,
                                                        const ProcessingContext &context) const {
    const auto *other = context.auxiliaryRaster;
    if (!other || other->bandCount() < 2)
        return {{}, QStringLiteral("Temporal compare failed: select two raster layers.")};

    QString index = context.parameters.value(QStringLiteral("index"), QStringLiteral("NDVI")).toString().toUpper();
    if (!kSupportedIndices.contains(index))
        index = QStringLiteral("NDVI");

    const BandMap bands1 = bandMapFromContext(input, context);
    const BandMap bands2 = bandMapFromContext(*other, context);
    const IndexGrid g1 = computeIndexGrid(input, index, bands1);
    const IndexGrid g2 = computeIndexGrid(*other, index, bands2);

    const int w = std::min(g1.width, g2.width);
    const int h = std::min(g1.height, g2.height);
    cv::Mat diff(h, w, CV_32F, cv::Scalar(0.0f));
    cv::Mat validMask(h, w, CV_8U, cv::Scalar(0));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int o1 = y * g1.width + x;
            const int o2 = y * g2.width + x;
            if (!g1.valid[o1] || !g2.valid[o2])
                continue;
            diff.at<float>(y, x) = g1.values[o1] - g2.values[o2];
            validMask.at<uchar>(y, x) = 255;
        }
    }

    cv::Mat norm;
    cv::normalize(diff, norm, 0, 255, cv::NORM_MINMAX, CV_8U, validMask);
    cv::Mat color;
    cv::applyColorMap(norm, color, cv::COLORMAP_COOL);
    color.setTo(cv::Scalar(35, 35, 35), validMask == 0);
    cv::Mat rgb;
    cv::cvtColor(color, rgb, cv::COLOR_BGR2RGB);
    QImage img(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
    return {img.copy(), QStringLiteral("Multi-temporal %1 difference finished.").arg(index)};
}

QString WaterAreaAlgorithm::name() const { return QStringLiteral("Water Area Calculation"); }
QString WaterAreaAlgorithm::category() const { return QStringLiteral("Remote Sensing Index"); }
std::vector<AlgorithmParameter> WaterAreaAlgorithm::parameterSchema() const {
    return {{QStringLiteral("index"), QStringLiteral("index"), QStringLiteral("MNDWI"), QStringLiteral("MNDWI / NDWI")},
            {QStringLiteral("threshold"), QStringLiteral("threshold"), QStringLiteral("0.0"),
             QStringLiteral("Water threshold")}};
}

ProcessingResult WaterAreaAlgorithm::execute(const RasterLayer &input,
                                             const ProcessingContext &context) const {
    if (input.bandCount() < 2)
        return {{}, QStringLiteral("水体面积计算失败：至少需要 2 个波段。")};

    QString index = context.parameters.value(QStringLiteral("index"), QStringLiteral("MNDWI"))
                        .toString()
                        .toUpper();
    if (index != QStringLiteral("NDWI") && index != QStringLiteral("MNDWI"))
        index = QStringLiteral("MNDWI");

    const float defaultThreshold = index == QStringLiteral("MNDWI") ? 0.0f : 0.2f;
    const float threshold =
        context.parameters.value(QStringLiteral("threshold"), defaultThreshold).toFloat();

    const BandMap bands = bandMapFromContext(input, context);
    const IndexGrid grid = computeIndexGrid(input, index, bands);
    const MaskStats maskStats = computeWaterMaskStats(grid, threshold);

    const double pixelArea = pixelGroundAreaSqM(input.geoTransform());
    const auto gt = input.geoTransform();
    const bool defaultGeo = gt[0] == 0.0 && gt[1] == 1.0 && gt[2] == 0.0 && gt[3] == 0.0 &&
                            gt[4] == 0.0 && gt[5] == -1.0;
    const bool hasGeoRef = !input.projection().trimmed().isEmpty() || !defaultGeo;
    const double waterAreaSqM = hasGeoRef ? maskStats.waterPixels * pixelArea : 0.0;

    const QString csvPath = context.parameters.value(QStringLiteral("csvPath")).toString();
    if (!csvPath.isEmpty()) {
        QFile file(csvPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "index,threshold,water_pixels,valid_pixels,water_ratio,water_area_sqm,pixel_area_sqm,has_georef\n";
            out << index << "," << threshold << "," << maskStats.waterPixels << ","
                << maskStats.validPixels << "," << maskStats.waterRatio << "," << waterAreaSqM
                << "," << pixelArea << "," << (hasGeoRef ? 1 : 0) << "\n";
        }
    }

    QImage preview = waterMaskToImage(grid, threshold);
    auto resultLayer = std::make_shared<RasterLayer>(
        input.name() + QStringLiteral("_") + index + QStringLiteral("_water"), QString(),
        QVector<RasterBand>{}, preview);
    resultLayer->setRenderDescription(
        index + QStringLiteral(" water mask (threshold=") + QString::number(threshold, 'f', 2) +
        QStringLiteral(")"));
    resultLayer->setProjection(input.projection());
    resultLayer->setGeoTransform(input.geoTransform());

    QString areaText = hasGeoRef ? formatArea(waterAreaSqM) : QStringLiteral("无地理参考，仅统计像素");
    ProcessingResult result;
    result.image = preview;
    result.rasterResult = resultLayer;
    result.message =
        QStringLiteral("水体面积计算完成 [%1≥%2]：水体 %3 像素（占有效像元 %4%），面积 %5")
            .arg(index)
            .arg(threshold, 0, 'f', 2)
            .arg(maskStats.waterPixels)
            .arg(maskStats.waterRatio * 100.0, 0, 'f', 1)
            .arg(areaText);
    return result;
}

QString InfraredSpectralAnalysisAlgorithm::name() const {
    return QStringLiteral("Infrared Spectral Analysis");
}
QString InfraredSpectralAnalysisAlgorithm::category() const {
    return QStringLiteral("Remote Sensing Index");
}
std::vector<AlgorithmParameter> InfraredSpectralAnalysisAlgorithm::parameterSchema() const {
    return {};
}

ProcessingResult InfraredSpectralAnalysisAlgorithm::execute(const RasterLayer &input,
                                                            const ProcessingContext &context) const {
    if (input.bandCount() < 2)
        return {{}, QStringLiteral("红外光谱分析失败：至少需要 2 个波段。")};

    BandMap bands = bandMapFromContext(input, context);
    bands.nir = std::clamp(context.parameters.value(QStringLiteral("nirBand"), bands.nir).toInt(), 0,
                           input.bandCount() - 1);
    bands.swir = std::clamp(context.parameters.value(QStringLiteral("swirBand"), bands.swir).toInt(), 0,
                            input.bandCount() - 1);
    bands.red = std::clamp(context.parameters.value(QStringLiteral("redBand"), bands.red).toInt(), 0,
                           input.bandCount() - 1);

    const BandStats nirStats = computeBandStats(input.band(bands.nir));
    const BandStats swirStats =
        bands.swir < input.bandCount() ? computeBandStats(input.band(bands.swir)) : BandStats{};
    const BandStats redStats = computeBandStats(input.band(bands.red));

    const IndexGrid ndviGrid = computeIndexGrid(input, QStringLiteral("NDVI"), bands);
    const IndexGrid ndmiGrid = computeIndexGrid(input, QStringLiteral("NDMI"), bands);
    const IndexGrid ndbiGrid = computeIndexGrid(input, QStringLiteral("NDBI"), bands);
    const IndexStats ndviStats = computeStats(ndviGrid);
    const IndexStats ndmiStats = computeStats(ndmiGrid);
    const IndexStats ndbiStats = computeStats(ndbiGrid);

    QStringList reportLines;
    reportLines << QStringLiteral("NIR 波段: %1").arg(bands.nir + 1);
    reportLines << QStringLiteral("  均值=%1  范围=[%2, %3]")
                       .arg(nirStats.mean, 0, 'f', 2)
                       .arg(nirStats.minValue, 0, 'f', 2)
                       .arg(nirStats.maxValue, 0, 'f', 2);
    if (bands.swir < input.bandCount()) {
        reportLines << QStringLiteral("SWIR 波段: %1").arg(bands.swir + 1);
        reportLines << QStringLiteral("  均值=%1  范围=[%2, %3]")
                           .arg(swirStats.mean, 0, 'f', 2)
                           .arg(swirStats.minValue, 0, 'f', 2)
                           .arg(swirStats.maxValue, 0, 'f', 2);
    }
    reportLines << QStringLiteral("Red 波段: %1").arg(bands.red + 1);
    reportLines << QStringLiteral("  均值=%1  范围=[%2, %3]")
                       .arg(redStats.mean, 0, 'f', 2)
                       .arg(redStats.minValue, 0, 'f', 2)
                       .arg(redStats.maxValue, 0, 'f', 2);
    reportLines << QString();
    reportLines << QStringLiteral("NDVI  均值=%1").arg(ndviStats.mean, 0, 'f', 4);
    reportLines << QStringLiteral("NDMI  均值=%1").arg(ndmiStats.mean, 0, 'f', 4);
    reportLines << QStringLiteral("NDBI  均值=%1").arg(ndbiStats.mean, 0, 'f', 4);

    const QString csvPath = context.parameters.value(QStringLiteral("csvPath")).toString();
    if (!csvPath.isEmpty()) {
        QFile file(csvPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "metric,value\n";
            out << "nir_band," << bands.nir + 1 << "\n";
            out << "swir_band," << bands.swir + 1 << "\n";
            out << "red_band," << bands.red + 1 << "\n";
            out << "nir_mean," << nirStats.mean << "\n";
            out << "nir_min," << nirStats.minValue << "\n";
            out << "nir_max," << nirStats.maxValue << "\n";
            out << "ndvi_mean," << ndviStats.mean << "\n";
            out << "ndmi_mean," << ndmiStats.mean << "\n";
            out << "ndbi_mean," << ndbiStats.mean << "\n";
        }
    }

    QImage nirPreview = bandToColorPreview(input.band(bands.nir));
    QImage reportImage =
        renderInfraredReportImage(QStringLiteral("红外光谱分析"), reportLines, nirPreview);

    auto resultLayer = std::make_shared<RasterLayer>(
        input.name() + QStringLiteral("_IR_spectral"), QString(), QVector<RasterBand>{}, reportImage);
    resultLayer->setRenderDescription(QStringLiteral("Infrared spectral analysis report"));
    resultLayer->setProjection(input.projection());
    resultLayer->setGeoTransform(input.geoTransform());

    ProcessingResult result;
    result.image = reportImage;
    result.rasterResult = resultLayer;
    result.message = QStringLiteral("红外光谱分析完成：NIR=%1，NDVI=%2，NDMI=%3，NDBI=%4")
                         .arg(bands.nir + 1)
                         .arg(ndviStats.mean, 0, 'f', 4)
                         .arg(ndmiStats.mean, 0, 'f', 4)
                         .arg(ndbiStats.mean, 0, 'f', 4);
    return result;
}

#else

QString RemoteSensingIndexAlgorithm::name() const { return QStringLiteral("Remote Sensing Index"); }
QString RemoteSensingIndexAlgorithm::category() const { return QStringLiteral("Remote Sensing Index"); }
std::vector<AlgorithmParameter> RemoteSensingIndexAlgorithm::parameterSchema() const { return {}; }
ProcessingResult RemoteSensingIndexAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("Remote Sensing Index"));
}

QString IndexTemporalCompareAlgorithm::name() const { return QStringLiteral("Multi-temporal Index Compare"); }
QString IndexTemporalCompareAlgorithm::category() const { return QStringLiteral("Remote Sensing Index"); }
std::vector<AlgorithmParameter> IndexTemporalCompareAlgorithm::parameterSchema() const { return {}; }
ProcessingResult IndexTemporalCompareAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("Multi-temporal Index Compare"));
}

QString WaterAreaAlgorithm::name() const { return QStringLiteral("Water Area Calculation"); }
QString WaterAreaAlgorithm::category() const { return QStringLiteral("Remote Sensing Index"); }
std::vector<AlgorithmParameter> WaterAreaAlgorithm::parameterSchema() const { return {}; }
ProcessingResult WaterAreaAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("Water Area Calculation"));
}

QString InfraredSpectralAnalysisAlgorithm::name() const { return QStringLiteral("Infrared Spectral Analysis"); }
QString InfraredSpectralAnalysisAlgorithm::category() const { return QStringLiteral("Remote Sensing Index"); }
std::vector<AlgorithmParameter> InfraredSpectralAnalysisAlgorithm::parameterSchema() const { return {}; }
ProcessingResult InfraredSpectralAnalysisAlgorithm::execute(const RasterLayer &, const ProcessingContext &) const {
    return openCvUnavailable(QStringLiteral("Infrared Spectral Analysis"));
}

#endif

} // namespace rs
