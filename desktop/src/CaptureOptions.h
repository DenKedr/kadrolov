#pragma once
#include <QRect>
#include <QString>
#include <QScreen>
struct CaptureOptions {
    QScreen *screen = nullptr;
    QRect region; // global logical desktop coordinates; empty = selected screen
    QString outputPath;
    QString format = "mp4";
    QString audioMode = "none"; // none, system, mic, both
    QString microphoneDevice;
    QString systemAudioDevice;
    int fps = 30;
    int width = 0, height = 0; // 0 = source; even encoder dimensions
    int quality = 23; // CRF, 18 high / 23 normal / 28 compact
    bool showCursor = true;
    bool highlightClicks = false;
    bool camera = false;
    QString cameraDevice;
};
