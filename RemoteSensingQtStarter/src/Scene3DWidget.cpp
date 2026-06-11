#include "rs/Scene3DWidget.h"
#include <GL/gl.h>
#include <QMouseEvent>
#include <QShowEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
//2026.6.6
static void buildPerspectiveMatrix(float fovY, float aspect, float zNear, float zFar, float m[16]) {
    const float f = 1.0f / std::tan(fovY * 3.14159265f / 360.0f);
    // OpenGL 列优先矩阵
    m[0] = f / aspect;
    m[1] = 0;
    m[2] = 0;
    m[3] = 0;
    m[4] = 0;
    m[5] = f;
    m[6] = 0;
    m[7] = 0;
    m[8] = 0;
    m[9] = 0;
    m[10] = (zFar + zNear) / (zNear - zFar);
    m[11] = -1;
    m[12] = 0;
    m[13] = 0;
    m[14] = 2 * zFar * zNear / (zNear - zFar);
    m[15] = 0;
}

Scene3DWidget::Scene3DWidget(QWidget *parent) : QOpenGLWidget(parent) {}

void Scene3DWidget::setPoints(const QVector<QVector3D> &points) {
    m_points = points;
    if (!points.isEmpty()) {
        meshVertices_.clear();
        meshFaces_.clear();
    }
    update();
}

void Scene3DWidget::setMesh(const QVector<QVector3D> &vertices, const QVector<rs::Face> &faces) {
    meshVertices_ = vertices;
    meshFaces_ = faces;
    if (!vertices.isEmpty()) {
        m_points.clear();
    }
    update();
}

void Scene3DWidget::clearData() {
    m_points.clear();
    meshVertices_.clear();
    meshFaces_.clear();
    update();
}

void Scene3DWidget::showEvent(QShowEvent *event) {
    QOpenGLWidget::showEvent(event);
    // 当控件变为可见时（如标签页切换），若有数据则主动触发重绘
    if (!m_points.isEmpty() || (!meshVertices_.isEmpty() && !meshFaces_.isEmpty())) {
        update();
    }
}

// ── OpenGL 初始化 ────────────────────────────
void Scene3DWidget::initializeGL() {
    // 背景色：深灰
    glClearColor(0.18f, 0.18f, 0.20f, 1.0f);

    // 启用深度测试
    glEnable(GL_DEPTH_TEST);

    // 【3D 增强】启用光照模型，让模型有立体反光感
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);  // 双面光照，背面也可见
    GLfloat lightPos[] = {1.0f, 1.0f, 1.0f, 0.0f};
    GLfloat lightAmbient[] = {0.3f, 0.3f, 0.3f, 1.0f};
    GLfloat lightDiffuse[] = {0.8f, 0.8f, 0.8f, 1.0f};
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);
    glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmbient);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiffuse);

    glPointSize(2.0f);
}

