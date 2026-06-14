#include "rs/Scene3DWidget.h"

#include <GL/gl.h>
#include <QShowEvent>
#include <algorithm>
#include <cmath>

namespace {

QVector<QVector3D> subsamplePoints(const QVector<QVector3D> &src, int maxCount) {
    if (src.size() <= maxCount)
        return src;
    QVector<QVector3D> out;
    out.reserve(maxCount);
    const double step = static_cast<double>(src.size()) / maxCount;
    for (int i = 0; i < maxCount; ++i)
        out.append(src[static_cast<int>(i * step)]);
    return out;
}

QVector<rs::Face> subsampleFaces(const QVector<rs::Face> &faces, int maxCount) {
    if (faces.size() <= maxCount)
        return faces;
    QVector<rs::Face> out;
    out.reserve(maxCount);
    const double step = static_cast<double>(faces.size()) / maxCount;
    for (int i = 0; i < maxCount; ++i)
        out.append(faces[static_cast<int>(i * step)]);
    return out;
}

void computeBounds(const QVector<QVector3D> &points, const QVector<QVector3D> &meshVerts,
                   QVector3D &center, float &halfExtent) {
    float cx = 0, cy = 0, cz = 0;
    int count = 0;

    auto addPoint = [&](float x, float y, float z) {
        cx += x;
        cy += y;
        cz += z;
        ++count;
    };

    for (const auto &p : points)
        addPoint(p.x(), p.y(), p.z());
    for (const auto &v : meshVerts)
        addPoint(v.x(), v.y(), v.z());

    if (count == 0) {
        center = QVector3D(0, 0, 0);
        halfExtent = 1.0f;
        return;
    }

    cx /= count;
    cy /= count;
    cz /= count;
    center = QVector3D(cx, cy, cz);

    float maxDist = 0;
    for (const auto &p : points) {
        const float dx = p.x() - cx;
        const float dy = p.y() - cy;
        const float dz = p.z() - cz;
        maxDist = std::max(maxDist, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    for (const auto &v : meshVerts) {
        const float dx = v.x() - cx;
        const float dy = v.y() - cy;
        const float dz = v.z() - cz;
        maxDist = std::max(maxDist, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    halfExtent = maxDist > 0 ? maxDist : 1.0f;
}

QVector3D faceNormal(const QVector3D &a, const QVector3D &b, const QVector3D &c) {
    QVector3D n = QVector3D::crossProduct(b - a, c - a);
    const float len = n.length();
    if (len > 1e-10f)
        n /= len;
    return n;
}

} // namespace

Scene3DWidget::Scene3DWidget(QWidget *parent) : QOpenGLWidget(parent) {}

Scene3DWidget::~Scene3DWidget() {
    makeCurrent();
    pointVbo_.destroy();
    meshVbo_.destroy();
    doneCurrent();
}

void Scene3DWidget::setPoints(const QVector<QVector3D> &points) {
    meshVertices_.clear();
    meshFaces_.clear();
    m_points = subsamplePoints(points, kMaxDisplayPoints);
    updateBounds();
    invalidateGpu();
    update();
}

void Scene3DWidget::setMesh(const QVector<QVector3D> &vertices, const QVector<rs::Face> &faces) {
    meshVertices_ = vertices;
    meshFaces_ = subsampleFaces(faces, kMaxDisplayFaces);
    drawWireframe_ = meshFaces_.size() <= kWireframeFaceLimit;
    if (!vertices.isEmpty())
        m_points.clear();
    updateBounds();
    invalidateGpu();
    update();
}

void Scene3DWidget::setDem(const rs::DemLayer &dem, int maxGrid) {
    m_points.clear();
    meshVertices_.clear();
    meshFaces_.clear();

    const int w = dem.width();
    const int h = dem.height();
    if (w <= 0 || h <= 0)
        return;

    const int stepX = std::max(1, w / maxGrid);
    const int stepY = std::max(1, h / maxGrid);
    const auto &elevs = dem.elevations();
    const auto gt = dem.geoTransform();

    float zMin = elevs[0], zMax = elevs[0];
    for (float z : elevs) {
        zMin = std::min(zMin, z);
        zMax = std::max(zMax, z);
    }
    const float zScale = (zMax - zMin) > 1e-6f ? 1.0f / (zMax - zMin) : 1.0f;

    const int cols = (w + stepX - 1) / stepX;
    const int rows = (h + stepY - 1) / stepY;
    meshVertices_.reserve(cols * rows);

    for (int gy = 0, y = 0; gy < rows; ++gy, y += stepY) {
        for (int gx = 0, x = 0; gx < cols; ++gx, x += stepX) {
            const int sx = std::min(x, w - 1);
            const int sy = std::min(y, h - 1);
            const float z = elevs[sy * w + sx];
            const float wx = static_cast<float>(gt[0] + sx * gt[1] + sy * gt[2]);
            const float wy = static_cast<float>(gt[3] + sx * gt[4] + sy * gt[5]);
            meshVertices_.append(QVector3D(wx, wy, (z - zMin) * zScale * 100.0f));
        }
    }

    for (int gy = 0; gy < rows - 1; ++gy) {
        for (int gx = 0; gx < cols - 1; ++gx) {
            const int i0 = gy * cols + gx;
            const int i1 = i0 + 1;
            const int i2 = i0 + cols;
            const int i3 = i2 + 1;
            meshFaces_.append({i0, i1, i2});
            meshFaces_.append({i1, i3, i2});
        }
    }

    drawWireframe_ = meshFaces_.size() <= kWireframeFaceLimit;
    updateBounds();
    invalidateGpu();
    update();
}

void Scene3DWidget::clearData() {
    m_points.clear();
    meshVertices_.clear();
    meshFaces_.clear();
    drawWireframe_ = false;
    updateBounds();
    invalidateGpu();
    update();
}

void Scene3DWidget::updateBounds() {
    computeBounds(m_points, meshVertices_, m_center, m_halfExtent);
}

void Scene3DWidget::invalidateGpu() {
    gpuDirty_ = true;
}

void Scene3DWidget::rebuildGpuBuffers() {
    const float scale = (m_halfExtent > 0) ? (1.0f / m_halfExtent) : 1.0f;

    if (!m_points.isEmpty()) {
        if (!pointVbo_.isCreated())
            pointVbo_.create();
        pointVbo_.bind();
        QVector<float> data;
        data.reserve(m_points.size() * 3);
        for (const auto &p : m_points) {
            data.append((p.x() - m_center.x()) * scale);
            data.append((p.y() - m_center.y()) * scale);
            data.append((p.z() - m_center.z()) * scale);
        }
        pointDrawCount_ = m_points.size();
        pointVbo_.allocate(data.constData(), data.size() * static_cast<int>(sizeof(float)));
        pointVbo_.release();
    } else {
        pointDrawCount_ = 0;
    }

    const bool hasMeshFaces = !meshVertices_.isEmpty() && !meshFaces_.isEmpty();
    if (hasMeshFaces) {
        if (!meshVbo_.isCreated())
            meshVbo_.create();
        meshVbo_.bind();
        QVector<float> data;
        data.reserve(meshFaces_.size() * 18);
        for (const auto &face : meshFaces_) {
            if (face.a < 0 || face.a >= meshVertices_.size() || face.b < 0 ||
                face.b >= meshVertices_.size() || face.c < 0 || face.c >= meshVertices_.size())
                continue;
            const QVector3D &va = meshVertices_[face.a];
            const QVector3D &vb = meshVertices_[face.b];
            const QVector3D &vc = meshVertices_[face.c];
            const QVector3D n = faceNormal(va, vb, vc);
            const float shade = 0.4f + 0.6f * std::abs(n.z());
            const float cr = 1.0f * shade;
            const float cg = 0.55f * shade;
            const float cb = 0.75f * shade;
            const QVector3D verts[3] = {va, vb, vc};
            for (const auto &v : verts) {
                data.append((v.x() - m_center.x()) * scale);
                data.append((v.y() - m_center.y()) * scale);
                data.append((v.z() - m_center.z()) * scale);
                data.append(cr);
                data.append(cg);
                data.append(cb);
            }
        }
        meshTriangleVertexCount_ = data.size() / 6;
        meshVbo_.allocate(data.constData(), data.size() * static_cast<int>(sizeof(float)));
        meshVbo_.release();
    } else {
        meshTriangleVertexCount_ = 0;
    }

    gpuDirty_ = false;
}

void Scene3DWidget::showEvent(QShowEvent *event) {
    QOpenGLWidget::showEvent(event);
    if (!m_points.isEmpty() || !meshVertices_.isEmpty())
        update();
}

void Scene3DWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(1.0f, 0.94f, 0.96f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glPointSize(2.0f);
}

void Scene3DWidget::paintGL() {
    const bool hasPoints = pointDrawCount_ > 0;
    const bool hasMeshFaces = meshTriangleVertexCount_ > 0;
    const bool hasMeshVerticesOnly = !meshVertices_.isEmpty() && meshFaces_.isEmpty();

    glClearColor(1.0f, 0.94f, 0.96f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (gpuDirty_)
        rebuildGpuBuffers();

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float aspect = static_cast<float>(width()) / std::max(height(), 1);
    const float zFar = m_halfExtent * 20.0f + 10.0f;
    const float zNear = 0.01f;
    float projMatrix[16];
    const float f = 1.0f / std::tan(45.0f * 3.14159265f / 360.0f);
    projMatrix[0] = f / aspect;
    projMatrix[1] = 0;
    projMatrix[2] = 0;
    projMatrix[3] = 0;
    projMatrix[4] = 0;
    projMatrix[5] = f;
    projMatrix[6] = 0;
    projMatrix[7] = 0;
    projMatrix[8] = 0;
    projMatrix[9] = 0;
    projMatrix[10] = (zFar + zNear) / (zNear - zFar);
    projMatrix[11] = -1;
    projMatrix[12] = 0;
    projMatrix[13] = 0;
    projMatrix[14] = 2 * zFar * zNear / (zNear - zFar);
    projMatrix[15] = 0;
    glMultMatrixf(projMatrix);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -3.0f * m_zoom);
    glRotatef(m_rotX, 1.0f, 0.0f, 0.0f);
    glRotatef(m_rotY, 0.0f, 1.0f, 0.0f);

    glDisable(GL_LIGHTING);
    glBegin(GL_LINES);
    const float axisLen = 1.5f;
    glColor3f(1.0f, 0.2f, 0.2f);
    glVertex3f(-axisLen, 0.0f, 0.0f);
    glVertex3f(axisLen, 0.0f, 0.0f);
    glColor3f(0.2f, 1.0f, 0.2f);
    glVertex3f(0.0f, -axisLen, 0.0f);
    glVertex3f(0.0f, axisLen, 0.0f);
    glColor3f(0.2f, 0.2f, 1.0f);
    glVertex3f(0.0f, 0.0f, -axisLen);
    glVertex3f(0.0f, 0.0f, axisLen);
    glEnd();

    if (hasMeshFaces && meshVbo_.isCreated()) {
        glDisable(GL_LIGHTING);
        meshVbo_.bind();
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), nullptr);
        glColorPointer(3, GL_FLOAT, 6 * sizeof(float),
                        reinterpret_cast<const void *>(3 * sizeof(float)));
        glDrawArrays(GL_TRIANGLES, 0, meshTriangleVertexCount_);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        meshVbo_.release();

        if (drawWireframe_) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            glLineWidth(1.0f);
            glColor3f(0.3f, 0.3f, 0.35f);
            meshVbo_.bind();
            glEnableClientState(GL_VERTEX_ARRAY);
            glDisableClientState(GL_COLOR_ARRAY);
            glVertexPointer(3, GL_FLOAT, 6 * sizeof(float), nullptr);
            glDrawArrays(GL_TRIANGLES, 0, meshTriangleVertexCount_);
            glDisableClientState(GL_VERTEX_ARRAY);
            meshVbo_.release();
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
    } else if (hasMeshVerticesOnly) {
        glPointSize(3.0f);
        glColor3f(0.85f, 0.35f, 0.55f);
        glBegin(GL_POINTS);
        const float scale = (m_halfExtent > 0) ? (1.0f / m_halfExtent) : 1.0f;
        for (const auto &v : meshVertices_) {
            glVertex3f((v.x() - m_center.x()) * scale, (v.y() - m_center.y()) * scale,
                       (v.z() - m_center.z()) * scale);
        }
        glEnd();
        glPointSize(2.0f);
    }

    if (hasPoints && pointVbo_.isCreated()) {
        glColor3f(0.8f, 0.2f, 0.5f);
        pointVbo_.bind();
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, nullptr);
        glDrawArrays(GL_POINTS, 0, pointDrawCount_);
        glDisableClientState(GL_VERTEX_ARRAY);
        pointVbo_.release();
    }
}

void Scene3DWidget::mousePressEvent(QMouseEvent *event) {
    m_lastPos = event->pos();
}

void Scene3DWidget::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        m_rotY += (event->pos().x() - m_lastPos.x()) * 0.5f;
        m_rotX += (event->pos().y() - m_lastPos.y()) * 0.5f;
        m_rotX = std::clamp(m_rotX, -90.0f, 90.0f);
        update();
    }
    m_lastPos = event->pos();
}

void Scene3DWidget::fitToBounds() {
    updateBounds();
    m_zoom = 1.0f;
    m_rotX = 0;
    m_rotY = 0;
    invalidateGpu();
    update();
}

void Scene3DWidget::wheelEvent(QWheelEvent *event) {
    const float delta = event->angleDelta().y() / 120.0f;
    m_zoom *= (1.0f - delta * 0.08f);
    m_zoom = std::clamp(m_zoom, 0.1f, 10.0f);
    update();
}
