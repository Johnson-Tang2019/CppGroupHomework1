#include "rs/pcmesh/PcMeshReconstruction.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QTextStream>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

namespace rs::pcmesh {
namespace {

using GridPoint = QVector3D;

quint64 cellKey(int x, int y, int z) {
    return (static_cast<quint64>(static_cast<quint32>(x)) << 42) |
           (static_cast<quint64>(static_cast<quint32>(y)) << 21) |
           static_cast<quint64>(static_cast<quint32>(z));
}

void cellCoords(const GridPoint &p, float cellSize, int &ix, int &iy, int &iz) {
    ix = static_cast<int>(std::floor(p.x() / cellSize));
    iy = static_cast<int>(std::floor(p.y() / cellSize));
    iz = static_cast<int>(std::floor(p.z() / cellSize));
}

class SpatialIndex {
  public:
    void build(const QVector<GridPoint> &points, float cellSize) {
        cellSize_ = std::max(cellSize, 1e-6f);
        invCellSize_ = 1.0f / cellSize_;
        buckets_.clear();
        buckets_.reserve(points.size());
        for (int i = 0; i < points.size(); ++i) {
            int ix = 0;
            int iy = 0;
            int iz = 0;
            cellCoords(points[i], cellSize_, ix, iy, iz);
            buckets_[cellKey(ix, iy, iz)].append(i);
        }
    }

    void queryBall(const GridPoint &center, float radius, QVector<int> &out,
                   const QSet<int> *exclude = nullptr) const {
        out.clear();
        if (buckets_.isEmpty())
            return;
        const int rCells = std::max(1, static_cast<int>(std::ceil(radius * invCellSize_)));
        int cx = 0;
        int cy = 0;
        int cz = 0;
        cellCoords(center, cellSize_, cx, cy, cz);
        const float r2 = radius * radius;
        for (int dx = -rCells; dx <= rCells; ++dx) {
            for (int dy = -rCells; dy <= rCells; ++dy) {
                for (int dz = -rCells; dz <= rCells; ++dz) {
                    const auto it = buckets_.constFind(cellKey(cx + dx, cy + dy, cz + dz));
                    if (it == buckets_.constEnd())
                        continue;
                    for (int idx : *it) {
                        if (exclude && exclude->contains(idx))
                            continue;
                        out.append(idx);
                    }
                }
            }
        }
        out.erase(std::unique(out.begin(), out.end()), out.end());
        Q_UNUSED(r2);
    }

    void queryNearby(const GridPoint &p, float radius, QVector<int> &out) const {
        queryBall(p, radius, out);
    }

