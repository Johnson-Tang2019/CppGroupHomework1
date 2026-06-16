#pragma once

#include <QImage>
#include <QString>
#include <QWidget>

class SwipeCompareWidget final : public QWidget {
    Q_OBJECT

public:
    explicit SwipeCompareWidget(QWidget *parent = nullptr);

    void setComparison(QImage oldImage, QImage newImage, QImage ndviDiffHeatmap,
                       QString oldName, QString newName);
    void clearComparison();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QRect imageTargetRect() const;
    QRect heatmapTargetRect() const;
    void updateDividerFromX(int x);

    QImage oldImage_;
    QImage newImage_;
    QImage heatmap_;
    QString oldName_;
    QString newName_;
    double dividerRatio_ = 0.5;
    bool dragging_ = false;
};
