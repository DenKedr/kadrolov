#include "DeviceInventory.h"
#include <QCoreApplication>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QFileInfo>
QList<AudioDevice> DeviceInventory::microphones(){
 QProcess p;
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
 QString name="kadrolov-capture";
#ifdef Q_OS_WIN
 name+=".exe";
#endif
 QString helper=QCoreApplication::applicationDirPath()+"/"+name;
 if(!QFileInfo(helper).isExecutable())helper=QStandardPaths::findExecutable(name);
 if(helper.isEmpty())return {};
 p.start(helper,{"--list-microphones"});
#else
 p.start("pactl",{"-f","json","list","sources"});
#endif
 if(!p.waitForFinished(3000)){p.kill();p.waitForFinished(1000);return {};}
 const QByteArray bytes=p.readAllStandardOutput().left(1024*1024);
 const auto json=QJsonDocument::fromJson(bytes);
 QList<AudioDevice> result;
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
 for(const auto &value:json.object()["microphones"].toArray()){const auto o=value.toObject();const QString id=o["id"].toString();if(!id.isEmpty())result.append({id,o["name"].toString(id)});}
#else
 for(const auto &value:json.array()){const auto o=value.toObject();const QString id=o["name"].toString();if(id.isEmpty()||id.endsWith(".monitor")||o["monitor_of_sink"].toInt(-1)!=-1)continue;const auto props=o["properties"].toObject();result.append({id,o["description"].toString(props["device.description"].toString(id))});}
#endif
 return result;
}
