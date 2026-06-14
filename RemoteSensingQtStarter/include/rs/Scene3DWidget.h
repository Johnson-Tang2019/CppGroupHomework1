#ifndef RS_SCENE3DWIDGET_H
#define RS_SCENE3DWIDGET_H

#include "rs/DataObject.h"
#include "rs/Geometry.h"

#include <QMouseEvent>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QVector3D>
#include <QVector>
#include <QWheelEvent>

class Scene3DWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
  public:
    explicit Scene3DWidget(QWidget *parent = nullptr);
    ~Scene3DWidget() override;

    void setMesh(const QVector<QVector3D> &vertices, const QVector<rs::Face> &faces);
    void setPoints(const QVector<QVector3D> &points);
    void setDem(const rs::DemLayer &dem, int maxGrid = 128);
    void clearData();
    void fitToBounds();

  protected:
    void initializeGL() override;
    void paintGL() override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

  private:
    void updateBounds();
    void invalidateGpu();
    void rebuildGpuBuffers();

    static constexpr int kMaxDisplayPoints = 250000;
    static constexpr int kMaxDisplayFaces = 150000;
    static constexpr int kWireframeFaceLimit = 40000;

    QVector<QVector3D> m_points;
    QVector<QVector3D> meshVertices_;
    QVector<rs::Face> meshFaces_;

    float m_rotX = 0;
    float m_rotY = 0;
    float m_zoom = 1.0f;
    QPoint m_lastPos;
    QVector3D m_center;
    float m_halfExtent = 1.0f;

    bool gpuDirty_ = true;
    bool drawWireframe_ = false;
    int pointDrawCount_ = 0;
    int meshTriangleVertexCount_ = 0;
    QOpenGLBuffer pointVbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer meshVbo_{QOpenGLBuffer::VertexBuffer};
};

#endif