  private:
    float cellSize_ = 1.0f;
    float invCellSize_ = 1.0f;
    QHash<quint64, QVector<int>> buckets_;
};

QVector<GridPoint> subsampleStride(const QVector<GridPoint> &points, int maxCount) {
    if (points.size() <= maxCount)
        return points;
    QVector<GridPoint> out;
    out.reserve(maxCount);
    const double step = static_cast<double>(points.size()) / maxCount;
    for (int i = 0; i < maxCount; ++i)
        out.append(points[static_cast<int>(i * step)]);
    return out;
}

QVector<GridPoint> voxelCentroidDownsample(const QVector<GridPoint> &points, float cellSize) {
    if (points.isEmpty())
        return points;
    struct Acc {
        GridPoint sum{0, 0, 0};
        int count = 0;
    };
    QHash<quint64, Acc> cells;
    cells.reserve(points.size());
    for (const auto &p : points) {
        int ix = 0;
        int iy = 0;
        int iz = 0;
        cellCoords(p, cellSize, ix, iy, iz);
        auto &acc = cells[cellKey(ix, iy, iz)];
        acc.sum += p;
        ++acc.count;
    }
    QVector<GridPoint> out;
    out.reserve(cells.size());
    for (auto it = cells.constBegin(); it != cells.constEnd(); ++it)
        out.append(it.value().sum / static_cast<float>(it.value().count));
    return out;
}

float estimateAverageSpacing(const QVector<GridPoint> &points, SpatialIndex &index, float searchRadius,
                             int samples) {
    if (points.size() < 2)
        return 1.0f;
    const int n = std::min(samples, static_cast<int>(points.size()));
    std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> pick(0, points.size() - 1);
    double sum = 0;
    int count = 0;
    QVector<int> neighbors;
    for (int s = 0; s < n; ++s) {
        const GridPoint &p = points[pick(rng)];
        index.queryNearby(p, searchRadius, neighbors);
        float best = std::numeric_limits<float>::max();
        for (int idx : neighbors) {
            if (idx < 0 || idx >= points.size())
                continue;
            const float d = p.distanceToPoint(points[idx]);
            if (d > 1e-8f)
                best = std::min(best, d);
        }
        if (best < std::numeric_limits<float>::max()) {
            sum += best;
            ++count;
        }
    }
    return count > 0 ? static_cast<float>(sum / count) : searchRadius * 0.25f;
}

QVector<GridPoint> estimateNormals(const QVector<GridPoint> &points, SpatialIndex &index,
                                   float searchRadius, int kNeighbors) {
    QVector<GridPoint> normals;
    normals.resize(points.size());
    QVector<int> neighbors;
    for (int i = 0; i < points.size(); ++i) {
        index.queryNearby(points[i], searchRadius, neighbors);
        if (neighbors.size() > kNeighbors) {
            neighbors.resize(kNeighbors);
        }
        if (neighbors.size() < 3) {
            normals[i] = GridPoint(0, 0, 1);
            continue;
        }
        GridPoint centroid(0, 0, 0);
        for (int idx : neighbors)
            centroid += points[idx];
        centroid /= static_cast<float>(neighbors.size());

        double c00 = 0, c01 = 0, c02 = 0, c11 = 0, c12 = 0, c22 = 0;
        for (int idx : neighbors) {
            const double x = points[idx].x() - centroid.x();
            const double y = points[idx].y() - centroid.y();
            const double z = points[idx].z() - centroid.z();
            c00 += x * x;
            c01 += x * y;
            c02 += x * z;
            c11 += y * y;
            c12 += y * z;
            c22 += z * z;
        }
        auto apply = [&](const GridPoint &v) {
            return GridPoint(static_cast<float>(c00 * v.x() + c01 * v.y() + c02 * v.z()),
                             static_cast<float>(c01 * v.x() + c11 * v.y() + c12 * v.z()),
                             static_cast<float>(c02 * v.x() + c12 * v.y() + c22 * v.z()));
        };
        GridPoint major(1, 0, 0);
        for (int iter = 0; iter < 16; ++iter) {
            major = apply(major);
            const float len = major.length();
            if (len > 1e-10f)
                major /= len;
        }
        GridPoint minor = QVector3D::crossProduct(major, GridPoint(0, 0, 1));
        if (minor.lengthSquared() < 1e-8f)
            minor = QVector3D::crossProduct(major, GridPoint(0, 1, 0));
        for (int iter = 0; iter < 16; ++iter) {
            minor = apply(minor);
            minor -= major * QVector3D::dotProduct(minor, major);
            const float len = minor.length();
            if (len > 1e-10f)
                minor /= len;
        }
        GridPoint normal = QVector3D::crossProduct(major, minor);
        const float len = normal.length();
        normals[i] = len > 1e-10f ? normal / len : GridPoint(0, 0, 1);
        if (QVector3D::dotProduct(normals[i], points[i] - centroid) > 0)
            normals[i] = -normals[i];
    }
    return normals;
}

bool computeBallCenter(const GridPoint &p1, const GridPoint &p2, const GridPoint &p3, float radius,
                       GridPoint &center) {
    const GridPoint p12 = p2 - p1;
    const float d12 = p12.length();
    if (d12 < 1e-8f || d12 > 2.0f * radius + 1e-6f)
        return false;

    const GridPoint n = QVector3D::crossProduct(p12, p3 - p1);
    const float nLen = n.length();
    if (nLen < 1e-10f)
        return false;

    GridPoint v = QVector3D::crossProduct(n / nLen, p12);
    const float vLen = v.length();
    if (vLen < 1e-10f)
        return false;
    v /= vLen;

    const float h = std::sqrt(std::max(0.0f, radius * radius - 0.25f * d12 * d12));
    const GridPoint mid = (p1 + p2) * 0.5f;
    const GridPoint c1 = mid + v * h;
    const GridPoint c2 = mid - v * h;
    center = (c1.distanceToPoint(p3) < c2.distanceToPoint(p3)) ? c1 : c2;
    return std::abs(center.distanceToPoint(p3) - radius) <= radius * 0.05f + 1e-4f;
}

bool ballIsEmpty(const GridPoint &center, float radius, int i0, int i1, int i2,
                 const QVector<GridPoint> &points, const SpatialIndex &index) {
    QVector<int> nearby;
    index.queryBall(center, radius, nearby);
    const float inner = radius * 0.995f;
    const float inner2 = inner * inner;
    for (int idx : nearby) {
        if (idx == i0 || idx == i1 || idx == i2)
            continue;
        if ((points[idx] - center).lengthSquared() < inner2)
            return false;
    }
    return true;
}

float triangleMinAngleDeg(const GridPoint &a, const GridPoint &b, const GridPoint &c) {
    auto angleAt = [](const GridPoint &p0, const GridPoint &p1, const GridPoint &p2) {
        const GridPoint u = (p1 - p0).normalized();
        const GridPoint v = (p2 - p0).normalized();
        return std::acos(std::clamp(QVector3D::dotProduct(u, v), -1.0f, 1.0f)) * 57.2957795f;
    };
    return std::min({angleAt(a, b, c), angleAt(b, a, c), angleAt(c, a, b)});
}

struct BorderEdge {
    int v1 = -1;
    int v2 = -1;
    GridPoint ballCenter;
};

class BallPivotingMesher {
  public:
    BallPivotingMesher(const QVector<GridPoint> &points, const QVector<GridPoint> &normals,
                       float radius, int maxTriangles, float minAngleDeg)
        : points_(points), normals_(normals), radius_(radius), maxTriangles_(maxTriangles),
          minAngleDeg_(minAngleDeg) {
        index_.build(points_, radius_);
        work_.reserve(256);
    }

