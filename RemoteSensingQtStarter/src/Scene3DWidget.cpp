#include "rs/Scene3DWidget.h"
#include <GL/gl.h>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
//2026.6.6
static void buildPerspectiveMatrix(float fovY, float aspect, float zNear, float zFar, float m[16]) {
    const float f = 1.0f / std::tan(fovY * 3.14159265f / 360.0f);
    m[0] = f / aspect; m[1] = 0; m[2] = 0; m[3] = 0;
    m[4] = 0; m[5] = f; m[6] = 0; m[7] = 0;
    m[8] = 0; m[9] = 0; m[10] = (zFar + zNear) / (zNear - zFar); m[11] = -1;
    m[12] = 0; m[13] = 0; m[14] = 2 * zFar * zNear / (zNear - zFar); m[15] = 0;
}

Scene3DWidget::Scene3DWidget(QWidget *parent) : QOpenGLWidget(parent) {}

void Scene3DWidget::setPoints(const QVector<QVector3D>& points) {
    m_points = points;
    meshVertices_.clear(); // 清空Mesh，防止重叠
    meshFaces_.clear();
    update();
}

void Scene3DWidget::setMesh(const QVector<QVector3D>& vertices, const QVector<rs::Face>& faces) {
    meshVertices_ = vertices;
    meshFaces_ = faces;
    m_points.clear(); // 修复报错：改为 m_points.clear()
    update();
}

void Scene3DWidget::initializeGL() {
    glClearColor(0.12f, 0.12f, 0.15f, 1.0f); // 高级深空灰背景
    glEnable(GL_DEPTH_TEST);
    
    // 【3D 增强】启用光照模型，让模型有立体反光感
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_NORMALIZE);
    GLfloat lightPos[] = { 1.0f, 1.0f, 1.0f, 0.0f };
    glLightfv(GL_LIGHT0, GL_POSITION, lightPos);

    glPointSize(2.0f);
}

void Scene3DWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    const float aspect = static_cast<float>(width()) / std::max(height(), 1);
    float projMatrix[16];
    buildPerspectiveMatrix(45.0f, aspect, 0.01f, 1000.0f, projMatrix);
    glMultMatrixf(projMatrix);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glTranslatef(0.0f, 0.0f, -3.0f * m_zoom);
    glRotatef(m_rotX, 1.0f, 0.0f, 0.0f);
    glRotatef(m_rotY, 0.0f, 1.0f, 0.0f);

    glDisable(GL_LIGHTING); // 坐标轴不需要光照
    glBegin(GL_LINES);
    glColor3f(1.0f, 0.3f, 0.3f); glVertex3f(0,0,0); glVertex3f(1.5f,0,0); // X红
    glColor3f(0.3f, 1.0f, 0.3f); glVertex3f(0,0,0); glVertex3f(0,1.5f,0); // Y绿
    glColor3f(0.3f, 0.6f, 1.0f); glVertex3f(0,0,0); glVertex3f(0,0,1.5f); // Z蓝
    glEnd();

    // 【算法增强】计算自动居中与缩放（同时兼容点云和Mesh）
    float cx = 0, cy = 0, cz = 0;
    int totalPts = m_points.size() + meshVertices_.size();
    if (totalPts == 0) return;

    for (const auto& p : m_points) { cx += p.x(); cy += p.y(); cz += p.z(); }
    for (const auto& p : meshVertices_) { cx += p.x(); cy += p.y(); cz += p.z(); }
    cx /= totalPts; cy /= totalPts; cz /= totalPts;

    float maxDist = 0;
    for (const auto& p : m_points) maxDist = std::max(maxDist, p.distanceToPoint(QVector3D(cx, cy, cz)));
    for (const auto& p : meshVertices_) maxDist = std::max(maxDist, p.distanceToPoint(QVector3D(cx, cy, cz)));
    const float scale = (maxDist > 0) ? (1.5f / maxDist) : 1.0f;

    // 绘制点云
    if (!m_points.isEmpty()) {
        glBegin(GL_POINTS);
        glColor3f(0.0f, 0.8f, 1.0f); // 极客蓝
        for (const auto& p : m_points) {
            glVertex3f((p.x() - cx) * scale, (p.y() - cy) * scale, (p.z() - cz) * scale);
        }
        glEnd();
    }

    // 绘制 Mesh
    if (!meshFaces_.isEmpty()) {
        glEnable(GL_LIGHTING); // 开启光照
        glBegin(GL_TRIANGLES);
        glColor3f(0.8f, 0.8f, 0.8f); // 工业灰质感

        for (const auto& face : meshFaces_) {
            QVector3D v1 = (meshVertices_[face.a] - QVector3D(cx, cy, cz)) * scale;
            QVector3D v2 = (meshVertices_[face.b] - QVector3D(cx, cy, cz)) * scale;
            QVector3D v3 = (meshVertices_[face.c] - QVector3D(cx, cy, cz)) * scale;

            // 叉乘计算法线，实现完美光影
            QVector3D normal = QVector3D::crossProduct(v2 - v1, v3 - v1).normalized();
            glNormal3f(normal.x(), normal.y(), normal.z());

            glVertex3f(v1.x(), v1.y(), v1.z());
            glVertex3f(v2.x(), v2.y(), v2.z());
            glVertex3f(v3.x(), v3.y(), v3.z());
        }
        glEnd();
        glDisable(GL_LIGHTING);
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
void Scene3DWidget::wheelEvent(QWheelEvent *event) {
    m_zoom *= (1.0f - (event->angleDelta().y() / 120.0f) * 0.08f);
    m_zoom = std::clamp(m_zoom, 0.1f, 10.0f);
    update();
}