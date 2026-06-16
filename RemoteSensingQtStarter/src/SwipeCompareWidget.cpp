#include "rs/SwipeCompareWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

SwipeCompareWidget::SwipeCompareWidget(QWidget *parent) : QWidget(parent) {
    setMouseTracking(true);
    setMinimumSize(520, 360);
}

void SwipeCompareWidget::setComparison(QImage oldImage, QImage newImage, QImage ndviDiffHeatmap,
                                       QString oldName, QString newName) {
    oldImage_ = std::move(oldImage);
    newImage_ = std::move(newImage);
    heatmap_ = std::move(ndviDiffHeatmap);
    oldName_ = std::move(oldName);
    newName_ = std::move(newName);
    dividerRatio_ = 0.5;
    update();
}

void SwipeCompareWidget::clearComparison() {
    oldImage_ = {};
    newImage_ = {};
    heatmap_ = {};
    oldName_.clear();
    newName_.clear();
    dividerRatio_ = 0.5;
    update();
}

QRect SwipeCompareWidget::imageTargetRect() const {
    const QRect area = rect().adjusted(20, 18, -20, -20);
    const int heatHeight = heatmap_.isNull() ? 0 : std::max(110, area.height() / 3);
    const QRect topArea(area.left(), area.top(), area.width(), area.height() - heatHeight - 18);
    if (oldImage_.isNull()) {
        return topArea;
    }
    QSize fitted = oldImage_.size();
    fitted.scale(topArea.size(), Qt::KeepAspectRatio);
    return QRect(QPoint(topArea.center().x() - fitted.width() / 2,
                        topArea.center().y() - fitted.height() / 2),
                 fitted);
}

QRect SwipeCompareWidget::heatmapTargetRect() const {
    if (heatmap_.isNull()) {
        return {};
    }
    const QRect imageRect = imageTargetRect();
    const QRect area = rect().adjusted(20, imageRect.bottom() + 18, -20, -28);
    QSize fitted = heatmap_.size();
    fitted.scale(area.size(), Qt::KeepAspectRatio);
    return QRect(QPoint(area.center().x() - fitted.width() / 2,
                        area.center().y() - fitted.height() / 2),
                 fitted);
}

void SwipeCompareWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.fillRect(rect(), QColor(255, 240, 245));

    if (oldImage_.isNull() || newImage_.isNull()) {
        painter.setPen(QColor(125, 56, 86));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("请选择两期遥感影像进行滑动对比"));
        return;
    }

    const QRect imageRect = imageTargetRect();
    painter.fillRect(imageRect.adjusted(-1, -1, 1, 1), QColor(252, 220, 232));
    painter.drawImage(imageRect, oldImage_);

    const int dividerX = imageRect.left() + static_cast<int>(dividerRatio_ * imageRect.width());
    painter.save();
    painter.setClipRect(QRect(dividerX, imageRect.top(), imageRect.right() - dividerX + 1, imageRect.height()));
    painter.drawImage(imageRect, newImage_);
    painter.restore();

    painter.setPen(QPen(QColor(224, 73, 142), 3));
    painter.drawLine(dividerX, imageRect.top(), dividerX, imageRect.bottom());
    painter.setBrush(QColor(255, 91, 166));
    painter.setPen(QPen(QColor(175, 28, 105), 1));
    painter.drawRoundedRect(QRect(dividerX - 13, imageRect.center().y() - 28, 26, 56), 13, 13);
    painter.setPen(QPen(QColor(255, 235, 245), 2));
    painter.drawLine(dividerX - 4, imageRect.center().y() - 14, dividerX - 4, imageRect.center().y() + 14);
    painter.drawLine(dividerX + 4, imageRect.center().y() - 14, dividerX + 4, imageRect.center().y() + 14);

    painter.setPen(QColor(72, 32, 48));
    painter.setBrush(QColor(255, 240, 245, 210));
    painter.drawRoundedRect(QRect(imageRect.left() + 12, imageRect.top() + 10, 220, 30), 6, 6);
    painter.drawText(QRect(imageRect.left() + 22, imageRect.top() + 10, 200, 30),
                     Qt::AlignVCenter | Qt::AlignLeft,
                     QStringLiteral("旧影像：%1").arg(oldName_));
    painter.drawRoundedRect(QRect(imageRect.right() - 232, imageRect.top() + 10, 220, 30), 6, 6);
    painter.drawText(QRect(imageRect.right() - 222, imageRect.top() + 10, 200, 30),
                     Qt::AlignVCenter | Qt::AlignRight,
                     QStringLiteral("新影像：%1").arg(newName_));

    const QRect heatRect = heatmapTargetRect();
    if (!heatRect.isEmpty()) {
        painter.setPen(QPen(QColor(232, 169, 192), 1));
        painter.setBrush(QColor(255, 248, 251));
        painter.drawRoundedRect(heatRect.adjusted(-10, -28, 10, 26), 8, 8);
        painter.setPen(QColor(72, 32, 48));
        painter.drawText(QRect(heatRect.left(), heatRect.top() - 25, heatRect.width(), 22),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         QStringLiteral("NDVI 差值热力图（新 - 旧）"));
        painter.drawImage(heatRect, heatmap_);

        const QRect legend(heatRect.right() - 260, heatRect.bottom() + 7, 260, 14);
        QLinearGradient gradient(legend.topLeft(), legend.topRight());
        gradient.setColorAt(0.0, QColor(198, 36, 72));
        gradient.setColorAt(0.5, QColor(250, 250, 250));
        gradient.setColorAt(1.0, QColor(27, 150, 91));
        painter.fillRect(legend, gradient);
        painter.setPen(QColor(110, 70, 86));
        painter.drawRect(legend);
        painter.drawText(QRect(legend.left(), legend.bottom() + 2, 90, 18),
                         Qt::AlignLeft, QStringLiteral("减少"));
        painter.drawText(QRect(legend.center().x() - 35, legend.bottom() + 2, 70, 18),
                         Qt::AlignCenter, QStringLiteral("稳定"));
        painter.drawText(QRect(legend.right() - 90, legend.bottom() + 2, 90, 18),
                         Qt::AlignRight, QStringLiteral("增加"));
    }
}

void SwipeCompareWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || oldImage_.isNull()) {
        return;
    }
    const QRect imageRect = imageTargetRect();
    const int dividerX = imageRect.left() + static_cast<int>(dividerRatio_ * imageRect.width());
    if (imageRect.contains(event->pos()) || std::abs(event->pos().x() - dividerX) <= 18) {
        dragging_ = true;
        updateDividerFromX(event->pos().x());
    }
}

void SwipeCompareWidget::mouseMoveEvent(QMouseEvent *event) {
    if (dragging_) {
        updateDividerFromX(event->pos().x());
        return;
    }
    const QRect imageRect = imageTargetRect();
    const int dividerX = imageRect.left() + static_cast<int>(dividerRatio_ * imageRect.width());
    setCursor((imageRect.contains(event->pos()) && std::abs(event->pos().x() - dividerX) <= 20)
                  ? Qt::SizeHorCursor
                  : Qt::ArrowCursor);
}

void SwipeCompareWidget::mouseReleaseEvent(QMouseEvent *) {
    dragging_ = false;
}

void SwipeCompareWidget::resizeEvent(QResizeEvent *) {
    update();
}

void SwipeCompareWidget::updateDividerFromX(int x) {
    const QRect imageRect = imageTargetRect();
    if (imageRect.width() <= 0) {
        return;
    }
    dividerRatio_ = std::clamp(static_cast<double>(x - imageRect.left()) /
                                   static_cast<double>(imageRect.width()),
                               0.02, 0.98);
    update();
}
