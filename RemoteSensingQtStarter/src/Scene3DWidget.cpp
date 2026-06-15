#include "rs/Scene3DWidget.h"
#include <GL/gl.h>
#include <QMouseEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

static QVector3D faceNormal(const QVector3D &a, const QVector3D &b, const QVector3D &c) {
    QVector3D ab = b - a;
    QVector3D ac = c - a;
    QVector3D n = QVector3D::crossProduct(ab, ac);
    float len = n.length();
    if (len > 1e-10f) n /= len;
    return n;
}

static void computeBounds(const QVector<QVector3D> &points,
                          const QVector<QVector3D> &meshVerts,
                          QVector3D &center, float &halfExtent) {
    float cx = 0, cy = 0, cz = 0;
    int count = 0;

    auto addPoint = [&](float x, float y, float z) {
        cx += x; cy += y; cz += z;
        ++count;
    };

    for (const auto &p : points) addPoint(p.x(), p.y(), p.z());
    for (const auto &v : meshVerts) addPoint(v.x(), v.y(), v.z());

    if (count == 0) {
        center = QVector3D(0, 0, 0);
        halfExtent = 1.0f;
        return;
    }

    cx /= count; cy /= count; cz /= count;
    center = QVector3D(cx, cy, cz);

    float maxDist = 0;
    for (const auto &p : points) {
        float dx = p.x() - cx, dy = p.y() - cy, dz = p.z() - cz;
        maxDist = std::max(maxDist, std::sqrt(dx*dx + dy*dy + dz*dz));
    }
    for (const auto &v : meshVerts) {
        float dx = v.x() - cx, dy = v.y() - cy, dz = v.z() - cz;
        maxDist = std::max(maxDist, std::sqrt(dx*dx + dy*dy + dz*dz));
    }
    halfExtent = maxDist > 0 ? maxDist : 1.0f;
}

Scene3DWidget::Scene3DWidget(QWidget *parent) : QOpenGLWidget(parent) {}

Scene3DWidget::~Scene3DWidget() {
    makeCurrent();
    if (m_meshDList) {
        glDeleteLists(m_meshDList, 1);
        m_meshDList = 0;
    }
    m_dlistValid = false;
}

void Scene3DWidget::markDlistDirty() {
    m_dlistValid = false;
}

void Scene3DWidget::setPoints(const QVector<QVector3D> &points) {
    m_points = points;
    if (!points.isEmpty()) {
        meshVertices_.clear();
        meshFaces_.clear();
    }
    markDlistDirty();
    update();
}

void Scene3DWidget::setMesh(const QVector<QVector3D> &vertices, const QVector<rs::Face> &faces) {
    meshVertices_ = vertices;
    meshFaces_ = faces;
    if (!vertices.isEmpty()) {
        m_points.clear();
    }
    markDlistDirty();
    update();
}

void Scene3DWidget::setDem(const rs::DemLayer &dem, int maxGrid) {
    m_points.clear();
    meshVertices_.clear();
    meshFaces_.clear();

    const int w = dem.width();
    const int h = dem.height();
    if (w <= 0 || h <= 0) return;

    const int stepX = std::max(1, w / maxGrid);
    const int stepY = std::max(1, h / maxGrid);
    const auto &elevs = dem.elevations();
    const auto gt = dem.geoTransform();

    float zMin = elevs[0], zMax = elevs[0];
    for (float z : elevs) { zMin = std::min(zMin, z); zMax = std::max(zMax, z); }
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
    markDlistDirty();
    update();
}

void Scene3DWidget::clearData() {
    m_points.clear();
    meshVertices_.clear();
    meshFaces_.clear();
    markDlistDirty();
    update();
}

void Scene3DWidget::showEvent(QShowEvent *event) {
    QOpenGLWidget::showEvent(event);
    if (!m_points.isEmpty() || (!meshVertices_.isEmpty() && !meshFaces_.isEmpty())) {
        update();
    }
}

void Scene3DWidget::initializeGL() {
    glClearColor(1.0f, 0.94f, 0.96f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);
    GLfloat lightPos[]    = {1.0f, 1.0f, 1.0f, 0.0f};
    GLfloat lightAmbient[] = {0.3f, 0.3f, 0.3f, 1.0f};
    GLfloat lightDiffuse[] = {0.8f, 0.8f, 0.8f, 1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmbient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiffuse);
    glPointSize(2.0f);

    // OpenGL context 可能被重建，让显示列表在下次 paintGL 时重新编译
    m_dlistValid = false;
}

