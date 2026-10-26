#pragma once
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QKeySequence>
class GlobalHotkeys : public QObject, public QAbstractNativeEventFilter {
 Q_OBJECT
public:
 explicit GlobalHotkeys(QObject *parent=nullptr);
 ~GlobalHotkeys() override;
 bool screenshotAvailable() const { return screenshotAvailable_; }
 bool recordingAvailable() const { return recordingAvailable_; }
 QString statusText() const;
 bool setShortcuts(const QKeySequence &screenshot,const QKeySequence &video,QString *error=nullptr);
 QKeySequence screenshotShortcut() const { return screenshot_; }
 QKeySequence videoShortcut() const { return video_; }
 static QKeySequence defaultScreenshot();
 static QKeySequence defaultVideo();
#if QT_VERSION >= QT_VERSION_CHECK(6,0,0)
 bool nativeEventFilter(const QByteArray&,void*,qintptr*) override;
#else
 bool nativeEventFilter(const QByteArray&,void*,long*) override;
#endif
signals:
 void screenshotRequested();
 void recordingRequested();
private:
 void release();
 bool bind();
 void *native_=nullptr;
 bool screenshotAvailable_=false,recordingAvailable_=false;
 QKeySequence screenshot_,video_;
};
