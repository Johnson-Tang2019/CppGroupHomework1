#include "rs/Scene3DWidget.h"

#include <GL/gl.h>
#include <QMouseEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

// ──────────────────────────────────────────────
// 简单的 PLY 点云三维预览控件
// 左键拖拽旋转 · 滚轮缩放
// ──────────────────────────────────────────────

// 手动实现 gluPerspective 的等效功能（避免依赖 GLU）
static void buildPerspectiveMatrix(float fovY, float aspect, float zNear, float zFar, float m[16]) {
    const float f = 1.0f / std::tan(fovY * 3.14159265f / 360.0f);
    // OpenGL 列优先矩阵
    m[0]  = f / aspect; m[1]  = 0; m[2]  = 0;                     m[3]  = 0;
    m[4]  = 0;           m[5]  = f; m[6]  = 0;                     m[7]  = 0;
    m[8]  = 0;           m[9]  = 0; m[10] = (zFar + zNear) / (zNear - zFar); m[11] = -1;
    m[12] = 0;           m[13] = 0; m[14] = 2 * zFar * zNear / (zNear - zFar); m[15] = 0;
}

Scene3DWidget::Scene3DWidget(QWidget *parent)
    : QOpenGLWidget(parent)
{
}

void Scene3DWidget::setPoints(const QVector<QVector3D>& points)
{
    m_points = points;
    update(); // 请求重绘
}

// ── OpenGL 初始化 ────────────────────────────
void Scene3DWidget::initializeGL()
{
    // 背景色：深灰
    glClearColor(0.18f, 0.18f, 0.20f, 1.0f);

    // 启用深度测试
    glEnable(GL_DEPTH_TEST);

    // 点大小（固定管线）
    glPointSize(2.5f);
}

// ── 每帧绘制 ─────────────────────────────────
void Scene3DWidget::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    const float aspect = static_cast<float>(width()) / std::max(height(), 1);
    float projMatrix[16];
    buildPerspectiveMatrix(45.0f, aspect, 0.01f, 1000.0f, projMatrix);
    glMultMatrixf(projMatrix);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

        // 相机位置：先从原点往后拉，让归一化后的点云在可见范围内
    glTranslatef(0.0f, 0.0f, -3.0f * m_zoom);
    glRotatef(m_rotX, 1.0f, 0.0f, 0.0f);
    glRotatef(m_rotY, 0.0f, 1.0f, 0.0f);

    // 绘制坐标轴（辅助观察方向）
    glBegin(GL_LINES);
    // X 轴（红）
    glColor3f(1.0f, 0.2f, 0.2f);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(2.0f, 0.0f, 0.0f);
    // Y 轴（绿）
    glColor3f(0.2f, 1.0f, 0.2f);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, 2.0f, 0.0f);
    // Z 轴（蓝）
    glColor3f(0.2f, 0.2f, 1.0f);
    glVertex3f(0.0f, 0.0f, 0.0f);
    glVertex3f(0.0f, 0.0f, 2.0f);
    glEnd();

    // ── 绘制点云 ──────────────────────────
    if (m_points.isEmpty()) {
        // 无数据时画提示文字（通过 QPainter 叠加）
        return;
    }

    // 计算点云中心，居中显示
    float cx = 0, cy = 0, cz = 0;
    for (const auto& p : m_points) {
        cx += p.x();
        cy += p.y();
        cz += p.z();
    }
    const float invN = 1.0f / m_points.size();
    cx *= invN;
    cy *= invN;
    cz *= invN;

    // 计算点云范围，用于自动缩放
    float maxDist = 0;
    for (const auto& p : m_points) {
        const float dx = p.x() - cx;
        const float dy = p.y() - cy;
        const float dz = p.z() - cz;
        maxDist = std::max(maxDist, std::sqrt(dx*dx + dy*dy + dz*dz));
    }
    const float scale = (maxDist > 0) ? (1.0f / maxDist) : 1.0f;

    glBegin(GL_POINTS);
    glColor3f(0.6f, 0.8f, 1.0f); // 浅蓝色点
    for (const auto& p : m_points) {
        // 平移至中心并缩放到合理大小
        glVertex3f((p.x() - cx) * scale,
                   (p.y() - cy) * scale,
                   (p.z() - cz) * scale);
    }
    glEnd();
}

// ── 鼠标交互 ─────────────────────────────────
void Scene3DWidget::mousePressEvent(QMouseEvent *event)
{
    m_lastPos = event->pos();
}

void Scene3DWidget::mouseMoveEvent(QMouseEvent *event)
{
    const int dx = event->pos().x() - m_lastPos.x();
    const int dy = event->pos().y() - m_lastPos.y();

    if (event->buttons() & Qt::LeftButton) {
        m_rotY += dx * 0.5f;
        m_rotX += dy * 0.5f;
        m_rotX = std::clamp(m_rotX, -90.0f, 90.0f);
        update();
    }

    m_lastPos = event->pos();
}

void Scene3DWidget::wheelEvent(QWheelEvent *event)
{
    const float delta = event->angleDelta().y() / 120.0f;
    m_zoom *= (1.0f - delta * 0.08f);
    m_zoom = std::clamp(m_zoom, 0.1f, 10.0f);
    update();
}
