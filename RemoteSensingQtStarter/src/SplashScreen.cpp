#include "rs/SplashScreen.h"

#include "rs/Translation.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPropertyAnimation>
#include <QTimer>
#include <QtMath>

namespace rs {

SplashScreen::SplashScreen(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::SplashScreen | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(420, 560);

    tickTimer_ = new QTimer(this);
    tickTimer_->setInterval(380);
    connect(tickTimer_, &QTimer::timeout, this, &SplashScreen::onTick);

    finishTimer_ = new QTimer(this);
    finishTimer_->setSingleShot(true);
    connect(finishTimer_, &QTimer::timeout, this, &SplashScreen::finishSplash);

    fadeIn_ = new QPropertyAnimation(this, "windowOpacity", this);
    fadeIn_->setDuration(520);
    fadeIn_->setStartValue(0.0);
    fadeIn_->setEndValue(1.0);

    fadeOut_ = new QPropertyAnimation(this, "windowOpacity", this);
    fadeOut_->setDuration(420);
    fadeOut_->setStartValue(1.0);
    fadeOut_->setEndValue(0.0);
    connect(fadeOut_, &QPropertyAnimation::finished, this, [this]() {
        hide();
        emit finished();
    });
}

void SplashScreen::setHeroImage(const QPixmap &pixmap) {
    heroImage_ = pixmap.isNull() ? pixmap : processHeroImage(pixmap);
    hasHeroImage_ = !heroImage_.isNull();
    update();
}

void SplashScreen::setHeroImagePath(const QString &path) {
    if (path.isEmpty()) {
        hasHeroImage_ = false;
        heroImage_ = QPixmap();
        update();
        return;
    }
    setHeroImage(QPixmap(path));
}

void SplashScreen::setMinimumDisplayMs(int ms) {
    minimumDisplayMs_ = qMax(800, ms);
}

void SplashScreen::start(int minimumDisplayMs) {
    minimumDisplayMs_ = qMax(800, minimumDisplayMs);
    dotPhase_ = 0;
    setWindowOpacity(0.0);
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    show();
    raise();
    activateWindow();
    fadeIn_->start();
    tickTimer_->start();
    finishTimer_->start(minimumDisplayMs_);
}

void SplashScreen::onTick() {
    dotPhase_ = (dotPhase_ + 1) % 4;
    update();
}

void SplashScreen::finishSplash() {
    tickTimer_->stop();
    fadeOut_->start();
}

QPixmap SplashScreen::processHeroImage(const QPixmap &source) const {
    QImage image = source.toImage().convertToFormat(QImage::Format_ARGB32);
    const QColor splashPink(QStringLiteral("#ffd6eb"));

    auto isBackground = [](int red, int green, int blue) {
        const int chroma = qMax(red, qMax(green, blue)) - qMin(red, qMin(green, blue));
        const int brightness = (red + green + blue) / 3;
        return brightness >= 230 && chroma <= 15;
    };

    auto deepenSatelliteColor = [](const QColor &input) {
        QColor color = input;
        int hue = 0;
        int saturation = 0;
        int value = 0;
        color.getHsv(&hue, &saturation, &value);

        const int red = color.red();
        const int green = color.green();
        const int blue = color.blue();
        const int chroma = qMax(red, qMax(green, blue)) - qMin(red, qMin(green, blue));
        const int brightness = (red + green + blue) / 3;

        // 卫星粉色部分：适度加深，增强对比
        if (chroma >= 12 && red >= green && red >= blue) {
            saturation = qMin(255, saturation + 40);
            value = qMax(75, value - 28);
            color.setHsv(hue, saturation, value);
            return color;
        }

        // 轮廓线等深色部分
        if (brightness < 150 && chroma >= 8) {
            value = qMax(45, value - 12);
            color.setHsv(hue, saturation, value);
            return color;
        }

        return color;
    };

    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor color = image.pixelColor(x, y);
            const int red = color.red();
            const int green = color.green();
            const int blue = color.blue();

            if (isBackground(red, green, blue)) {
                image.setPixelColor(x, y, splashPink);
            } else {
                image.setPixelColor(x, y, deepenSatelliteColor(color));
            }
        }
    }

    return QPixmap::fromImage(image);
}

