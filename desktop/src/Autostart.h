#pragma once
#include <QString>
class Autostart {
public:
 static bool isEnabled();
 static bool setEnabled(bool enabled,QString *error=nullptr);
 static QString description();
};
