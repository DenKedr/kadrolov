#include "ApplicationInstance.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
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
          if (*buffer == "show\n") {
            emit showRequested();
            socket->write("ok\n");
          }
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
        if (socket.write("show\n") != 5)
          break;
        socket.flush();
        // A successful pipe write alone does not prove that the primary
        // accepted the command. Keep this connection until it acknowledges.
        QByteArray reply;
        QElapsedTimer deadline;
        deadline.start();
        while (reply.size() < 4 && deadline.elapsed() < 2000) {
          reply += socket.read(4 - reply.size());
          if (reply.contains('\n'))
            break;
          if (!socket.waitForReadyRead(
                  qMax(1, 2000 - int(deadline.elapsed())))) {
            reply += socket.read(4 - reply.size());
            break;
          }
        }
        if (reply == "ok\n")
          return Secondary;
        break; // Never replay an unacknowledged show command.
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
