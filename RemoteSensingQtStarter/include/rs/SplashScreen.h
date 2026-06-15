#pragma once

#include <QPixmap>
#include <QWidget>

class QPropertyAnimation;
class QTimer;

namespace rs {

// 启动闪屏：微信式「插画 + 品牌 + 加载动画」，插画位可后续替换为正式图片。
class SplashScreen final : public QWidget {
    Q_OBJECT

public:
    explicit SplashScreen(QWidget *parent = nullptr);

    void setHeroImage(const QPixmap &pixmap);
    void setHeroImagePath(const QString &path);
    void setMinimumDisplayMs(int ms);

public slots:
    void start(int minimumDisplayMs = 2600);

signals:
    void finished();

protected:
    void paintEvent(QPaintEvent *event) override;

private slots:
    void onTick();
    void finishSplash();

private:
    void drawPlaceholderArt(QPainter &p, const QRectF &area) const;
    QPixmap processHeroImage(const QPixmap &source) const;

    QPixmap heroImage_;
    bool hasHeroImage_{false};
    int minimumDisplayMs_{2600};
    int dotPhase_{0};

    QTimer *tickTimer_{nullptr};
    QTimer *finishTimer_{nullptr};
    QPropertyAnimation *fadeIn_{nullptr};
    QPropertyAnimation *fadeOut_{nullptr};
};

} // namespace rs
