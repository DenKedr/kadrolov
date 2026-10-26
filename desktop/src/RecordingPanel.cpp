#include "RecordingPanel.h"
#include "CaptureIcons.h"
#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace {
QPoint globalPoint(QMouseEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  return e->globalPosition().toPoint();
#else
  return e->globalPos();
#endif
}
class RecordingFrame : public QWidget {
public:
  RecordingFrame() {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput |
                   Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#F97316"), 1.5, Qt::DashLine));
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(rect()).adjusted(1, 1, -1, -1));
  }
};
} // namespace
RecordingPanel::RecordingPanel(Recorder *r, QWidget *p)
    : QWidget(p), recorder_(r), frame_(new RecordingFrame) {
  setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
  setAttribute(Qt::WA_TranslucentBackground);
  setWindowTitle(QStringLiteral("Кадролов — управление записью"));
  setFixedSize(244, 48);
  setCursor(Qt::OpenHandCursor);
  auto *l = new QHBoxLayout(this);
  l->setContentsMargins(24, 5, 8, 5);
  l->setSpacing(5);
  status_ = new QLabel;
  status_->setPixmap(captureIcon("record", QColor("#DC2626")).pixmap(17, 17));
  status_->setFixedSize(18, 22);
  status_->setAttribute(Qt::WA_TransparentForMouseEvents);
  status_->setToolTip(QStringLiteral("Подготовка записи"));
  time_ = new QLabel(QStringLiteral("00:00:00"));
  time_->setStyleSheet(QStringLiteral(
      "color:#20242B;font-size:16px;font-weight:600;background:transparent;"));
  time_->setMinimumWidth(79);
  time_->setAlignment(Qt::AlignCenter);
  time_->setAttribute(Qt::WA_TransparentForMouseEvents);
  pause_ = new QPushButton;
  stop_ = new QPushButton;
  for (auto *button : {pause_, stop_}) {
    button->setFixedSize(35, 35);
    button->setIconSize(QSize(24, 24));
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(
        QStringLiteral("QPushButton{border:0;background:transparent;border-"
                       "radius:5px;padding:3px;}QPushButton:hover{background:#"
                       "F0F1F3;}QPushButton:pressed{background:#FFE5D4;}"));
  }
  pause_->setIcon(captureIcon("pause"));
  pause_->setToolTip(QStringLiteral("Пауза"));
  pause_->setAccessibleName(QStringLiteral("Пауза"));
  stop_->setIcon(captureIcon("stop", QColor("#DC2626")));
  stop_->setToolTip(QStringLiteral("Стоп — завершить и сохранить запись"));
  stop_->setAccessibleName(QStringLiteral("Стоп — сохранить запись"));
  l->addWidget(status_);
  l->addWidget(time_);
  l->addWidget(pause_);
  l->addWidget(stop_);
  connect(pause_, &QPushButton::clicked, this, [this] {
    if (recorder_->state() == Recorder::Paused)
      recorder_->resume();
    else if (recorder_->state() == Recorder::Recording)
      recorder_->pause();
  });
  connect(stop_, &QPushButton::clicked, this, [this] {
    recorder_->stop();
    emit stopRequested();
  });
  connect(r, &Recorder::elapsedChanged, this, [this](qint64 ms) {
    qint64 s = ms / 1000;
    time_->setText(QStringLiteral("%1:%2:%3")
                       .arg(s / 3600, 2, 10, QChar('0'))
                       .arg((s / 60) % 60, 2, 10, QChar('0'))
                       .arg(s % 60, 2, 10, QChar('0')));
  });
  connect(r, &Recorder::stateChanged, this, [this](Recorder::State state) {
    bool paused = state == Recorder::Paused;
    pause_->setIcon(captureIcon(paused ? "play" : "pause"));
    pause_->setToolTip(paused ? QStringLiteral("Продолжить запись")
                              : QStringLiteral("Пауза"));
    pause_->setAccessibleName(pause_->toolTip());
    pause_->setEnabled(state == Recorder::Recording || paused);
    stop_->setEnabled(state == Recorder::Recording || paused ||
                      state == Recorder::Starting);
    QString status =
        paused                          ? QStringLiteral("Запись на паузе")
        : state == Recorder::Recording  ? QStringLiteral("Идёт запись")
        : state == Recorder::Finalizing ? QStringLiteral("Сохранение видео…")
                                        : QStringLiteral("Подготовка записи");
    status_->setToolTip(status);
    status_->setAccessibleName(status);
    setAccessibleName(status);
    status_->setPixmap(
        captureIcon("record", paused ? QColor("#9CA3AF") : QColor("#DC2626"))
            .pixmap(17, 17));
    if (state == Recorder::Idle || state == Recorder::Failed) {
      frame_->hide();
      hide();
    } else {
      frame_->show();
    }
  });
#ifdef Q_OS_WIN
  if (!SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()), 0x00000011))
    setToolTip(QStringLiteral("Переместите панель за область захвата: эта "
                              "Windows не исключает её из видео."));
  SetWindowDisplayAffinity(reinterpret_cast<HWND>(frame_->winId()), 0x00000011);
