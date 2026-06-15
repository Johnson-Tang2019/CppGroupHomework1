#ifndef RS_SCENE3DWIDGET_H
#define RS_SCENE3DWIDGET_H

#include "rs/DataObject.h"
#include "rs/Geometry.h"

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
    ~Scene3DWidget() override;
    void setMesh(const QVector<QVector3D>& vertices, const QVector<rs::Face>& faces);
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
    void markDlistDirty();
    void rebuildDisplayList();

    QVector<QVector3D> m_points;
    QVector<QVector3D> meshVertices_;
    QVector<rs::Face> meshFaces_;
    float m_rotX = 0, m_rotY = 0, m_zoom = 1.0f;
    QPoint m_lastPos;
    QVector3D m_center;
    float m_halfExtent = 1.0f;

    // GPU ??????? + ?????
    GLuint m_meshDList = 0;
    bool m_dlistValid = false;
    QVector3D m_cachedCenter;
    float m_cachedHalfExtent = 1.0f;
};

#endif