    bool build(QVector<Face> &faces) {
        faces.clear();
        edgeCount_.clear();
        faceSet_.clear();
        if (points_.size() < 3)
            return false;

        QQueue<BorderEdge> border;
        if (!findSeedTriangle(faces, border))
            return false;

        int guard = 0;
        while (!border.isEmpty() && faces.size() < maxTriangles_ && guard++ < maxTriangles_ * 12) {
            const BorderEdge edge = border.dequeue();
            if (edgeCount(edge.v1, edge.v2) != 1)
                continue;

            int v3 = -1;
            GridPoint newCenter;
            if (!findPivotVertex(edge, v3, newCenter))
                continue;

            Face tri{edge.v1, edge.v2, v3};
            addTriangle(tri, newCenter, faces, border);
        }
        return faces.size() >= 4;
    }

  private:
    bool findSeedTriangle(QVector<Face> &faces, QQueue<BorderEdge> &border) {
        const int n = points_.size();
        for (int i = 0; i < n; ++i) {
            index_.queryNearby(points_[i], radius_ * 2.5f, work_);
            for (int jj = 0; jj < work_.size(); ++jj) {
                const int j = work_[jj];
                if (j == i)
                    continue;
                for (int kk = jj + 1; kk < work_.size(); ++kk) {
                    const int k = work_[kk];
                    if (k == i || k == j)
                        continue;
                    GridPoint center;
                    if (!computeBallCenter(points_[i], points_[j], points_[k], radius_, center))
                        continue;
                    if (!ballIsEmpty(center, radius_, i, j, k, points_, index_))
                        continue;
                    Face tri{i, j, k};
                    if (addTriangle(tri, center, faces, border))
                        return true;
                }
            }
        }
        return false;
    }

