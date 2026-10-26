#include "ClickOverlay.h"
#include <QColor>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QPainter>
#include <QScreen>
#include <QWidget>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(Q_OS_LINUX) && defined(KADROLOV_HAVE_X11)
#include <X11/Xlib.h>
#endif
namespace {
class ClickCircle final : public QWidget {
public:
  ClickCircle(const QPoint &point, bool rightButton)
      : QWidget(nullptr),
        color_(rightButton ? QColor("#2563EB") : QColor("#F97316")) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint | Qt::WindowTransparentForInput |
                   Qt::WindowDoesNotAcceptFocus);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::NoFocus);
    setGeometry(point.x() - 48, point.y() - 48, 96, 96);
    clock_.start();
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, [this] {
      if (clock_.elapsed() >= 600)
        close();
      else
        update();
    });
    timer_.start();
    show();
#ifdef Q_OS_WIN
    auto handle = reinterpret_cast<HWND>(winId());
    auto style = GetWindowLongPtr(handle, GWL_EXSTYLE);
    SetWindowLongPtr(handle, GWL_EXSTYLE,
                     style | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
                         WS_EX_TOOLWINDOW);
    SetWindowPos(handle, HWND_TOPMOST, point.x() - 48, point.y() - 48, 96, 96,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
#endif
  }

protected:
  void paintEvent(QPaintEvent *) override {
    const qreal progress = qBound(0.0, clock_.elapsed() / 600.0, 1.0);
    const qreal radius = 10.0 + progress * 28.0;
    QColor edge = color_;
    edge.setAlphaF((1.0 - progress) * 0.95);
    QColor fill = color_;
    fill.setAlphaF((1.0 - progress) * 0.18);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(edge, 3));
    painter.setBrush(fill);
    painter.drawEllipse(QPointF(48, 48), radius, radius);
  }

private:
  QElapsedTimer clock_;
  QTimer timer_;
  QColor color_;
};
} // namespace
ClickOverlay::ClickOverlay(QObject *parent) : QObject(parent) {
  timer_.setInterval(16);
  connect(&timer_, &QTimer::timeout, this, &ClickOverlay::poll);
}
ClickOverlay::~ClickOverlay() { stop(); }
bool ClickOverlay::isSupported() {
#ifdef Q_OS_WIN
  return true;
#elif defined(Q_OS_LINUX) && defined(KADROLOV_HAVE_X11)
  if (qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland" ||
      qEnvironmentVariableIsEmpty("DISPLAY"))
    return false;
  auto *display = XOpenDisplay(nullptr);
  if (!display)
    return false;
  XCloseDisplay(display);
  return true;
#else
  return false; // macOS feedback is composed by the ScreenCaptureKit helper.
#endif
}
void ClickOverlay::start() {
  stop();
  if (!isSupported())
    return;
#if defined(Q_OS_LINUX) && defined(KADROLOV_HAVE_X11)
  display_ = XOpenDisplay(nullptr);
  if (!display_)
    return;
#endif
  initialized_ = false;
  timer_.start();
}
void ClickOverlay::stop() {
  timer_.stop();
  for (auto &circle : circles_)
    if (circle)
      delete circle.data();
  circles_.clear();
#if defined(Q_OS_LINUX) && defined(KADROLOV_HAVE_X11)
  if (display_)
    XCloseDisplay(static_cast<Display *>(display_));
#endif
  display_ = nullptr;
  initialized_ = false;
}
void ClickOverlay::poll() {
  QPoint point;
  bool left = false, right = false;
#ifdef Q_OS_WIN
  POINT position{};
  if (!GetCursorPos(&position))
    return;
  point = QPoint(position.x, position.y);
  left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
  right = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
#elif defined(Q_OS_LINUX) && defined(KADROLOV_HAVE_X11)
  auto *display = static_cast<Display *>(display_);
  if (!display)
    return;
  Window root{}, child{};
  int rootX = 0, rootY = 0, localX = 0, localY = 0;
  unsigned int mask = 0;
  if (!XQueryPointer(display, DefaultRootWindow(display), &root, &child, &rootX,
                     &rootY, &localX, &localY, &mask))
    return;
  point = QPoint(rootX, rootY);
  left = (mask & Button1Mask) != 0;
  right = (mask & Button3Mask) != 0;
#else
  return;
#endif
  if (initialized_) {
    if (left && !leftDown_)
      showClick(point, false);
    if (right && !rightDown_)
      showClick(point, true);
  }
  leftDown_ = left;
  rightDown_ = right;
  initialized_ = true;
}
void ClickOverlay::showClick(const QPoint &point, bool rightButton) {
  if (!captureRect_.isEmpty() && !captureRect_.contains(point))
    return;
  for (int i = circles_.size() - 1; i >= 0; --i)
    if (circles_[i].isNull())
      circles_.removeAt(i);
  // Bound the number of transient native surfaces during rapid clicking.
  while (circles_.size() >= 24) {
    if (circles_.first())
      delete circles_.first().data();
    circles_.removeFirst();
  }
  circles_.append(new ClickCircle(point, rightButton));
}
