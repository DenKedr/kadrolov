#include "GlobalHotkeys.h"
#include "MainWindow.h"
#include <QApplication>
#include <QFontDatabase>
#include <QStyleFactory>
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  app.setApplicationName(QStringLiteral("Кадролов"));
  app.setOrganizationName(QStringLiteral("Kadrolov"));
  app.setOrganizationDomain(QStringLiteral("kadrolov.local"));
  app.setApplicationVersion("0.1.0");
  app.setStyle(QStyleFactory::create("Fusion"));
  app.setFont(QFontDatabase::systemFont(QFontDatabase::GeneralFont));
  MainWindow window;
  GlobalHotkeys hotkeys(&app);
  QObject::connect(&hotkeys, &GlobalHotkeys::screenshotRequested, &window,
                   &MainWindow::takeScreenshot);
  QObject::connect(&hotkeys, &GlobalHotkeys::recordingRequested, &window,
                   &MainWindow::startRecording);
  window.setHotkeys(&hotkeys);
  if (!app.arguments().contains("--background"))
    window.show();
  return app.exec();
}
