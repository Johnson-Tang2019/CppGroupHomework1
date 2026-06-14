#include "rs/pcmesh/PcMeshReconstruction.h"

#include <QCoreApplication>
#include <QVector3D>
#include <cmath>
#include <cstdio>

static QVector<QVector3D> makeSphere(int rings, int segments, float r) {
    QVector<QVector3D> pts;
    for (int i = 1; i < rings; ++i) {
        const float phi = static_cast<float>(M_PI) * i / rings;
        for (int j = 0; j < segments; ++j) {
            const float theta = 2.0f * static_cast<float>(M_PI) * j / segments;
            pts.append(QVector3D(r * std::sin(phi) * std::cos(theta), r * std::sin(phi) * std::sin(theta),
                                 r * std::cos(phi)));
        }
    }
    return pts;
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    const auto pts = makeSphere(24, 48, 10.0f);
    rs::pcmesh::PcMeshOptions opt;
    const auto result = rs::pcmesh::reconstructFromPointCloud(pts, opt);
    std::printf("ok=%d verts=%d faces=%d\n", result.ok ? 1 : 0, result.vertices.size(),
                result.faces.size());
    std::printf("msg=%s\n", result.message.toUtf8().constData());
    return result.ok ? 0 : 1;
}