    bool findPivotVertex(const BorderEdge &edge, int &outV3, GridPoint &outCenter) const {
        index_.queryNearby(points_[edge.v1], radius_ * 2.5f, work_);
        float bestScore = std::numeric_limits<float>::max();
        int best = -1;
        GridPoint bestCenter;
        for (int v3 : work_) {
            if (v3 == edge.v1 || v3 == edge.v2)
                continue;
            GridPoint center;
            if (!computeBallCenter(points_[edge.v1], points_[edge.v2], points_[v3], radius_, center))
                continue;
            if (!ballIsEmpty(center, radius_, edge.v1, edge.v2, v3, points_, index_))
                continue;
            const float score = center.distanceToPoint(edge.ballCenter);
            if (score < bestScore) {
                bestScore = score;
                best = v3;
                bestCenter = center;
            }
        }
        if (best < 0)
            return false;
        outV3 = best;
        outCenter = bestCenter;
        return true;
    }

    bool addTriangle(Face tri, const GridPoint &center, QVector<Face> &faces,
                     QQueue<BorderEdge> &border) {
        if (!orientTriangle(tri))
            return false;
        if (triangleMinAngleDeg(points_[tri.a], points_[tri.b], points_[tri.c]) < minAngleDeg_)
            return false;
        if (hasFace(tri))
            return false;
        if (faces.size() >= maxTriangles_)
            return false;

        markFace(tri);
        registerFaceEdges(tri);
        faces.append(tri);
        enqueueBorderEdges(tri, center, border);
        return true;
    }

    bool orientTriangle(Face &tri) const {
        const GridPoint &a = points_[tri.a];
        const GridPoint &b = points_[tri.b];
        const GridPoint &c = points_[tri.c];
        GridPoint fn = QVector3D::crossProduct(b - a, c - a);
        const float fnLen = fn.length();
        if (fnLen < 1e-12f)
            return false;
        fn /= fnLen;
        GridPoint avgN = normals_[tri.a] + normals_[tri.b] + normals_[tri.c];
        if (avgN.lengthSquared() < 1e-12f)
            return true;
        avgN.normalize();
        if (QVector3D::dotProduct(fn, avgN) < 0.0f)
            std::swap(tri.b, tri.c);
        return true;
    }

    void enqueueBorderEdges(const Face &tri, const GridPoint &center, QQueue<BorderEdge> &border) {
        auto tryEnqueue = [&](int a, int b) {
            if (edgeCount(a, b) == 1)
                border.enqueue({a, b, center});
        };
        tryEnqueue(tri.a, tri.b);
        tryEnqueue(tri.b, tri.c);
        tryEnqueue(tri.c, tri.a);
    }

    static quint64 edgeKey(int a, int b) {
        if (a > b)
            std::swap(a, b);
        return (static_cast<quint64>(static_cast<quint32>(a)) << 32) |
               static_cast<quint64>(static_cast<quint32>(b));
    }

    void registerFaceEdges(const Face &tri) {
        ++edgeCount_[edgeKey(tri.a, tri.b)];
        ++edgeCount_[edgeKey(tri.b, tri.c)];
        ++edgeCount_[edgeKey(tri.c, tri.a)];
    }

    int edgeCount(int a, int b) const { return edgeCount_.value(edgeKey(a, b), 0); }

    bool hasFace(const Face &f) const {
        int a = f.a;
        int b = f.b;
        int c = f.c;
        if (a > b)
            std::swap(a, b);
        if (b > c)
            std::swap(b, c);
        if (a > b)
            std::swap(a, b);
        return faceSet_.contains((static_cast<quint64>(a) << 42) |
                                 (static_cast<quint64>(b) << 21) | static_cast<quint64>(c));
    }

    void markFace(const Face &f) {
        int a = f.a;
        int b = f.b;
        int c = f.c;
        if (a > b)
            std::swap(a, b);
        if (b > c)
            std::swap(b, c);
        if (a > b)
            std::swap(a, b);
        faceSet_.insert((static_cast<quint64>(a) << 42) | (static_cast<quint64>(b) << 21) |
                        static_cast<quint64>(c));
    }

