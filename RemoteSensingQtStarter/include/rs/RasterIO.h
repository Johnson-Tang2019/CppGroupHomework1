#pragma once

#include "rs/Geometry.h"

#include <QString>

#include <memory>

namespace rs::io {

struct RasterReadOptions {
    bool readSamples {true};
    int previewMaxSize {2048};
};

struct RasterWriteOptions {
    QString driverName {QStringLiteral("GTiff")};
    QString creationOptions;
};

std::shared_ptr<RasterLayer> loadRasterDataset(const QString& path, const RasterReadOptions& options = {});
QImage renderSingleBandGray(const RasterLayer& raster, int zeroBasedBandIndex);
QImage renderRgbComposite(const RasterLayer& raster, int redBand, int greenBand, int blueBand);
void exportDemAsGeoTiff(const DemLayer& dem, const QString& path, const RasterWriteOptions& options = {});

} // namespace rs::io