// ── 计算数据包围盒 ──────────────────────────
static void computeBounds(const QVector<QVector3D> &points,
                          const QVector<QVector3D> &meshVerts,
                          QVector3D &center, float &halfExtent) {
    float cx = 0, cy = 0, cz = 0;
    int count = 0;

    auto addPoint = [&](float x, float y, float z) {
        cx += x;
        cy += y;
        cz += z;
        ++count;
    };

    for (const auto &p : points) {
        addPoint(p.x(), p.y(), p.z());
    }
    for (const auto &v : meshVerts) {
        addPoint(v.x(), v.y(), v.z());
    }

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

// ── 计算三角面法向量 ────────────────────────
static QVector3D faceNormal(const QVector3D &a, const QVector3D &b, const QVector3D &c) {
    QVector3D ab = b - a;
    QVector3D ac = c - a;
    QVector3D n = QVector3D::crossProduct(ab, ac);
    float len = n.length();
    if (len > 1e-10f) n /= len;
    return n;
}

// ── 每帧绘制 ─────────────────────────────────
void Scene3DWidget::paintGL() {
    const bool hasPoints = !m_points.isEmpty();
    const bool hasMesh = !meshVertices_.isEmpty() && !meshFaces_.isEmpty();

    // 诊断：有数据时用醒目的红色清屏，确认 paintGL 被调用了
    if (hasPoints || hasMesh) {
        glClearColor(0.3f, 0.05f, 0.05f, 1.0f);  // 暗红色 = paintGL 被调用且有数据
    } else {
        glClearColor(0.18f, 0.18f, 0.20f, 1.0f);  // 深灰 = 正常空状态
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 计算包围盒（用于设置视口和坐标轴）
    QVector3D center;
    float halfExtent;
    computeBounds(m_points, meshVertices_, center, halfExtent);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float aspect = static_cast<float>(width()) / std::max(height(), 1);
    const float zFar = halfExtent * 20.0f + 10.0f;
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

    const float scale = (halfExtent > 0) ? (1.0f / halfExtent) : 1.0f;

    // 相机位置
    glTranslatef(0.0f, 0.0f, -3.0f * m_zoom);
    glRotatef(m_rotX, 1.0f, 0.0f, 0.0f);
    glRotatef(m_rotY, 0.0f, 1.0f, 0.0f);

    glDisable(GL_LIGHTING); // 坐标轴不需要光照
    glBegin(GL_LINES);
    const float axisLen = halfExtent * 1.5f;
    glColor3f(1.0f, 0.2f, 0.2f);
    glVertex3f((center.x() - axisLen) * scale, (center.y()) * scale, (center.z()) * scale);
    glVertex3f((center.x() + axisLen) * scale, (center.y()) * scale, (center.z()) * scale);
    glColor3f(0.2f, 1.0f, 0.2f);
    glVertex3f((center.x()) * scale, (center.y() - axisLen) * scale, (center.z()) * scale);
    glVertex3f((center.x()) * scale, (center.y() + axisLen) * scale, (center.z()) * scale);
    glColor3f(0.2f, 0.2f, 1.0f);
    glVertex3f((center.x()) * scale, (center.y()) * scale, (center.z() - axisLen) * scale);
    glVertex3f((center.x()) * scale, (center.y()) * scale, (center.z() + axisLen) * scale);
    glEnd();

    // ── 绘制 Mesh ──────────────────────────
    if (hasMesh) {
        glEnable(GL_LIGHTING);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        glBegin(GL_TRIANGLES);
        for (const auto &face : meshFaces_) {
            if (face.a < 0 || face.a >= meshVertices_.size() ||
                face.b < 0 || face.b >= meshVertices_.size() ||
                face.c < 0 || face.c >= meshVertices_.size())
                continue;

            const QVector3D &va = meshVertices_[face.a];
            const QVector3D &vb = meshVertices_[face.b];
            const QVector3D &vc = meshVertices_[face.c];

            // 基于面法向量的简单着色（利用 Z 分量）
            QVector3D n = faceNormal(va, vb, vc);
            float shade = 0.3f + 0.7f * std::abs(n.z());
            glColor3f(0.5f * shade, 0.7f * shade, 0.9f * shade);

            glNormal3f(n.x(), n.y(), n.z());
            glVertex3f((va.x() - center.x()) * scale,
                       (va.y() - center.y()) * scale,
                       (va.z() - center.z()) * scale);
            glVertex3f((vb.x() - center.x()) * scale,
                       (vb.y() - center.y()) * scale,
                       (vb.z() - center.z()) * scale);
            glVertex3f((vc.x() - center.x()) * scale,
                       (vc.y() - center.y()) * scale,
                       (vc.z() - center.z()) * scale);
        }
        glEnd();

        // 绘制线框，让三角面边界可见
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
            glVertex3f((va.x() - center.x()) * scale,
                       (va.y() - center.y()) * scale,
                       (va.z() - center.z()) * scale);
            glVertex3f((vb.x() - center.x()) * scale,
                       (vb.y() - center.y()) * scale,
                       (vb.z() - center.z()) * scale);
            glVertex3f((vc.x() - center.x()) * scale,
                       (vc.y() - center.y()) * scale,
                       (vc.z() - center.z()) * scale);
        }
        glEnd();

        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }

    // ── 绘制点云 ──────────────────────────
    if (hasPoints) {
        glEnable(GL_LIGHTING);
        glBegin(GL_POINTS);
        glColor3f(0.6f, 0.8f, 1.0f); // 浅蓝色点
        for (const auto &p : m_points) {
            glVertex3f((p.x() - center.x()) * scale,
                       (p.y() - center.y()) * scale,
                       (p.z() - center.z()) * scale);
        }
        glEnd();
    }

    if (!hasPoints && !hasMesh) {
        return;
    }
}

// ── 鼠标交互 ─────────────────────────────────
void Scene3DWidget::mousePressEvent(QMouseEvent *event) { m_lastPos = event->pos(); }

void Scene3DWidget::mouseMoveEvent(QMouseEvent *event) {
    const int dx = event->pos().x() - m_lastPos.x();
    const int dy = event->pos().y() - m_lastPos.y();

    if (event->buttons() & Qt::LeftButton) {
        m_rotY += (event->pos().x() - m_lastPos.x()) * 0.5f;
        m_rotX += (event->pos().y() - m_lastPos.y()) * 0.5f;
        m_rotX = std::clamp(m_rotX, -90.0f, 90.0f);
        update();
    }
    m_lastPos = event->pos();
}

void Scene3DWidget::fitToBounds() {
    QVector3D center;
    float halfExtent;
    computeBounds(m_points, meshVertices_, center, halfExtent);

    m_center = center;
    m_halfExtent = halfExtent;
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