    const QVector<GridPoint> &points_;
    const QVector<GridPoint> &normals_;
    float radius_;
    int maxTriangles_;
    float minAngleDeg_;
    SpatialIndex index_;
    mutable QVector<int> work_;
    QHash<quint64, int> edgeCount_;
    QSet<quint64> faceSet_;
};

QVector<Face> subsampleFaces(const QVector<Face> &faces, int maxFaces) {
    if (faces.size() <= maxFaces)
        return faces;
    QVector<Face> out;
    out.reserve(maxFaces);
    const double step = static_cast<double>(faces.size()) / maxFaces;
    for (int i = 0; i < maxFaces; ++i)
        out.append(faces[static_cast<int>(i * step)]);
    return out;
}

QVector<Face> buildTangentPlaneMesh(const QVector<GridPoint> &points, const QVector<GridPoint> &normals,
                                    SpatialIndex &index, float maxEdge, int maxTriangles) {
    QVector<Face> faces;
    faces.reserve(std::min(maxTriangles, static_cast<int>(points.size() * 6)));
    QSet<quint64> faceKeys;
    QVector<int> neighbors;

    struct RingPoint {
        int idx;
        float angle;
    };
    QVector<RingPoint> ring;

    auto addFace = [&](Face f) {
        int a = f.a;
        int b = f.b;
        int c = f.c;
        if (a > b)
            std::swap(a, b);
        if (b > c)
            std::swap(b, c);
        if (a > b)
            std::swap(a, b);
        const quint64 key = (static_cast<quint64>(a) << 42) | (static_cast<quint64>(b) << 21) |
                            static_cast<quint64>(c);
        if (faceKeys.contains(key))
            return;
        faceKeys.insert(key);
        faces.append(f);
    };

    for (int i = 0; i < points.size() && faces.size() < maxTriangles; ++i) {
        index.queryNearby(points[i], maxEdge * 2.5f, neighbors);
        if (neighbors.size() < 4)
            continue;

        GridPoint n = normals[i];
        if (n.lengthSquared() < 1e-12f)
            n = GridPoint(0, 0, 1);
        else
            n.normalize();
        const GridPoint ref = std::abs(n.z()) < 0.9f ? GridPoint(0, 0, 1) : GridPoint(1, 0, 0);
        GridPoint u = QVector3D::crossProduct(n, ref);
        const float uLen = u.length();
        if (uLen < 1e-10f)
            continue;
        u /= uLen;
        const GridPoint v = QVector3D::crossProduct(n, u);

        ring.clear();
        for (int j : neighbors) {
            if (j == i)
                continue;
            const float dist = points[i].distanceToPoint(points[j]);
            if (dist < 1e-8f || dist > maxEdge * 2.2f)
                continue;
            const GridPoint rel = points[j] - points[i];
            ring.append({j, std::atan2(QVector3D::dotProduct(rel, v), QVector3D::dotProduct(rel, u))});
        }
        if (ring.size() < 3)
            continue;
        std::sort(ring.begin(), ring.end(),
                  [](const RingPoint &a, const RingPoint &b) { return a.angle < b.angle; });

        for (int t = 0; t < ring.size() && faces.size() < maxTriangles; ++t) {
            const int j = ring[t].idx;
            const int k = ring[(t + 1) % ring.size()].idx;
            if (points[j].distanceToPoint(points[k]) > maxEdge * 2.5f)
                continue;
            Face tri{i, j, k};
            const GridPoint fn = QVector3D::crossProduct(points[j] - points[i], points[k] - points[i]);
            if (QVector3D::dotProduct(fn, n) < 0.0f)
                std::swap(tri.b, tri.c);
            if (triangleMinAngleDeg(points[tri.a], points[tri.b], points[tri.c]) < 5.0f)
                continue;
            addFace(tri);
        }
    }
    return faces;
}

void appendEdge(QSet<QPair<int, int>> &set, QVector<Edge> &edges, int a, int b) {
    if (a == b)
        return;
    const int lo = std::min(a, b);
    const int hi = std::max(a, b);
    const QPair<int, int> key(lo, hi);
    if (set.contains(key))
        return;
    set.insert(key);
    edges.append({lo, hi});
}

void edgesFromFaces(const QVector<Face> &faces, QVector<Edge> &edges) {
    QSet<QPair<int, int>> set;
    edges.clear();
    for (const auto &f : faces) {
        appendEdge(set, edges, f.a, f.b);
        appendEdge(set, edges, f.b, f.c);
        appendEdge(set, edges, f.c, f.a);
    }
}

MeshBuildResult buildMeshPipeline(const QVector<GridPoint> &inputPoints, const PcMeshOptions &options) {
    MeshBuildResult result;
    if (inputPoints.size() < 30) {
        result.message = QStringLiteral("Mesh 重建失败：点数过少（至少 30 点）。");
        return result;
    }

    QVector<GridPoint> points = subsampleStride(inputPoints, options.maxInputPoints);

    GridPoint minP = points[0];
    GridPoint maxP = points[0];
    for (const auto &p : points) {
        minP.setX(std::min(minP.x(), p.x()));
        minP.setY(std::min(minP.y(), p.y()));
        minP.setZ(std::min(minP.z(), p.z()));
        maxP.setX(std::max(maxP.x(), p.x()));
        maxP.setY(std::max(maxP.y(), p.y()));
        maxP.setZ(std::max(maxP.z(), p.z()));
    }
    const float maxSpan =
        std::max({maxP.x() - minP.x(), maxP.y() - minP.y(), maxP.z() - minP.z(), 1.0f});
    const float coarseCell =
        maxSpan / std::cbrt(static_cast<float>(std::max(static_cast<int>(points.size()), 1)));

    SpatialIndex coarseIndex;
    coarseIndex.build(points, coarseCell);
    const float spacing =
        estimateAverageSpacing(points, coarseIndex, coarseCell * 4.0f, 400);
    const float maxEdge = spacing * 2.2f;

    if (points.size() > options.maxInputPoints) {
        points = voxelCentroidDownsample(points, spacing * 0.85f);
        if (points.size() > options.maxInputPoints)
            points = subsampleStride(points, options.maxInputPoints);
    }
    if (points.size() < 30) {
        result.message = QStringLiteral("Mesh 重建失败：降采样后点数不足。");
        return result;
    }

    SpatialIndex index;
    index.build(points, spacing);
    const QVector<GridPoint> normals =
        estimateNormals(points, index, maxEdge, options.normalNeighbors);

    QVector<Face> faces;
    float usedRadius = spacing;
    QString method = QStringLiteral("Ball Pivoting");
    const std::array<float, 4> radiusScales = {0.85f, 1.0f, 1.15f, 1.35f};
    for (const float scale : radiusScales) {
        const float radius =
            options.ballRadiusScale > 0.0f ? options.ballRadiusScale : spacing * scale;
        BallPivotingMesher mesher(points, normals, radius, options.maxTriangles,
                                  options.minTriangleAngleDeg);
        QVector<Face> trial;
        if (mesher.build(trial) && trial.size() >= 12) {
            faces = std::move(trial);
            usedRadius = radius;
            break;
        }
    }

    if (faces.size() < 12) {
        faces = buildTangentPlaneMesh(points, normals, index, maxEdge, options.maxTriangles);
        method = QStringLiteral("切平面三角网");
        usedRadius = maxEdge;
    }

    if (faces.isEmpty()) {
        result.message = QStringLiteral(
            "Mesh 重建失败：未能从点云生成表面三角网（平均点距 %1）。"
            "请检查点云是否过稀，或手动设置更小的球半径。")
                             .arg(spacing, 0, 'g', 4);
        return result;
    }

    if (faces.size() > options.maxTriangles)
        faces = subsampleFaces(faces, options.maxTriangles);

    result.vertices = points;
    result.faces = faces;
    edgesFromFaces(faces, result.edges);
    result.ok = true;
    result.message =
        QStringLiteral("%1：输入 %2 点 → 顶点 %3，三角面 %4（特征尺度 %5）")
            .arg(method)
            .arg(inputPoints.size())
            .arg(points.size())
            .arg(faces.size())
            .arg(usedRadius, 0, 'g', 4);
    return result;
}

bool loadPlyMesh(const QString &path, QVector<GridPoint> &vertices, QVector<Face> &faces) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = file.readAll();
    file.close();

