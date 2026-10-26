#pragma once
#include <QList>
#include <QString>
struct AudioDevice { QString id; QString name; };
class DeviceInventory {
public:
 static QList<AudioDevice> microphones();
};
