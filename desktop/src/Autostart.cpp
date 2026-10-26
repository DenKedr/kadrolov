#include "Autostart.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
static QString entry(){
#ifdef Q_OS_MACOS
 return QDir::homePath()+"/Library/LaunchAgents/ru.kadrolov.autostart.plist";
#else
 return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)+"/autostart/kadrolov.desktop";
#endif
}
bool Autostart::isEnabled(){
#ifdef Q_OS_WIN
 QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",QSettings::NativeFormat);return run.contains("Kadrolov");
#else
 return QFile::exists(entry());
#endif
}
bool Autostart::setEnabled(bool enabled,QString*error){
#ifdef Q_OS_WIN
 QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",QSettings::NativeFormat);if(enabled)run.setValue("Kadrolov",'"'+QDir::toNativeSeparators(QCoreApplication::applicationFilePath())+"\" --background");else run.remove("Kadrolov");run.sync();if(run.status()!=QSettings::NoError){if(error)*error=QStringLiteral("Не удалось изменить автозапуск текущего пользователя.");return false;}return true;
#else
 const QString path=entry();if(!enabled){if(!QFile::exists(path)||QFile::remove(path))return true;if(error)*error=QStringLiteral("Не удалось отключить автозапуск.");return false;}
 QDir().mkpath(QFileInfo(path).absolutePath());QString executable=QCoreApplication::applicationFilePath();QString content;
#ifdef Q_OS_MACOS
 content="<?xml version=\"1.0\" encoding=\"UTF-8\"?><!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\"><plist version=\"1.0\"><dict><key>Label</key><string>ru.kadrolov.autostart</string><key>ProgramArguments</key><array><string>"+executable.toHtmlEscaped()+"</string><string>--background</string></array><key>RunAtLoad</key><true/></dict></plist>";
#else
 executable.replace("\\","\\\\").replace("\"","\\\"").replace("`","\\`").replace("$","\\$").replace("%","%%");content="[Desktop Entry]\nType=Application\nName=Кадролов\nExec=\""+executable+"\" --background\nTerminal=false\nX-GNOME-Autostart-enabled=true\n";
#endif
 QSaveFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(content.toUtf8())<0||!f.commit()){if(error)*error=QStringLiteral("Не удалось сохранить настройку автозапуска.");return false;}return true;
#endif
}
QString Autostart::description(){return QStringLiteral("Запускать Кадролов при входе в систему. Захват и запись сами не начинаются. После перемещения приложения включите автозапуск заново.");}
