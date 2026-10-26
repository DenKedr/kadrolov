#include "ApplicationInstance.h"
#include <QCryptographicHash>
#include <QDir>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTimer>
ApplicationInstance::ApplicationInstance(const QString &identity,
                                         QObject *parent)
    : QObject(parent), identity_(identity) {
  server_.setSocketOptions(QLocalServer::UserAccessOption);
  connect(&server_, &QLocalServer::newConnection, this, [this] {
    while (server_.hasPendingConnections()) {
      auto *socket = server_.nextPendingConnection();
      auto buffer = std::make_shared<QByteArray>();
      connect(socket, &QLocalSocket::disconnected, socket,
              &QObject::deleteLater);
      auto read = [this, socket, buffer] {
        *buffer += socket->read(65 - buffer->size());
        if (buffer->size() > 64) {
          socket->abort();
          return;
        }
        if (buffer->contains('\n')) {
          if (*buffer == "show\n")
            emit showRequested();
          socket->disconnectFromServer();
        }
      };
      connect(socket, &QLocalSocket::readyRead, this, read);
      QTimer::singleShot(2000, socket, [socket] { socket->abort(); });
      read();
    }
  });
}
ApplicationInstance::Result ApplicationInstance::start(bool background,
                                                       QString *error) {
  QString dir =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  if (!QDir().mkpath(dir)) {
    if (error)
      *error = QStringLiteral("Не удалось открыть папку приложения.");
    return Error;
  }
  lock_ = std::make_unique<QLockFile>(dir + "/" + identity_ + ".lock");
  lock_->setStaleLockTime(0);
  // AF_UNIX names are capped around 100 bytes on macOS. Hash the
  // per-user data location instead of passing its potentially long path.
  const auto digest = QCryptographicHash::hash((dir + "/" + identity_).toUtf8(),
                                               QCryptographicHash::Sha256)
                          .toHex()
                          .left(20);
#ifdef Q_OS_WIN
  const QString name = "kadrolov-" + QString::fromLatin1(digest);
#else
  const QString name = "/tmp/kadrolov-" + QString::fromLatin1(digest);
#endif
  if (!lock_->tryLock()) {
    if (background)
      return Secondary;
    QLocalSocket socket;
    for (int retry = 0; retry < 10; ++retry) {
      socket.connectToServer(name);
      if (socket.waitForConnected(200)) {
        socket.write("show\n");
        socket.flush();
        if (socket.waitForBytesWritten(500) || socket.bytesToWrite() == 0)
          return Secondary;
      }
      socket.abort();
    }
    if (error)
      *error = QStringLiteral("Кадролов уже запущен, но окно пока не отвечает. "
                              "Повторите запуск через несколько секунд.");
    return Error;
  }
  QLocalServer::removeServer(name);
  if (!server_.listen(name)) {
    if (error)
      *error = server_.errorString();
    lock_->unlock();
    return Error;
  }
  return Primary;
}