#endif
  auto *screen = QApplication::primaryScreen();
  setCaptureRegion(screen->geometry(), screen);
  connect(this, &QObject::destroyed, frame_,
          [frame = frame_] { delete frame; });
}
void RecordingPanel::setCaptureRegion(const QRect &rect, QScreen *screen) {
  if (!screen)
    screen = QApplication::primaryScreen();
  frame_->setGeometry(rect.adjusted(-2, -2, 2, 2));
  auto available = screen->availableGeometry();
  int x = qBound(available.left() + 8, rect.center().x() - width() / 2,
                 qMax(available.left() + 8, available.right() - width() - 8));
  int y = rect.bottom() + 10;
  if (y + height() > available.bottom() - 8)
    y = rect.top() - height() - 10;
  if (y < available.top() + 8)
    y = available.bottom() - height() - 8;
  move(x, y);
}
void RecordingPanel::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setBrush(Qt::white);
  p.setPen(QPen(QColor("#D8DADD"), 1));
  p.drawRoundedRect(QRectF(rect()).adjusted(.5, .5, -.5, -.5), 9, 9);
  p.setPen(Qt::NoPen);
  p.setBrush(QColor("#8B9099"));
  for (int row = 0; row < 3; ++row)
    for (int col = 0; col < 2; ++col)
      p.drawEllipse(QPointF(10 + col * 5, 16 + row * 6), 1.1, 1.1);
}
void RecordingPanel::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    dragging_ = true;
    dragOffset_ = globalPoint(event) - frameGeometry().topLeft();
    setCursor(Qt::ClosedHandCursor);
    event->accept();
  }
}
void RecordingPanel::mouseMoveEvent(QMouseEvent *event) {
  if (dragging_) {
    QPoint next = globalPoint(event) - dragOffset_;
    auto *screen = QGuiApplication::screenAt(globalPoint(event));
    if (screen) {
      auto area = screen->availableGeometry();
      next.setX(qBound(area.left(), next.x(),
                       qMax(area.left(), area.right() - width() + 1)));
      next.setY(qBound(area.top(), next.y(),
                       qMax(area.top(), area.bottom() - height() + 1)));
    }
    move(next);
    event->accept();
  }
}
void RecordingPanel::mouseReleaseEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    dragging_ = false;
    setCursor(Qt::OpenHandCursor);
    event->accept();
  }
}
void RecordingPanel::closeEvent(QCloseEvent *event) {
  if (qApp->isSavingSession()) {
    frame_->hide();
    QWidget::closeEvent(event);
    return;
  }
  if (recorder_->state() == Recorder::Recording ||
      recorder_->state() == Recorder::Paused ||
      recorder_->state() == Recorder::Starting ||
      recorder_->state() == Recorder::Finalizing) {
    event->ignore();
    raise();
    return;
  }
  frame_->hide();
  QWidget::closeEvent(event);
}