void SplashScreen::drawPlaceholderArt(QPainter &p, const QRectF &area) const {
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPointF center = area.center();
    const qreal moonR = qMin(area.width(), area.height()) * 0.22;

    // 月亮
    QRadialGradient moonGrad(center + QPointF(moonR * 0.15, -moonR * 0.1), moonR * 1.2);
    moonGrad.setColorAt(0.0, QColor("#fff6fb"));
    moonGrad.setColorAt(0.55, QColor("#ffd6ea"));
    moonGrad.setColorAt(1.0, QColor("#ffb6d9"));
    p.setPen(Qt::NoPen);
    p.setBrush(moonGrad);
    p.drawEllipse(center, moonR, moonR);

    p.setBrush(QColor(255, 255, 255, 45));
    p.drawEllipse(center + QPointF(-moonR * 0.28, -moonR * 0.18), moonR * 0.18, moonR * 0.14);

    // 小人（微信式剪影）
    const QPointF personBase = center + QPointF(moonR * 0.05, moonR * 0.12);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#8b4a62"));

    p.drawEllipse(personBase + QPointF(0, -moonR * 0.52), moonR * 0.11, moonR * 0.11);

    QPainterPath body;
    body.moveTo(personBase + QPointF(0, -moonR * 0.40));
    body.lineTo(personBase + QPointF(0, -moonR * 0.08));
    body.lineTo(personBase + QPointF(-moonR * 0.18, moonR * 0.08));
    body.lineTo(personBase + QPointF(-moonR * 0.12, moonR * 0.14));
    body.lineTo(personBase + QPointF(0, moonR * 0.02));
    body.lineTo(personBase + QPointF(moonR * 0.12, moonR * 0.14));
    body.lineTo(personBase + QPointF(moonR * 0.18, moonR * 0.08));
    body.closeSubpath();
    p.drawPath(body);

    // 小星星点缀
    auto drawStar = [&](const QPointF &pos, qreal size, const QColor &color) {
        p.setBrush(color);
        p.drawEllipse(pos, size, size);
    };
    drawStar(area.topLeft() + QPointF(area.width() * 0.18, area.height() * 0.16), 3.5, QColor("#ff9ec7"));
    drawStar(area.topLeft() + QPointF(area.width() * 0.78, area.height() * 0.22), 2.8, QColor("#ffc0dd"));
    drawStar(area.topLeft() + QPointF(area.width() * 0.62, area.height() * 0.10), 2.2, QColor("#ffffff"));

    p.restore();
}

void SplashScreen::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event)

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRectF card(8, 8, width() - 16, height() - 16);
    QPainterPath clip;
    clip.addRoundedRect(card, 28, 28);

    p.setClipPath(clip);

    QLinearGradient bg(card.topLeft(), card.bottomRight());
    bg.setColorAt(0.0, QColor("#fff0f8"));
    bg.setColorAt(0.45, QColor("#ffd6eb"));
    bg.setColorAt(1.0, QColor("#ffb6d9"));
    p.fillPath(clip, bg);

    const QRectF artRect(card.left() + 36, card.top() + 42, card.width() - 72, card.height() * 0.52);
    if (hasHeroImage_) {
        const QPixmap scaled =
            heroImage_.scaled(artRect.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        const QPointF pos(artRect.center().x() - scaled.width() * 0.5,
                          artRect.center().y() - scaled.height() * 0.5);
        p.drawPixmap(pos, scaled);
    } else {
        drawPlaceholderArt(p, artRect);
    }

    p.setPen(QColor("#7a3f57"));
    QFont titleFont = font();
    titleFont.setPointSize(18);
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.drawText(QRectF(card.left(), card.top() + card.height() * 0.66, card.width(), 34),
               Qt::AlignHCenter | Qt::AlignVCenter,
               Translation::instance().tr(QStringLiteral("splash.title")));

    QFont subFont = font();
    subFont.setPointSize(10);
    p.setFont(subFont);
    p.setPen(QColor("#9a5a72"));
    p.drawText(QRectF(card.left(), card.top() + card.height() * 0.72, card.width(), 24),
               Qt::AlignHCenter | Qt::AlignVCenter, QStringLiteral("Remote Sensing Qt Starter"));

    const QString dots =
        Translation::instance().tr(QStringLiteral("splash.starting")) + QString(dotPhase_, QChar('.'));
    p.drawText(QRectF(card.left(), card.top() + card.height() * 0.78, card.width(), 22),
               Qt::AlignHCenter | Qt::AlignVCenter, dots);

    QFont teamFont = font();
    teamFont.setPointSize(9);
    p.setFont(teamFont);
    p.setPen(QColor("#b06a84"));
    p.drawText(QRectF(card.left(), card.top() + card.height() * 0.84, card.width(), 20),
               Qt::AlignHCenter | Qt::AlignVCenter,
               Translation::instance().tr(QStringLiteral("splash.team")));

    // 底部进度条装饰
    const QRectF barBg(card.left() + 72, card.bottom() - 52, card.width() - 144, 6);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 120));
    p.drawRoundedRect(barBg, 3, 3);

    const qreal progress = qMin(1.0, static_cast<qreal>(dotPhase_) / 3.0);
    QRectF barFill = barBg;
    barFill.setWidth(barBg.width() * (0.25 + progress * 0.75));
    QLinearGradient barGrad(barFill.topLeft(), barFill.topRight());
    barGrad.setColorAt(0.0, QColor("#ff8fb8"));
    barGrad.setColorAt(1.0, QColor("#ff69b4"));
    p.setBrush(barGrad);
    p.drawRoundedRect(barFill, 3, 3);

    p.setClipping(false);
    p.setPen(QPen(QColor(255, 255, 255, 180), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(card, 28, 28);
}

} // namespace rs
