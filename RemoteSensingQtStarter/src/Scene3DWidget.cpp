#include "rs/Scene3DWidget.h"

// ──────────────────────────────────────────────
// 简单的 PLY 点云三维预览控件
// 左键拖拽旋转 · 滚轮缩放
// ──────────────────────────────────────────────

// 手动实现 gluPerspective 的等效功能（避免依赖 GLU）
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

    // 计算包围盒中心和范围
    if (points.isEmpty()) {
        m_center = QVector3D(0, 0, 0);
        m_halfExtent = 1.0f;
    } else {
        float minX = points[0].x(), maxX = points[0].x();
        float minY = points[0].y(), maxY = points[0].y();
        float minZ = points[0].z(), maxZ = points[0].z();
        for (const auto &p : points) {
            minX = std::min(minX, p.x()); maxX = std::max(maxX, p.x());
            minY = std::min(minY, p.y()); maxY = std::max(maxY, p.y());
            minZ = std::min(minZ, p.z()); maxZ = std::max(maxZ, p.z());
        }
        m_center = QVector3D((minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (minZ + maxZ) * 0.5f);
        m_halfExtent = std::max({maxX - minX, maxY - minY, maxZ - minZ}) * 0.5f;
        if (m_halfExtent < 1e-6f) m_halfExtent = 1.0f;
    }

    fitToBounds();
}

// ── 自适应缩放至包围盒 ─────────────────────────
void Scene3DWidget::fitToBounds() {
    if (m_points.isEmpty()) {
        m_rotX = 0;
        m_rotY = 0;
        m_zoom = 1.0f;
        update();
        return;
    }
    // 根据包围盒大小和视锥 FOV 计算合适 zoom
    const float fov = 45.0f;
    const float dist = m_halfExtent / std::tan(fov * 3.14159265f / 360.0f);
    m_zoom = dist / 3.0f; // 因为 paintGL 中 glTranslatef 使用 -3.0f * m_zoom
    m_rotX = 0;
    m_rotY = 0;
    update();
}

// ── OpenGL 初始化 ────────────────────────────
void Scene3DWidget::initializeGL() {
    // 背景色：深灰
    glClearColor(0.18f, 0.18f, 0.20f, 1.0f);

    // 启用深度测试
    glEnable(GL_DEPTH_TEST);

    // 点大小（固定管线）
    glPointSize(2.5f);
}

// ── 每帧绘制 ─────────────────────────────────
void Scene3DWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    const float aspect = static_cast<float>(width()) / std::max(height(), 1);
    const float zFar = m_halfExtent * 20.0f + 10.0f;
    const float zNear = 0.01f;
    float projMatrix[16];
    const float f = 1.0f / std::tan(45.0f * 3.14159265f / 360.0f);
    projMatrix[0] = f / aspect;   projMatrix[1] = 0;    projMatrix[2] = 0;    projMatrix[3] = 0;
    projMatrix[4] = 0;            projMatrix[5] = f;    projMatrix[6] = 0;    projMatrix[7] = 0;
    projMatrix[8] = 0;            projMatrix[9] = 0;    projMatrix[10] = (zFar + zNear) / (zNear - zFar); projMatrix[11] = -1;
    projMatrix[12] = 0;           projMatrix[13] = 0;   projMatrix[14] = 2 * zFar * zNear / (zNear - zFar); projMatrix[15] = 0;
    glMultMatrixf(projMatrix);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // 相机位置
    glTranslatef(0.0f, 0.0f, -3.0f * m_zoom);
    glRotatef(m_rotX, 1.0f, 0.0f, 0.0f);
    glRotatef(m_rotY, 0.0f, 1.0f, 0.0f);

    // 绘制坐标轴（在包围盒中心）
    glBegin(GL_LINES);
    const float axisLen = m_halfExtent * 1.5f;
    glColor3f(1.0f, 0.2f, 0.2f);
    glVertex3f(m_center.x() - axisLen, m_center.y(), m_center.z());
    glVertex3f(m_center.x() + axisLen, m_center.y(), m_center.z());
    glColor3f(0.2f, 1.0f, 0.2f);
    glVertex3f(m_center.x(), m_center.y() - axisLen, m_center.z());
    glVertex3f(m_center.x(), m_center.y() + axisLen, m_center.z());
    glColor3f(0.2f, 0.2f, 1.0f);
    glVertex3f(m_center.x(), m_center.y(), m_center.z() - axisLen);
    glVertex3f(m_center.x(), m_center.y(), m_center.z() + axisLen);
    glEnd();

    // ── 绘制点云 ──
    if (m_points.isEmpty()) return;

    // 直接渲染原始坐标（已居中），不再归一化
    glBegin(GL_POINTS);
    glColor3f(0.6f, 0.8f, 1.0f);
    for (const auto &p : m_points) {
        glVertex3f(p.x(), p.y(), p.z());
    }
    glEnd();
}

// ── 鼠标交互 ─────────────────────────────────
// ── 鼠标交互 ─────────────────────────────────
void Scene3DWidget::mousePressEvent(QMouseEvent *event) { m_lastPos = event->pos(); }

void Scene3DWidget::mouseMoveEvent(QMouseEvent *event) {
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

void Scene3DWidget::wheelEvent(QWheelEvent *event) {
    const float delta = event->angleDelta().y() / 120.0f;
    m_zoom *= (1.0f - delta * 0.08f);
    m_zoom = std::clamp(m_zoom, 0.1f, 10.0f);
    update();
}
