#pragma once
#include <QList>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>
class QWidget;
// Visible click feedback is independent of screen-capture cursor composition.
// The recorder owns activation and must stop polling whenever capture is
// paused.
class ClickOverlay : public QObject {
  Q_OBJECT
public:
  explicit ClickOverlay(QObject *parent = nullptr);
  ~ClickOverlay() override;
  static bool isSupported();
  void setCaptureRect(const QRect &globalRect) { captureRect_ = globalRect; }
  void start();
  void stop();

private:
  void poll();
  void showClick(const QPoint &point, bool rightButton);
  QTimer timer_;
  QRect captureRect_;
  QList<QPointer<QWidget>> circles_;
  void *display_ = nullptr;
  bool initialized_ = false, leftDown_ = false, rightDown_ = false;
};
