/**
 * @file RasterIO.cpp
 * @brief 遥感影像的读写与渲染接口实现
 * 实现了 GDAL 遥感数据读取、波段渲染和 DEM 导出功能。
 */

#include "rs/RasterIO.h"

namespace rs::io {

static void ensureGdalRegistered() {
#ifdef RS_WITH_GDAL
    static const bool registered = []() {
        GDALAllRegister();
        return true;
    }();
    Q_UNUSED(registered);
#endif
}

// 辅助函数：线性拉伸像素值到 0~255 范围
static void stretchToByte(const float* src, unsigned char* dst, int count,
                           float minVal, float maxVal, float noData, bool hasNoData) {
    const float range = maxVal - minVal;
    if (range < 1e-10f) {
        for (int i = 0; i < count; ++i)
            dst[i] = (hasNoData && std::abs(src[i] - noData) < 1e-6f) ? 0 : 127;
        return;
    }
    const float scale = 255.0f / range;
    for (int i = 0; i < count; ++i) {
        if (hasNoData && std::abs(src[i] - noData) < 1e-6f) {
            dst[i] = 0;
        } else {
            float v = (src[i] - minVal) * scale;
            dst[i] = static_cast<unsigned char>(std::max(0.0f, std::min(255.0f, v)));
        }
    }
}

// 辅助函数：计算像素数据的 min/max（跳过 NoData）
static std::pair<float, float> calcMinMax(const std::vector<float>& data,
                                           float noData, bool hasNoData) {
    float minV = std::numeric_limits<float>::max();
    float maxV = std::numeric_limits<float>::lowest();
    for (float v : data) {
        if (hasNoData && std::abs(v - noData) < 1e-6f) continue;
        minV = std::min(minV, v);
        maxV = std::max(maxV, v);
    }
    if (minV > maxV) { minV = 0; maxV = 255; }
    return {minV, maxV};
}

// ============================================================================
// loadRasterDataset
// ============================================================================
std::shared_ptr<RasterLayer> loadRasterDataset(const QString& path,
                                               const RasterReadOptions& options) {
#ifndef RS_WITH_GDAL
    Q_UNUSED(options);
    throw std::runtime_error("当前构建未启用 GDAL，无法读取遥感影像: " + path.toStdString());
#else
    ensureGdalRegistered();

    GDALDataset* dataset = static_cast<GDALDataset*>(
        GDALOpenEx(path.toUtf8().constData(), GA_ReadOnly, nullptr, nullptr, nullptr));
    if (!dataset)
        throw std::runtime_error("无法打开遥感影像文件: " + path.toStdString());

    const int rasterXSize = dataset->GetRasterXSize();
    const int rasterYSize = dataset->GetRasterYSize();
    const int bandCount = dataset->GetRasterCount();
    if (rasterXSize <= 0 || rasterYSize <= 0 || bandCount <= 0) {
        GDALClose(dataset);
        throw std::runtime_error("影像尺寸或波段数无效: " + path.toStdString());
    }

    const char* projRef = dataset->GetProjectionRef();
    QString projection;
    if (projRef && std::strlen(projRef) > 0)
        projection = QString::fromUtf8(projRef);

    double gtBuf[6] = {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    if (dataset->GetGeoTransform(gtBuf) != CE_None) {
        gtBuf[0] = 0.0; gtBuf[1] = 1.0; gtBuf[2] = 0.0; gtBuf[3] = 0.0; gtBuf[4] = 0.0; gtBuf[5] = -1.0;
    }
    std::array<double, 6> geoTransform;
    for (int i = 0; i < 6; ++i) geoTransform[i] = gtBuf[i];

    QVector<RasterBand> bands;
    bands.reserve(bandCount);
    for (int b = 0; b < bandCount; ++b) {
        GDALRasterBand* gdalBand = dataset->GetRasterBand(b + 1);
        if (!gdalBand) continue;

        RasterBand bandInfo;
        const char* desc = gdalBand->GetDescription();
        bandInfo.name = (desc && std::strlen(desc) > 0)
                            ? QString::fromUtf8(desc)
                            : QString(QStringLiteral("Band %1").arg(b + 1));
        bandInfo.width = gdalBand->GetXSize();
        bandInfo.height = gdalBand->GetYSize();

        int hasNoData = false;
        bandInfo.noDataValue = gdalBand->GetNoDataValue(&hasNoData);
        bandInfo.hasNoDataValue = (hasNoData != 0);

        double minVal = 0.0, maxVal = 0.0;
        int successMin = false, successMax = false;
        minVal = gdalBand->GetMinimum(&successMin);
        maxVal = gdalBand->GetMaximum(&successMax);
        if (!successMin || !successMax) {
            double mean = 0.0, stddev = 0.0;
            gdalBand->ComputeStatistics(false, &minVal, &maxVal, &mean, &stddev, nullptr, nullptr);
        }
        bandInfo.minValue = static_cast<float>(minVal);
        bandInfo.maxValue = static_cast<float>(maxVal);

        if (options.readSamples && bandInfo.width > 0 && bandInfo.height > 0) {
            const int pixelCount = bandInfo.width * bandInfo.height;
            bandInfo.samples.resize(pixelCount);
            CPLErr err = gdalBand->RasterIO(GF_Read, 0, 0,
                                             bandInfo.width, bandInfo.height,
                                             bandInfo.samples.data(),
                                             bandInfo.width, bandInfo.height,
                                             GDT_Float32, 0, 0, nullptr);
            if (err != CE_None) bandInfo.samples.clear();
        }
        bands.append(bandInfo);
    }

    // 生成缩略图
    QImage displayImage;
    const int previewMaxSize = options.previewMaxSize;

    if (bandCount >= 3 && bands.size() >= 3) {
        const int srcW = bands[0].width;
        const int srcH = bands[0].height;
        int thumbW = srcW, thumbH = srcH, step = 1;
        while (thumbW > previewMaxSize || thumbH > previewMaxSize) {
            step *= 2; thumbW = srcW / step; thumbH = srcH / step;
        }
        if (thumbW < 1) thumbW = 1;
        if (thumbH < 1) thumbH = 1;

        displayImage = QImage(thumbW, thumbH, QImage::Format_RGB32);
        std::vector<float> rData(thumbW * thumbH), gData(thumbW * thumbH), bData(thumbW * thumbH);
        std::vector<float> rowBuf(srcW);

        for (int y = 0; y < thumbH; ++y) {
            const int srcY = y * step;
            if (srcW <= 0) continue;
            dataset->GetRasterBand(1)->RasterIO(GF_Read, 0, srcY, srcW, 1,
                                                 rowBuf.data(), srcW, 1, GDT_Float32, 0, 0, nullptr);
            for (int x = 0; x < thumbW; ++x) rData[y * thumbW + x] = rowBuf[x * step];
            dataset->GetRasterBand(2)->RasterIO(GF_Read, 0, srcY, srcW, 1,
                                                 rowBuf.data(), srcW, 1, GDT_Float32, 0, 0, nullptr);
            for (int x = 0; x < thumbW; ++x) gData[y * thumbW + x] = rowBuf[x * step];
            dataset->GetRasterBand(3)->RasterIO(GF_Read, 0, srcY, srcW, 1,
                                                 rowBuf.data(), srcW, 1, GDT_Float32, 0, 0, nullptr);
            for (int x = 0; x < thumbW; ++x) bData[y * thumbW + x] = rowBuf[x * step];
        }

        auto [rMin, rMax] = calcMinMax(rData, static_cast<float>(bands[0].noDataValue), bands[0].hasNoDataValue);
        auto [gMin, gMax] = calcMinMax(gData, static_cast<float>(bands[1].noDataValue), bands[1].hasNoDataValue);
        auto [bMin, bMax] = calcMinMax(bData, static_cast<float>(bands[2].noDataValue), bands[2].hasNoDataValue);

        std::vector<unsigned char> rS(thumbW * thumbH), gS(thumbW * thumbH), bS(thumbW * thumbH);
        stretchToByte(rData.data(), rS.data(), thumbW * thumbH, rMin, rMax,
                      static_cast<float>(bands[0].noDataValue), bands[0].hasNoDataValue);
        stretchToByte(gData.data(), gS.data(), thumbW * thumbH, gMin, gMax,
                      static_cast<float>(bands[1].noDataValue), bands[1].hasNoDataValue);
        stretchToByte(bData.data(), bS.data(), thumbW * thumbH, bMin, bMax,
                      static_cast<float>(bands[2].noDataValue), bands[2].hasNoDataValue);

        for (int y = 0; y < thumbH; ++y) {
            unsigned char* line = displayImage.scanLine(y);
            for (int x = 0; x < thumbW; ++x) {
                const int idx = y * thumbW + x;
                line[x * 4 + 0] = bS[idx];
                line[x * 4 + 1] = gS[idx];
                line[x * 4 + 2] = rS[idx];
                line[x * 4 + 3] = 255;
            }
        }
    } else if (bandCount >= 1 && !bands.isEmpty()) {
        const int srcW = bands[0].width, srcH = bands[0].height;
        int thumbW = srcW, thumbH = srcH, step = 1;
        while (thumbW > previewMaxSize || thumbH > previewMaxSize) {
            step *= 2; thumbW = srcW / step; thumbH = srcH / step;
        }
        if (thumbW < 1) thumbW = 1;
        if (thumbH < 1) thumbH = 1;

        displayImage = QImage(thumbW, thumbH, QImage::Format_Grayscale8);
        std::vector<float> data(thumbW * thumbH);
        std::vector<float> rowBuf2(srcW);
        for (int y = 0; y < thumbH; ++y) {
            const int srcY = y * step;
            if (srcW > 0) {
                dataset->GetRasterBand(1)->RasterIO(GF_Read, 0, srcY, srcW, 1,
                                                     rowBuf2.data(), srcW, 1, GDT_Float32, 0, 0, nullptr);
                for (int x = 0; x < thumbW; ++x) data[y * thumbW + x] = rowBuf2[x * step];
            }
        }
        float minV = bands[0].minValue, maxV = bands[0].maxValue;
        if (maxV - minV < 1e-10f) { minV = 0; maxV = 255; }
        for (int y = 0; y < thumbH; ++y) {
            unsigned char* line = displayImage.scanLine(y);
            for (int x = 0; x < thumbW; ++x) {
                float v = (data[y * thumbW + x] - minV) / (maxV - minV) * 255.0f;
                line[x] = static_cast<unsigned char>(std::max(0.0f, std::min(255.0f, v)));
            }
        }
    }

    GDALClose(dataset);

    QFileInfo fi(path);
    auto layer = std::make_shared<RasterLayer>(fi.fileName(), path, bands, displayImage);
    layer->setProjection(projection);
    layer->setGeoTransform(geoTransform);
    if (!displayImage.isNull()) layer->setCurrentDisplayImage(displayImage);
    return layer;
#endif
}

// ============================================================================
// renderSingleBandGray
// ============================================================================
QImage renderSingleBandGray(const RasterLayer& raster, int zeroBasedBandIndex) {
    if (zeroBasedBandIndex < 0 || zeroBasedBandIndex >= raster.bandCount()) return {};
    const RasterBand& bandInfo = raster.band(zeroBasedBandIndex);
    if (!bandInfo.hasSamples()) return {};
    const int pixelCount = bandInfo.width * bandInfo.height;
    QImage image(bandInfo.width, bandInfo.height, QImage::Format_Grayscale8);
    std::vector<unsigned char> stretched(pixelCount);
    stretchToByte(bandInfo.samples.constData(), stretched.data(), pixelCount,
                  bandInfo.minValue, bandInfo.maxValue,
                  static_cast<float>(bandInfo.noDataValue), bandInfo.hasNoDataValue);
    for (int y = 0; y < bandInfo.height; ++y) {
        std::memcpy(image.scanLine(y), stretched.data() + y * bandInfo.width,
                    static_cast<size_t>(bandInfo.width));
    }
    return image;
}

// ============================================================================
// renderRgbComposite
// ============================================================================
QImage renderRgbComposite(const RasterLayer& raster, int redBand, int greenBand, int blueBand) {
    const int bCount = raster.bandCount();
    if (redBand < 0 || redBand >= bCount || greenBand < 0 || greenBand >= bCount || blueBand < 0 || blueBand >= bCount)
        return {};
    const RasterBand& rInfo = raster.band(redBand);
    const RasterBand& gInfo = raster.band(greenBand);
    const RasterBand& bInfo = raster.band(blueBand);
    if (!rInfo.hasSamples() || !gInfo.hasSamples() || !bInfo.hasSamples()) return {};
    if (rInfo.width != gInfo.width || rInfo.width != bInfo.width || rInfo.height != gInfo.height || rInfo.height != bInfo.height)
        return {};

    const int pixelCount = rInfo.width * rInfo.height;
    std::vector<unsigned char> rS(pixelCount), gS(pixelCount), bS(pixelCount);
    stretchToByte(rInfo.samples.constData(), rS.data(), pixelCount, rInfo.minValue, rInfo.maxValue,
                  static_cast<float>(rInfo.noDataValue), rInfo.hasNoDataValue);
    stretchToByte(gInfo.samples.constData(), gS.data(), pixelCount, gInfo.minValue, gInfo.maxValue,
                  static_cast<float>(gInfo.noDataValue), gInfo.hasNoDataValue);
    stretchToByte(bInfo.samples.constData(), bS.data(), pixelCount, bInfo.minValue, bInfo.maxValue,
                  static_cast<float>(bInfo.noDataValue), bInfo.hasNoDataValue);

    QImage image(rInfo.width, rInfo.height, QImage::Format_RGB32);
    for (int y = 0; y < rInfo.height; ++y) {
        unsigned char* line = image.scanLine(y);
        for (int x = 0; x < rInfo.width; ++x) {
            const int idx = y * rInfo.width + x;
            line[x * 4 + 0] = bS[idx];
            line[x * 4 + 1] = gS[idx];
            line[x * 4 + 2] = rS[idx];
            line[x * 4 + 3] = 255;
        }
    }
    return image;
}

// ============================================================================
// exportDemAsGeoTiff
// ============================================================================
void exportDemAsGeoTiff(const DemLayer& dem, const QString& path,
                        const RasterWriteOptions& options) {
#ifndef RS_WITH_GDAL
    Q_UNUSED(dem);
    Q_UNUSED(options);
    throw std::runtime_error("当前构建未启用 GDAL，无法导出 GeoTIFF: " + path.toStdString());
#else
    ensureGdalRegistered();

    const QByteArray driverName = options.driverName.toUtf8();
    GDALDriver* driver = GetGDALDriverManager()->GetDriverByName(driverName.constData());
    if (!driver) throw std::runtime_error("无法获取 GDAL 输出驱动: " + options.driverName.toStdString());

    const int width = dem.width(), height = dem.height();
    if (width <= 0 || height <= 0) throw std::runtime_error("DEM 尺寸无效");

    char** createOptions = nullptr;
    if (!options.creationOptions.isEmpty()) {
        createOptions = CSLAddString(createOptions, options.creationOptions.toUtf8().constData());
    }
    GDALDataset* outDataset = driver->Create(path.toUtf8().constData(), width, height, 1, GDT_Float32, createOptions);
    if (createOptions) CSLDestroy(createOptions);
    if (!outDataset) throw std::runtime_error("无法创建 GeoTIFF 文件: " + path.toStdString());

    double gt[6];
    const auto& gtArr = dem.geoTransform();
    for (int i = 0; i < 6; ++i) gt[i] = gtArr[i];
    outDataset->SetGeoTransform(gt);

    const QString srcPath = dem.sourceRasterPath();
    if (!srcPath.isEmpty()) {
        GDALDataset* srcDataset = static_cast<GDALDataset*>(
            GDALOpenEx(srcPath.toUtf8().constData(), GA_ReadOnly, nullptr, nullptr, nullptr));
        if (srcDataset) {
            const char* projRef = srcDataset->GetProjectionRef();
            if (projRef && std::strlen(projRef) > 0)
                outDataset->SetProjection(projRef);
            GDALClose(srcDataset);
        }
    }

    GDALRasterBand* outBand = outDataset->GetRasterBand(1);
    const QVector<float>& elevations = dem.elevations();
    if (elevations.size() < width * height) {
        GDALClose(outDataset);
        throw std::runtime_error("DEM 高程数据不足");
    }

    std::vector<float> writable(elevations.begin(), elevations.end());
    CPLErr err = outBand->RasterIO(GF_Write, 0, 0, width, height,
                                    writable.data(), width, height, GDT_Float32, 0, 0, nullptr);
    if (err != CE_None) {
        GDALClose(outDataset);
        throw std::runtime_error("写入 DEM 数据失败");
    }
    GDALClose(outDataset);
#endif
}

} // namespace rs::io