    const int headerEnd = data.indexOf("end_header");
    if (headerEnd < 0)
        return false;
    const int headerBytes = data.indexOf('\n', headerEnd) + 1;

    bool ascii = false;
    int vertexCount = 0;
    int faceCount = 0;
    int vertexProps = 0;
    bool inVertex = false;
    QTextStream headerStream(data.left(headerBytes));
    while (!headerStream.atEnd()) {
        const QString line = headerStream.readLine().trimmed();
        if (line.startsWith(QLatin1String("element vertex"))) {
            vertexCount = line.section(QLatin1Char(' '), 2, 2).toInt();
            inVertex = true;
        } else if (line.startsWith(QLatin1String("element face"))) {
            faceCount = line.section(QLatin1Char(' '), 2, 2).toInt();
            inVertex = false;
        } else if (inVertex && line.startsWith(QLatin1String("property "))) {
            ++vertexProps;
        } else if (line.contains(QLatin1String("format ascii"))) {
            ascii = true;
        }
    }
    if (vertexCount <= 0)
        return false;

    vertices.clear();
    faces.clear();
    if (ascii) {
        QTextStream in(data);
        while (!in.atEnd()) {
            if (in.readLine().trimmed() == QLatin1String("end_header"))
                break;
        }
        vertices.reserve(vertexCount);
        for (int i = 0; i < vertexCount && !in.atEnd(); ++i) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty()) {
                --i;
                continue;
            }
            const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() >= 3)
                vertices.append(
                    GridPoint(parts[0].toFloat(), parts[1].toFloat(), parts[2].toFloat()));
        }
        for (int i = 0; i < faceCount && !in.atEnd(); ++i) {
            const QString line = in.readLine().trimmed();
            if (line.isEmpty()) {
                --i;
                continue;
            }
            const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() < 4)
                continue;
            const int n = parts[0].toInt();
            if (n < 3)
                continue;
            const int i0 = parts[1].toInt();
            for (int t = 1; t < n - 1; ++t)
                faces.append({i0, parts[1 + t].toInt(), parts[2 + t].toInt()});
        }
    } else {
        if (vertexProps < 3)
            return false;
        const int vertexSize = vertexProps * static_cast<int>(sizeof(float));
        int offset = headerBytes;
        vertices.reserve(vertexCount);
        for (int i = 0; i < vertexCount; ++i) {
            if (offset + vertexSize > data.size())
                break;
            const float *f = reinterpret_cast<const float *>(data.constData() + offset);
            vertices.append(GridPoint(f[0], f[1], f[2]));
            offset += vertexSize;
        }
        for (int i = 0; i < faceCount; ++i) {
            if (offset >= data.size())
                break;
            const quint8 n = static_cast<quint8>(data[offset++]);
            if (n < 3 || offset + n * 4 > data.size())
                break;
            const int i0 = *reinterpret_cast<const qint32 *>(data.constData() + offset);
            for (int t = 1; t < n - 1; ++t) {
                const int ib = *reinterpret_cast<const qint32 *>(data.constData() + offset + t * 4);
                const int ic =
                    *reinterpret_cast<const qint32 *>(data.constData() + offset + (t + 1) * 4);
                faces.append({i0, ib, ic});
            }
            offset += n * 4;
        }
    }
    return !vertices.isEmpty();
}

} // namespace

MeshBuildResult reconstructFromPointCloud(const QVector<GridPoint> &points,
                                          const PcMeshOptions &options) {
    return buildMeshPipeline(points, options);
}

MeshBuildResult extractMeshFromPlyFile(const QString &path, const PcMeshOptions &options) {
    MeshBuildResult result;
    QVector<GridPoint> vertices;
    QVector<Face> faces;
    if (!loadPlyMesh(path, vertices, faces)) {
        result.message = QStringLiteral("PLY 读取失败：%1").arg(path);
        return result;
    }
    if (!faces.isEmpty()) {
        result.vertices = vertices;
        result.faces = faces;
        edgesFromFaces(faces, result.edges);
        result.ok = true;
        result.loadedFromPlyFaces = true;
        result.message = QStringLiteral("已从 PLY 直接读取 Mesh：V=%1 E=%2 F=%3")
                             .arg(vertices.size())
                             .arg(result.edges.size())
                             .arg(faces.size());
        return result;
    }
    result = buildMeshPipeline(vertices, options);
    if (result.ok) {
        result.message = QStringLiteral("PLY 点云 Mesh 重建：%1").arg(result.message);
    }
    return result;
}

} // namespace rs::pcmesh