void Scene3DWidget::rebuildDisplayList() {
    // 删除旧的显示列表
    if (m_meshDList) {
        glDeleteLists(m_meshDList, 1);
        m_meshDList = 0;
    }

    const bool hasMeshFaces = !meshVertices_.isEmpty() && !meshFaces_.isEmpty();
    const bool hasMeshVerticesOnly = !meshVertices_.isEmpty() && meshFaces_.isEmpty();

    if (!hasMeshFaces && !hasMeshVerticesOnly) {
    // 没有 mesh 数据也要缓存包围盒，这样 paintGL 不会重复 computeBounds
        computeBounds(m_points, meshVertices_, m_cachedCenter, m_cachedHalfExtent);
        m_dlistValid = true;
        return;
    }

    // 预计算法线（只在重建时做一次，而不是每帧）
    QVector<QVector3D> normals;
    if (hasMeshFaces) {
        normals.reserve(meshFaces_.size());
        for (const auto &face : meshFaces_) {
            if (face.a < 0 || face.a >= meshVertices_.size() ||
                face.b < 0 || face.b >= meshVertices_.size() ||
                face.c < 0 || face.c >= meshVertices_.size()) {
                normals.append(QVector3D(0, 0, 1));
                continue;
            }
            normals.append(faceNormal(meshVertices_[face.a],
                                      meshVertices_[face.b],
                                      meshVertices_[face.c]));
        }
    }

    // 缓存包围盒
    computeBounds(m_points, meshVertices_, m_cachedCenter, m_cachedHalfExtent);

    const float scale = (m_cachedHalfExtent > 0) ? (1.0f / m_cachedHalfExtent) : 1.0f;
    const float cx = m_cachedCenter.x();
    const float cy = m_cachedCenter.y();
    const float cz = m_cachedCenter.z();

    // 编译显示列表 —— 把整个 mesh 几何体一次性录制到 GPU 端
    m_meshDList = glGenLists(1);
    glNewList(m_meshDList, GL_COMPILE);

    if (hasMeshFaces) {
    // 第一遍：填充三角形 + 光照
        glEnable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glBegin(GL_TRIANGLES);
        for (int i = 0; i < meshFaces_.size(); ++i) {
            const auto &face = meshFaces_[i];
            if (face.a < 0 || face.a >= meshVertices_.size() ||
                face.b < 0 || face.b >= meshVertices_.size() ||
                face.c < 0 || face.c >= meshVertices_.size())
                continue;

            const QVector3D &va = meshVertices_[face.a];
            const QVector3D &vb = meshVertices_[face.b];
            const QVector3D &vc = meshVertices_[face.c];
            const QVector3D &n = normals[i];

            float shade = 0.4f + 0.6f * std::abs(n.z());
            glColor3f(1.0f * shade, 0.55f * shade, 0.75f * shade);
            glNormal3f(n.x(), n.y(), n.z());
            glVertex3f((va.x() - cx) * scale, (va.y() - cy) * scale, (va.z() - cz) * scale);
            glVertex3f((vb.x() - cx) * scale, (vb.y() - cy) * scale, (vb.z() - cz) * scale);
            glVertex3f((vc.x() - cx) * scale, (vc.y() - cy) * scale, (vc.z() - cz) * scale);
        }
        glEnd();

    // 第二遍：线框轮廓
        glDisable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glLineWidth(1.0f);
        glColor3f(0.3f, 0.3f, 0.35f);
        glBegin(GL_TRIANGLES);
        for (const auto &face : meshFaces_) {
            if (face.a < 0 || face.a >= meshVertices_.size() ||
                face.b < 0 || face.b >= meshVertices_.size() ||
                face.c < 0 || face.c >= meshVertices_.size())
                continue;
            const QVector3D &va = meshVertices_[face.a];
            const QVector3D &vb = meshVertices_[face.b];
            const QVector3D &vc = meshVertices_[face.c];
            glVertex3f((va.x() - cx) * scale, (va.y() - cy) * scale, (va.z() - cz) * scale);
            glVertex3f((vb.x() - cx) * scale, (vb.y() - cy) * scale, (vb.z() - cz) * scale);
            glVertex3f((vc.x() - cx) * scale, (vc.y() - cy) * scale, (vc.z() - cz) * scale);
        }
        glEnd();
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    } else if (hasMeshVerticesOnly) {
        glDisable(GL_LIGHTING);
        glPointSize(3.0f);
        glBegin(GL_POINTS);
        glColor3f(0.85f, 0.35f, 0.55f);
        for (const auto &v : meshVertices_) {
            glVertex3f((v.x() - cx) * scale, (v.y() - cy) * scale, (v.z() - cz) * scale);
        }
        glEnd();
        glPointSize(2.0f);
    }

    glEndList();
    m_dlistValid = true;
}

