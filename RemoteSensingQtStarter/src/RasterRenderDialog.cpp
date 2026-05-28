#include "rs/RasterRenderDialog.h"

namespace rs {

std::optional<RasterRenderRequest> askRasterRenderRequest(QWidget* parent, const RasterLayer& raster) {
    Q_UNUSED(parent)
    Q_UNUSED(raster)
    return RasterRenderRequest {};
}

} // namespace rs
