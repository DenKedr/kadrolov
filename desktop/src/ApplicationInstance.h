#pragma once
#include <QLocalServer>
#include <QLockFile>
#include <QObject>
#include <memory>
class ApplicationInstance : public QObject {
  Q_OBJECT
public:
  enum Result { Primary, Secondary, Error };
  explicit ApplicationInstance(
      const QString &identity = QStringLiteral("kadrolov"),
      QObject *parent = nullptr);
  Result start(bool background, QString *error = nullptr);
signals:
  void showRequested();

private:
  QString identity_;
  std::unique_ptr<QLockFile> lock_;
  QLocalServer server_;
};