void Scene3DWidget::paintGL() {
    // 确保显示列表已编译（数据变更或 context 重建后自动重建）
    if (!m_dlistValid) {
        rebuildDisplayList();
    }

    const bool hasPoints = !m_points.isEmpty();
    const bool hasMesh = m_meshDList != 0;

    glClearColor(1.0f, 0.94f, 0.96f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 使用缓存的包围盒
    const float halfExtent = m_cachedHalfExtent;
    const QVector3D &center = m_cachedCenter;

    // 投影矩阵
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float aspect = static_cast<float>(width()) / std::max(height(), 1);
    const float zFar = halfExtent * 20.0f + 10.0f;
    const float zNear = 0.01f;
    float projMatrix[16] = {};
    const float f = 1.0f / std::tan(45.0f * 3.14159265f / 360.0f);
    projMatrix[0]  = f / aspect;
    projMatrix[5]  = f;
    projMatrix[10] = (zFar + zNear) / (zNear - zFar);
    projMatrix[11] = -1;
    projMatrix[14] = 2 * zFar * zNear / (zNear - zFar);
    glMultMatrixf(projMatrix);

    // 模型视图矩阵
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    const float scale = (halfExtent > 0) ? (1.0f / halfExtent) : 1.0f;

    glTranslatef(0.0f, 0.0f, -3.0f * m_zoom);
    glRotatef(m_rotX, 1.0f, 0.0f, 0.0f);
    glRotatef(m_rotY, 0.0f, 1.0f, 0.0f);

    // 坐标轴
    glDisable(GL_LIGHTING);
    glBegin(GL_LINES);
    const float axisLen = halfExtent * 1.5f;
    glColor3f(1.0f, 0.2f, 0.2f);
    glVertex3f((center.x() - axisLen) * scale, center.y() * scale, center.z() * scale);
    glVertex3f((center.x() + axisLen) * scale, center.y() * scale, center.z() * scale);
    glColor3f(0.2f, 1.0f, 0.2f);
    glVertex3f(center.x() * scale, (center.y() - axisLen) * scale, center.z() * scale);
    glVertex3f(center.x() * scale, (center.y() + axisLen) * scale, center.z() * scale);
    glColor3f(0.2f, 0.3f, 1.0f);
    glVertex3f(center.x() * scale, center.y() * scale, (center.z() - axisLen) * scale);
    glVertex3f(center.x() * scale, center.y() * scale, (center.z() + axisLen) * scale);
    glEnd();

    // 通过一次 glCallList 绘制整个 mesh（GPU 加速）
    if (hasMesh) {
        glCallList(m_meshDList);
    }

    // 点云（点云通常较小，直接绘制即可）
    if (hasPoints) {
        glEnable(GL_LIGHTING);
        glBegin(GL_POINTS);
        glColor3f(0.8f, 0.2f, 0.5f);
        for (const auto &p : m_points) {
            glVertex3f((p.x() - center.x()) * scale,
                       (p.y() - center.y()) * scale,
                       (p.z() - center.z()) * scale);
        }
        glEnd();
    }
}

void Scene3DWidget::mousePressEvent(QMouseEvent *event) { m_lastPos = event->pos(); }

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
    // 用缓存的包围盒（如果有效），否则重新计算
    if (!m_dlistValid) {
        computeBounds(m_points, meshVertices_, m_cachedCenter, m_cachedHalfExtent);
    }
    m_center = m_cachedCenter;
    m_halfExtent = m_cachedHalfExtent;
    m_zoom = 1.0f;
    m_rotX = 0;
    m_rotY = 0;
    update();
}

void Scene3DWidget::wheelEvent(QWheelEvent *event) {
    const float delta = event->angleDelta().y() / 120.0f;
    m_zoom *= (1.0f - delta * 0.08f);
    m_zoom = std::clamp(m_zoom, 0.1f, 10.0f);
    update();
}
