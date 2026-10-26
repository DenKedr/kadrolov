#include "Recorder.h"
#include <QApplication>
#include <QDebug>
#include <QScreen>
#include <QTimer>

// Explicit integration runner, never part of unattended CTest: captures real local media.
// recorder_smoke OUTPUT [none|system|mic|both] [x,y,w,h] [sparse]
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  if (argc < 2)
    return 2;
  Recorder recorder;
  CaptureOptions options;
  options.screen = app.primaryScreen();
  options.outputPath = QString::fromLocal8Bit(argv[1]);
  options.audioMode = argc > 2 ? QString::fromLatin1(argv[2]) : "none";
  options.width = 1280;
  options.height = 720;
  if (argc > 3) {
    const auto parts = QString::fromLatin1(argv[3]).split(',');
    if (parts.size() != 4)
      return 2;
    int values[4];
    for (int i = 0; i < 4; ++i) {
      bool ok = false;
      values[i] = parts[i].toInt(&ok);
      if (!ok)
        return 2;
    }
    options.region = QRect(values[0], values[1], values[2], values[3]);
    options.width = 600;
    options.height = 400;
  }
  const bool sparse = argc > 4 && QString::fromLatin1(argv[4]) == "sparse";
  QObject::connect(&recorder, &Recorder::error, &app, [&](const QString &error) {
    qWarning().noquote() << error;
    app.exit(1);
  });
  QObject::connect(&recorder, &Recorder::completed, &app, [&](const QString &path) {
    qInfo() << "COMPLETE" << path;
    app.exit(0);
  });
  int runs = 0;
  QObject::connect(&recorder, &Recorder::stateChanged, &app,
                   [&](Recorder::State state) {
    qInfo() << "STATE" << state;
    if (state == Recorder::Recording) {
      if (++runs == 1)
        QTimer::singleShot(sparse ? 6000 : 1500, &recorder, &Recorder::pause);
      else
        QTimer::singleShot(sparse ? 5000 : 1500, &recorder, &Recorder::stop);
    }
    if (state == Recorder::Paused)
      QTimer::singleShot(sparse ? 2000 : 1000, &recorder, &Recorder::resume);
  });
  QTimer::singleShot(0, &app, [&] {
    if (!recorder.start(options))
      app.exit(1);
  });
  QTimer::singleShot(45000, &app, [&] {
    qWarning() << "TIMEOUT";
    app.exit(3);
  });
  return app.exec();
}
