#include "RecordingPlan.h"
#include <QFileInfo>
QString validateCaptureOptions(const CaptureOptions &o) {
    if(o.outputPath.trimmed().isEmpty()) return QStringLiteral("Выберите место сохранения видео.");
    if(QFileInfo::exists(o.outputPath)) return QStringLiteral("Файл уже существует. Выберите другое имя.");
    if(o.fps<1 || o.fps>60) return QStringLiteral("Частота кадров должна быть от 1 до 60.");
    if(!QStringList{"none","system","mic","both"}.contains(o.audioMode)) return QStringLiteral("Неизвестный режим звука.");
    if(!QStringList{"mp4","mkv","webm"}.contains(o.format.toLower())) return QStringLiteral("Выберите MP4, MKV или WebM.");
    if(o.width<0 || o.height<0 || (o.width>0 && o.width<2) || (o.height>0 && o.height<2)) return QStringLiteral("Размер видео должен быть не меньше двух пикселей.");
    if(o.quality<0 || o.quality>51) return QStringLiteral("Недопустимое качество видео.");
    return {};
}
QStringList encoderOutputArguments(const CaptureOptions &o,const QString &output) {
    QStringList a;
    if(o.width>0 && o.height>0) a << "-vf" << QString("scale=%1:%2:force_original_aspect_ratio=decrease,pad=%1:%2:(ow-iw)/2:(oh-ih)/2").arg(o.width/2*2).arg(o.height/2*2);
    else a << "-vf" << "scale=trunc(iw/2)*2:trunc(ih/2)*2";
    if(o.format=="webm") a << "-c:v" << "libvpx-vp9" << "-deadline" << "realtime" << "-cpu-used" << "6" << "-b:v" << "0" << "-crf" << QString::number(o.quality+8);
    else a << "-c:v" << "libx264" << "-preset" << "veryfast" << "-crf" << QString::number(o.quality);
    a << "-pix_fmt" << "yuv420p";
    if(o.audioMode=="none") a << "-an";
    else a << "-c:a" << (o.format=="webm"?"libopus":"aac") << "-b:a" << "160k";
    if(o.format=="mp4") a << "-movflags" << "+faststart";
    a << "-y" << output;
    return a;
}
