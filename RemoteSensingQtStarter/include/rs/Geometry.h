#pragma once

#include "rs/DataObject.h"

#include <array>
#include <QImage>
#include <QStringList>
#include <QVector>
#include <QVector3D>

namespace rs {

struct Face {
    int a {};
    int b {};
    int c {};
};

struct RasterBand {
    QString name;
    int width {};
    int height {};
    double noDataValue {};
    bool hasNoDataValue {};
    float minValue {};
    float maxValue {};
    QVector<float> samples;

    bool hasSamples() const { return width > 0 && height > 0 && !samples.isEmpty(); }
};

class RasterLayer final : public DataObject {
public:
    RasterLayer(QString name, QString path, QVector<RasterBand> bands = {}, QImage displayImage = {})
        : DataObject(std::move(name), std::move(path), DataType::Raster),
          bands_(std::move(bands)),
          displayImage_(std::move(displayImage)) {}

    int bandCount() const { return bands_.size(); }
    bool hasRasterBands() const { return !bands_.isEmpty(); }
    const QVector<RasterBand>& bands() const { return bands_; }
    const RasterBand& band(int zeroBasedIndex) const { return bands_.at(zeroBasedIndex); }

    const QImage& currentDisplayImage() const { return displayImage_; }
    void setCurrentDisplayImage(QImage image) { displayImage_ = std::move(image); }

    QString projection() const { return projection_; }
    void setProjection(QString projection) { projection_ = std::move(projection); }

    std::array<double, 6> geoTransform() const { return geoTransform_; }
    void setGeoTransform(const std::array<double, 6>& transform) { geoTransform_ = transform; }

    QString renderDescription() const { return renderDescription_; }
    void setRenderDescription(QString description) { renderDescription_ = std::move(description); }

    QString summary() const override {
        if (!bands_.isEmpty()) {
            return QStringLiteral("%1 x %2, %3 bands")
                .arg(bands_.front().width)
                .arg(bands_.front().height)
                .arg(bands_.size());
        }
        return QStringLiteral("GDAL dataset TODO");
    }

private:
    QVector<RasterBand> bands_;
    QImage displayImage_;
    QString projection_;
    std::array<double, 6> geoTransform_ {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
    QString renderDescription_ {QStringLiteral("未设色")};
};

class PointCloudLayer final : public DataObject {
public:
    PointCloudLayer(QString name, QString path, QVector<QVector3D> points)
        : DataObject(std::move(name), std::move(path), DataType::PointCloud),
          points_(std::move(points)) {}

    const QVector<QVector3D>& points() const { return points_; }
    QString summary() const override { return QStringLiteral("%1 points").arg(points_.size()); }

private:
    QVector<QVector3D> points_;
};

class MeshLayer final : public DataObject {
public:
    MeshLayer(QString name, QString path, QVector<QVector3D> vertices, QVector<Face> faces)
        : DataObject(std::move(name), std::move(path), DataType::Mesh),
          vertices_(std::move(vertices)),
          faces_(std::move(faces)) {}

    const QVector<QVector3D>& vertices() const { return vertices_; }
    const QVector<Face>& faces() const { return faces_; }
    QString summary() const override {
        return QStringLiteral("%1 vertices, %2 faces").arg(vertices_.size()).arg(faces_.size());
    }

private:
    QVector<QVector3D> vertices_;
    QVector<Face> faces_;
};

class DemLayer final : public DataObject {
public:
    DemLayer(QString name, QString path, int width, int height, QVector<float> elevations)
        : DataObject(std::move(name), std::move(path), DataType::Dem),
          width_(width),
          height_(height),
          elevations_(std::move(elevations)) {}

    int width() const { return width_; }
    int height() const { return height_; }
    const QVector<float>& elevations() const { return elevations_; }
    QString sourceRasterPath() const { return sourceRasterPath_; }
    void setSourceRasterPath(QString path) { sourceRasterPath_ = std::move(path); }
    std::array<double, 6> geoTransform() const { return geoTransform_; }
    void setGeoTransform(const std::array<double, 6>& transform) { geoTransform_ = transform; }
    QString summary() const override { return QStringLiteral("%1 x %2 DEM").arg(width_).arg(height_); }

private:
    int width_ {};
    int height_ {};
    QVector<float> elevations_;
    QString sourceRasterPath_;
    std::array<double, 6> geoTransform_ {0.0, 1.0, 0.0, 0.0, 0.0, -1.0};
};

} // namespace rs
