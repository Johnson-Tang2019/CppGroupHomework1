#ifndef RS_PANORAMA360WIDGET_H
#define RS_PANORAMA360WIDGET_H

#include <QImage>
#include <QOpenGLWidget>
#include <QPoint>

class Panorama360Widget : public QOpenGLWidget {
    Q_OBJECT

public:
    explicit Panorama360Widget(QWidget *parent = nullptr);
    ~Panorama360Widget() override;

    void setPanorama(const QImage &image, const QString &name = QString());
    void clearPanorama();
    bool hasPanorama() const;

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int width, int height) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void uploadTextureIfNeeded();
    void drawSphere(float radius, int slices, int stacks);

    QImage panorama_;
    QString panoramaName_;
    QPoint lastPos_;
    unsigned int textureId_ = 0;
    bool textureDirty_ = false;
    float yaw_ = 0.0f;
    float pitch_ = 0.0f;
    float fov_ = 70.0f;
};

#endif
