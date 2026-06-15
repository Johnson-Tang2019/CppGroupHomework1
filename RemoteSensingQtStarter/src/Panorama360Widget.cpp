#include "rs/Panorama360Widget.h"

#include <GL/gl.h>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;

float degToRad(float deg) {
    return deg * kPi / 180.0f;
}
} // namespace

Panorama360Widget::Panorama360Widget(QWidget *parent) : QOpenGLWidget(parent) {
    setMouseTracking(true);
}

Panorama360Widget::~Panorama360Widget() {
    makeCurrent();
    if (textureId_) {
        glDeleteTextures(1, &textureId_);
        textureId_ = 0;
    }
    doneCurrent();
}

void Panorama360Widget::setPanorama(const QImage &image, const QString &name) {
    panorama_ = image.isNull() ? QImage() : image.convertToFormat(QImage::Format_RGBA8888);
    panoramaName_ = name;
    textureDirty_ = true;
    yaw_ = 0.0f;
    pitch_ = 0.0f;
    fov_ = 70.0f;
    update();
}

void Panorama360Widget::clearPanorama() {
    panorama_ = QImage();
    panoramaName_.clear();
    textureDirty_ = true;
    update();
}

bool Panorama360Widget::hasPanorama() const {
    return !panorama_.isNull();
}

void Panorama360Widget::initializeGL() {
    glClearColor(0.04f, 0.05f, 0.06f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    textureDirty_ = true;
}

void Panorama360Widget::resizeGL(int width, int height) {
    glViewport(0, 0, width, std::max(height, 1));
}

void Panorama360Widget::uploadTextureIfNeeded() {
    if (!textureDirty_) {
        return;
    }

    if (textureId_) {
        glDeleteTextures(1, &textureId_);
        textureId_ = 0;
    }

    if (!panorama_.isNull()) {
        glGenTextures(1, &textureId_);
        glBindTexture(GL_TEXTURE_2D, textureId_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, panorama_.width(), panorama_.height(),
                     0, GL_RGBA, GL_UNSIGNED_BYTE, panorama_.constBits());
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    textureDirty_ = false;
}

void Panorama360Widget::paintGL() {
    uploadTextureIfNeeded();

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!textureId_) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(12, 14, 17));
        painter.setPen(QColor(220, 225, 230));
        painter.drawText(rect(), Qt::AlignCenter,
                         QStringLiteral("请加载一张 360° 全景/街景图"));
        return;
    }

    const float aspect = static_cast<float>(width()) / static_cast<float>(std::max(height(), 1));
    const float zNear = 0.01f;
    const float zFar = 20.0f;
    const float f = 1.0f / std::tan(degToRad(fov_) * 0.5f);
    const float proj[16] = {
        f / aspect, 0.0f, 0.0f, 0.0f,
        0.0f, f, 0.0f, 0.0f,
        0.0f, 0.0f, (zFar + zNear) / (zNear - zFar), -1.0f,
        0.0f, 0.0f, 2.0f * zFar * zNear / (zNear - zFar), 0.0f};

    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(proj);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef(-pitch_, 1.0f, 0.0f, 0.0f);
    glRotatef(-yaw_, 0.0f, 1.0f, 0.0f);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, textureId_);
    glColor3f(1.0f, 1.0f, 1.0f);
    drawSphere(5.0f, 96, 48);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_DEPTH_TEST);

    QPainter painter(this);
    painter.setPen(QColor(245, 245, 245));
    painter.drawText(12, 24, panoramaName_.isEmpty()
                             ? QStringLiteral("360° 街景")
                             : QStringLiteral("360° 街景：%1").arg(panoramaName_));
}

void Panorama360Widget::drawSphere(float radius, int slices, int stacks) {
    for (int stack = 0; stack < stacks; ++stack) {
        const float v0 = static_cast<float>(stack) / static_cast<float>(stacks);
        const float v1 = static_cast<float>(stack + 1) / static_cast<float>(stacks);
        const float phi0 = kPi * v0;
        const float phi1 = kPi * v1;

        glBegin(GL_TRIANGLE_STRIP);
        for (int slice = 0; slice <= slices; ++slice) {
            const float u = static_cast<float>(slice) / static_cast<float>(slices);
            const float theta = 2.0f * kPi * u;

            const float x0 = radius * std::sin(phi0) * std::sin(theta);
            const float y0 = radius * std::cos(phi0);
            const float z0 = radius * std::sin(phi0) * std::cos(theta);
            const float x1 = radius * std::sin(phi1) * std::sin(theta);
            const float y1 = radius * std::cos(phi1);
            const float z1 = radius * std::sin(phi1) * std::cos(theta);

            glTexCoord2f(1.0f - u, v0);
            glVertex3f(x0, y0, z0);
            glTexCoord2f(1.0f - u, v1);
            glVertex3f(x1, y1, z1);
        }
        glEnd();
    }
}

void Panorama360Widget::mousePressEvent(QMouseEvent *event) {
    lastPos_ = event->pos();
}

void Panorama360Widget::mouseMoveEvent(QMouseEvent *event) {
    if (event->buttons() & Qt::LeftButton) {
        const QPoint delta = event->pos() - lastPos_;
        yaw_ += delta.x() * 0.18f;
        pitch_ = std::clamp(pitch_ + delta.y() * 0.18f, -85.0f, 85.0f);
        update();
    }
    lastPos_ = event->pos();
}

void Panorama360Widget::wheelEvent(QWheelEvent *event) {
    const float steps = event->angleDelta().y() / 120.0f;
    fov_ = std::clamp(fov_ - steps * 4.0f, 35.0f, 100.0f);
    update();
}
