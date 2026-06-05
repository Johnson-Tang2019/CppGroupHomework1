#ifndef RS_SCENE3DWIDGET_H
#define RS_SCENE3DWIDGET_H

#include <QMouseEvent>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QVector3D>
#include <QVector>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

class Scene3DWidget : public QOpenGLWidget {
    Q_OBJECT
  public:
    explicit Scene3DWidget(QWidget *parent = nullptr);
    void setPoints(const QVector<QVector3D> &points);
    void fitToBounds();

  protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

  private:
    QVector<QVector3D> m_points;
    float m_rotX = 0, m_rotY = 0, m_zoom = 1.0f;
    QPoint m_lastPos;
    QVector3D m_center;
    float m_halfExtent = 1.0f;
};

#endif