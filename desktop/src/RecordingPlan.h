#pragma once
#include "CaptureOptions.h"
QString validateCaptureOptions(const CaptureOptions &options);
QStringList encoderOutputArguments(const CaptureOptions &options, const QString &output);
