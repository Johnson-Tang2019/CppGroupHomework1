#pragma once

#include "rs/Geometry.h"

#include <QVector3D>
#include <QVector>

namespace rs::pcmesh {

struct PcMeshOptions {
    int maxInputPoints = 100000;
    int normalNeighbors = 24;
    float ballRadiusScale = 0.0f;
    int maxTriangles = 250000;
    float minTriangleAngleDeg = 5.0f;
};

struct MeshBuildResult {
    bool ok = false;
    QString message;
    QVector<QVector3D> vertices;
    QVector<Edge> edges;
    QVector<Face> faces;
    bool loadedFromPlyFaces = false;
};

MeshBuildResult reconstructFromPointCloud(const QVector<QVector3D> &points,
                                        const PcMeshOptions &options = {});

MeshBuildResult extractMeshFromPlyFile(const QString &path, const PcMeshOptions &options = {});

} // namespace rs::pcmesh
