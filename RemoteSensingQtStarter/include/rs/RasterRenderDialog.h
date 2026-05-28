#pragma once

#include "rs/Geometry.h"

#include <QWidget>

#include <optional>

namespace rs {

enum class RasterRenderMode {
    AutoRgb,
    RgbBands,
    SingleBandGray,
    PseudoColor
};

struct RasterRenderRequest {
    RasterRenderMode mode {RasterRenderMode::AutoRgb};
    int redBand {0};
    int greenBand {1};
    int blueBand {2};
    int grayBand {0};
    bool stretchToByte {true};
};

std::optional<RasterRenderRequest> askRasterRenderRequest(QWidget* parent, const RasterLayer& raster);

} // namespace rs
