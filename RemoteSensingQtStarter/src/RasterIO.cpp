#include "rs/RasterIO.h"

#include <stdexcept>

namespace rs::io {

std::shared_ptr<RasterLayer> loadRasterDataset(const QString& path, const RasterReadOptions& options) {
    Q_UNUSED(path)
    Q_UNUSED(options)
    throw std::runtime_error("TODO: implement GDALOpenEx, band metadata reading and preview rendering");
}

QImage renderSingleBandGray(const RasterLayer& raster, int zeroBasedBandIndex) {
    Q_UNUSED(raster)
    Q_UNUSED(zeroBasedBandIndex)
    return {};
}

QImage renderRgbComposite(const RasterLayer& raster, int redBand, int greenBand, int blueBand) {
    Q_UNUSED(raster)
    Q_UNUSED(redBand)
    Q_UNUSED(greenBand)
    Q_UNUSED(blueBand)
    return {};
}

void exportDemAsGeoTiff(const DemLayer& dem, const QString& path, const RasterWriteOptions& options) {
    Q_UNUSED(dem)
    Q_UNUSED(path)
    Q_UNUSED(options)
    throw std::runtime_error("TODO: implement GDAL GeoTIFF DEM export");
}

} // namespace rs::io
