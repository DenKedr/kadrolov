#include "Recorder.h"
#include "RecordingPlan.h"
#include "ClickOverlay.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QUuid>
#include <QStorageInfo>
#include <QOperatingSystemVersion>
#include <QRegularExpression>
#ifdef Q_OS_MACOS
#include <CoreGraphics/CoreGraphics.h>
#endif

static QString companion(const QString &name) {
    const QString dir=QCoreApplication::applicationDirPath();
    for(const auto &p : QStringList{dir+"/"+name,dir+"/native/"+name,dir+"/../native/"+name})
        if(QFileInfo(p).isExecutable()) return p;
    return QStandardPaths::findExecutable(name);
}
Recorder::Recorder(QObject *parent):QObject(parent) {
    clickOverlay_=new ClickOverlay(this);
    connect(&segmentMerger_,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,[this](int code,QProcess::ExitStatus status){
        if(state_==Failed)return;
        if(code!=0||status!=QProcess::NormalExit){fail(QStringLiteral("Не удалось объединить видео со звуком. ")+QString::fromUtf8(segmentMerger_.readAllStandardError().right(1000)));return;}
        rawVideo_=temporaryDir_+QString("/merged-%1.mkv").arg(segments_.size());audioFiles_.clear();acceptSegment();
    });
    ticker_.setInterval(200);
    connect(&ticker_,&QTimer::timeout,this,[this]{
        if(state_==Recording) emit elapsedChanged(elapsed_+clock_.elapsed());
        if(state_==Recording&&!helper_&&clock_.elapsed()>15*60*1000){elapsed_+=clock_.elapsed();autoNext_=true;setState(Starting);process_.write("q\n");for(auto *p:audioProcesses_)p->write("stop\n");}
        const QStorageInfo storage(QFileInfo(options_.outputPath).absolutePath());
        if(state_==Recording && storage.isValid() && storage.bytesAvailable()<64*1024*1024) {
            stop(); emit error(QStringLiteral("Мало места. Запись остановлена; сохраняю записанную часть."));
        }
    });
    connect(&process_,&QProcess::readyReadStandardOutput,this,&Recorder::readHelper);
    connect(&process_,&QProcess::readyReadStandardError,this,[this]{diagnostics_+=process_.readAllStandardError();if(diagnostics_.size()>16384) diagnostics_=diagnostics_.right(16384);});
    connect(&process_,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),this,&Recorder::segmentEnded);
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart)fail(QStringLiteral("Не удалось запустить механизм записи: ")+process_.errorString());});
}
Recorder::~Recorder() {
    ticker_.stop();
    if(process_.state()!=QProcess::NotRunning){process_.write(helper_?"{\"command\":\"stop\"}\n":"q\n");if(!process_.waitForFinished(3000)){process_.terminate();process_.waitForFinished(1000);}}
    for(auto *p:audioProcesses_){if(p->state()!=QProcess::NotRunning){p->write("stop\n");p->waitForFinished(1000);p->kill();}}
}
QString Recorder::ffmpegPath() {
    const QString bundled=companion("ffmpeg"
#ifdef Q_OS_WIN
        ".exe"
#endif
    );
    if(!bundled.isEmpty())return bundled;
#ifdef Q_OS_MACOS
    for(const auto &p:QStringList{"/opt/homebrew/bin/ffmpeg","/usr/local/bin/ffmpeg"})if(QFileInfo(p).isExecutable())return p;
#endif
    return {};
}
bool Recorder::supportsClicks(){
#ifdef Q_OS_MACOS
    return QOperatingSystemVersion::current()>=QOperatingSystemVersion(QOperatingSystemVersion::MacOS,15);
#else
    return ClickOverlay::isSupported();
#endif
}
bool Recorder::supportsSystemAudio(){
#ifdef Q_OS_MACOS
    return !companion("kadrolov-capture").isEmpty();
#elif defined(Q_OS_WIN)
    return !companion("kadrolov-capture.exe").isEmpty()||!companion("kadrolov-audio.exe").isEmpty();
#else
    return true;
#endif
}
QString Recorder::capabilityNote(){
#ifdef Q_OS_MACOS
    return QStringLiteral("Для записи разрешите доступ к экрану в настройках macOS. Подсветка кликов: macOS 15 и новее.");
#elif defined(Q_OS_WIN)
    return QStringLiteral("Захват Windows через GDI. Системный звук — WASAPI; подсветка кликов пока недоступна.");
#else
    return qEnvironmentVariableIsSet("WAYLAND_DISPLAY") ? QStringLiteral("Wayland: источник выбирается в системном диалоге. Подсветка кликов зависит от окружения.") : QStringLiteral("X11: запись через FFmpeg, звук через PulseAudio/PipeWire. Подсветка кликов пока недоступна.");
#endif
}
void Recorder::setState(State s){if(state_==s)return;state_=s;if(clickOverlay_){if(s==Recording&&options_.highlightClicks&&!helper_){clickOverlay_->setCaptureRect(options_.region);clickOverlay_->start();}else clickOverlay_->stop();}emit stateChanged(s);}
void Recorder::fail(const QString &message){ticker_.stop();
    if(process_.state()!=QProcess::NotRunning){process_.write(helper_?"{\"command\":\"stop\"}\n":"q\n");const auto pid=process_.processId();QTimer::singleShot(3000,this,[this,pid]{if(pid&&process_.processId()==pid&&process_.state()!=QProcess::NotRunning)process_.terminate();});}
    for(auto *p:audioProcesses_)if(p->state()!=QProcess::NotRunning)p->write("stop\n");
    setState(Failed);emit error(message+QStringLiteral("\nНезавершённые файлы: ")+temporaryDir_);}
bool Recorder::start(const CaptureOptions &o){
    if(state_!=Idle && state_!=Failed)return false;
    if(process_.state()!=QProcess::NotRunning||segmentMerger_.state()!=QProcess::NotRunning){emit error(QStringLiteral("Предыдущая запись ещё закрывается. Подождите несколько секунд."));return false;}
    const QString validation=validateCaptureOptions(o);
    if(!validation.isEmpty()){emit error(validation);return false;}
    if(!o.screen){emit error(QStringLiteral("Выберите экран."));return false;}
#if defined(Q_OS_WIN)
    const bool nativeWindows=!companion("kadrolov-capture.exe").isEmpty();
    if(!nativeWindows){emit error(QStringLiteral("Не найден встроенный модуль записи. Распакуйте пакет Кадролова целиком."));return false;}
    if(o.format!="mp4"&&ffmpegPath().isEmpty()){emit error(QStringLiteral("Для WebM/MKV нужен FFmpeg. MP4 записывается встроенным модулем."));return false;}
#elif !defined(Q_OS_MACOS)
    if(ffmpegPath().isEmpty() && !qEnvironmentVariableIsSet("WAYLAND_DISPLAY")){emit error(QStringLiteral("Не найден FFmpeg. Установите FFmpeg или используйте полный пакет Кадролова."));return false;}
#else
    if(ffmpegPath().isEmpty() && o.format!="mp4"){emit error(QStringLiteral("Для MKV/WebM нужен FFmpeg. Выберите MP4 либо установите FFmpeg."));return false;}
#endif
    if(o.highlightClicks && !supportsClicks()){emit error(QStringLiteral("Подсветка кликов недоступна в этом окружении."));return false;}
    if((o.audioMode=="system"||o.audioMode=="both")&&!supportsSystemAudio()){
        emit error(QStringLiteral("Системный звук недоступен в этом окружении. Выберите другой режим звука."));return false;
    }
    options_=o; options_.region=o.region.isEmpty()?o.screen->geometry():o.region;
    if(!o.screen->geometry().contains(options_.region)){emit error(QStringLiteral("Область должна находиться на выбранном экране."));return false;}
    if(!QDir().mkpath(QFileInfo(o.outputPath).absolutePath())){emit error(QStringLiteral("Не удаётся создать папку сохранения."));return false;}
    temporaryDir_=QFileInfo(o.outputPath).absolutePath()+"/.kadrolov-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(!QDir().mkdir(temporaryDir_)){emit error(QStringLiteral("Нет доступа к папке сохранения."));return false;}
    elapsed_=0;segments_.clear();diagnostics_.clear();helperBuffer_.clear();helperAudioTracks_=0;stopping_=false;pausePending_=false;postprocessing_=false;autoNext_=false;saveRecoveryManifest();
    setState(Starting);
#ifdef Q_OS_MACOS
    const QString helper=companion("kadrolov-capture");
    if(helper.isEmpty()){fail(QStringLiteral("Не найден модуль ScreenCaptureKit."));return false;}
    helper_=true;
    const auto r=options_.region;
    // QScreen name on macOS encodes no stable CG ID; match actual global bounds.
    uint32_t count=0;CGGetActiveDisplayList(0,nullptr,&count);QVector<CGDirectDisplayID> ids(int(count),0);CGGetActiveDisplayList(count,ids.data(),&count);
    CGDirectDisplayID display=CGMainDisplayID();
    for(auto id:ids){auto b=CGDisplayBounds(id);if(qRound(b.origin.x)==o.screen->geometry().x()&&qRound(b.origin.y)==o.screen->geometry().y()){display=id;break;}}
    rawVideo_=temporaryDir_+"/capture.mp4";
    QStringList a{"--output",rawVideo_,"--display",QString::number(display),"--rect",QString("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()),"--fps",QString::number(o.fps),"--width",QString::number(o.width),"--height",QString::number(o.height),"--audio",o.audioMode,"--cursor",o.showCursor?"1":"0","--clicks",o.highlightClicks?"1":"0","--quality",QString::number(o.quality)};
    a << "--exclude-pid" << QString::number(QCoreApplication::applicationPid()) << "--camera" << (o.camera?"1":"0");
    if(!o.cameraDevice.isEmpty())a<<"--camera-device"<<o.cameraDevice;
    if(!o.microphoneDevice.isEmpty())a<<"--microphone"<<o.microphoneDevice;
    process_.start(helper,a);
#elif defined(Q_OS_WIN)
    helper_=nativeWindows;
    if(helper_){
        rawVideo_=temporaryDir_+"/capture.mp4";
        const auto r=options_.region;const auto d=o.screen->devicePixelRatio();
        QStringList a{"--output",rawVideo_,"--rect",QString("%1,%2,%3,%4").arg(qRound(r.x()*d)).arg(qRound(r.y()*d)).arg(qRound(r.width()*d)).arg(qRound(r.height()*d)),"--fps",QString::number(o.fps),"--width",QString::number(o.width),"--height",QString::number(o.height),"--quality",QString::number(o.quality),"--audio",o.audioMode,"--cursor",o.showCursor?"1":"0","--clicks",o.highlightClicks?"1":"0","--camera",o.camera?"1":"0"};
        if(!o.cameraDevice.isEmpty())a<<"--camera-device"<<o.cameraDevice;
        if(!o.microphoneDevice.isEmpty())a<<"--microphone"<<o.microphoneDevice;
        process_.start(companion("kadrolov-capture.exe"),a);
    }else startSegment();
#elif defined(Q_OS_LINUX)
    helper_=qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
    if(helper_){
        QString script=QCoreApplication::applicationDirPath()+"/portal_capture.py";
        if(!QFileInfo::exists(script))script=QStringLiteral(KADROLOV_SOURCE_DIR "/native/linux/portal_capture.py");
        if(!QFileInfo::exists(script)){fail(QStringLiteral("Модуль Wayland отсутствует. Используйте сессию X11."));return false;}
        rawVideo_=temporaryDir_+"/capture."+o.format;
        const auto r=options_.region;
        QStringList a{script,"--output",rawVideo_,"--rect",QString("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()),"--audio",o.audioMode,"--cursor",o.showCursor?"1":"0","--fps",QString::number(o.fps),"--width",QString::number(o.width),"--height",QString::number(o.height),"--quality",QString::number(o.quality),"--microphone",o.microphoneDevice,"--system-device",o.systemAudioDevice,"--camera",o.camera?"1":"0"};
        if(!o.cameraDevice.isEmpty())a<<"--camera-device"<<o.cameraDevice;
        process_.start("python3",a);
    }else startSegment();
#else
    helper_=false;startSegment();
#endif
    return true;
}
void Recorder::readHelper(){
    const QByteArray bytes=process_.readAllStandardOutput();
    if(!helper_)return;
    helperBuffer_+=bytes;
    while(helperBuffer_.contains('\n')){
        const int at=helperBuffer_.indexOf('\n');const auto line=helperBuffer_.left(at);helperBuffer_.remove(0,at+1);
        const auto d=QJsonDocument::fromJson(line);if(!d.isObject())continue;
        const auto obj=d.object();const QString e=obj["event"].toString();
        if(e=="started"){clock_.restart();setState(Recording);ticker_.start();}
        else if(e=="paused"){if(state_==Recording)elapsed_+=clock_.elapsed();setState(Paused);emit elapsedChanged(elapsed_);}
        else if(e=="resumed"){clock_.restart();setState(Recording);}
        else if(e=="finished"){helperAudioTracks_=obj["audioTracks"].toInt(0);}
        else if(e=="error")fail(obj["message"].toString(QStringLiteral("Ошибка захвата экрана.")));
    }
}
void Recorder::startSegment(){
    if(stopping_){finalize();return;}
    QStringList a{"-hide_banner","-loglevel","warning","-thread_queue_size","1024"};
    const qreal dpr=options_.screen->devicePixelRatio();const auto r=options_.region;
    const int x=qRound(r.x()*dpr), y=qRound(r.y()*dpr), w=qRound(r.width()*dpr)/2*2,h=qRound(r.height()*dpr)/2*2;
    QString videoMap="0:v";
    int audioBase=1;
#ifdef Q_OS_WIN
    a<<"-f"<<"gdigrab"<<"-framerate"<<QString::number(options_.fps)<<"-draw_mouse"<<(options_.showCursor?"1":"0")<<"-offset_x"<<QString::number(x)<<"-offset_y"<<QString::number(y)<<"-video_size"<<QString("%1x%2").arg(w).arg(h)<<"-i"<<"desktop";
    audioFiles_.clear();
    for(auto *p:audioProcesses_)p->deleteLater();audioProcesses_.clear();
    for(const auto &mode:QStringList{"system","mic"}){
        if(options_.audioMode!=mode&&options_.audioMode!="both")continue;
        const QString path=temporaryDir_+QString("/audio-%1-%2.wav").arg(segments_.size()).arg(mode);
        auto *p=new QProcess(this);p->start(companion("kadrolov-audio.exe"),{"--output",path,"--mode",mode});
        if(!p->waitForStarted(3000)){fail(QStringLiteral("Не удалось открыть звуковое устройство."));p->deleteLater();return;}
        audioProcesses_<<p;audioFiles_<<path;
    }
#else
    a<<"-f"<<"x11grab"<<"-framerate"<<QString::number(options_.fps)<<"-draw_mouse"<<(options_.showCursor?"1":"0")<<"-video_size"<<QString("%1x%2").arg(w).arg(h)<<"-i"<<QString("%1+%2,%3").arg(qEnvironmentVariable("DISPLAY",":0.0")).arg(x).arg(y);
#endif
    if(options_.camera){
#ifdef Q_OS_WIN
        QString device=options_.cameraDevice;
        if(device.isEmpty()){
            QProcess list;list.start(ffmpegPath(),{"-hide_banner","-list_devices","true","-f","dshow","-i","dummy"});list.waitForFinished(5000);
            const auto match=QRegularExpression("\"([^\"]+)\" \\(video\\)").match(QString::fromUtf8(list.readAllStandardError()));
            if(match.hasMatch())device=match.captured(1);
        }
        if(device.isEmpty()){fail(QStringLiteral("Не найдена камера. Укажите её имя в дополнительных параметрах."));return;}
        a<<"-thread_queue_size"<<"1024"<<"-f"<<"dshow"<<"-i"<<("video="+device);
#else
        a<<"-thread_queue_size"<<"1024"<<"-f"<<"v4l2"<<"-framerate"<<QString::number(options_.fps)<<"-i"<<(options_.cameraDevice.isEmpty()?"/dev/video0":options_.cameraDevice);
#endif
        audioBase=2;videoMap="[v]";
    }
#ifndef Q_OS_WIN
    int inputs=0;
    if(options_.audioMode=="system"||options_.audioMode=="both"){
        QString monitor=options_.systemAudioDevice;
        if(monitor.isEmpty()){
            QProcess p;p.start("pactl",{"get-default-sink"});p.waitForFinished(2000);monitor=QString::fromUtf8(p.readAllStandardOutput()).trimmed();
            if(monitor.isEmpty()){fail(QStringLiteral("Не найден системный звуковой выход PulseAudio/PipeWire."));return;}monitor+=".monitor";
        }
        a<<"-thread_queue_size"<<"1024"<<"-f"<<"pulse"<<"-i"<<monitor;++inputs;
    }
    if(options_.audioMode=="mic"||options_.audioMode=="both"){a<<"-thread_queue_size"<<"1024"<<"-f"<<"pulse"<<"-i"<<(options_.microphoneDevice.isEmpty()?"default":options_.microphoneDevice);++inputs;}
    QString filter;
    if(options_.camera)filter=QString("[1:v]scale=%1:-2[cam];[0:v][cam]overlay=W-w-24:H-h-24[v]").arg(qMax(64,w/4)/2*2);
    if(inputs==2){if(!filter.isEmpty())filter+=";";filter+=QString("[%1:a][%2:a]amix=inputs=2:duration=longest:normalize=0[a]").arg(audioBase).arg(audioBase+1);}
    if(!filter.isEmpty())a<<"-filter_complex"<<filter;
    a<<"-map"<<videoMap;if(inputs==2)a<<"-map"<<"[a]";else if(inputs==1)a<<"-map"<<QString("%1:a").arg(audioBase);
#else
    if(options_.camera)a<<"-filter_complex"<<QString("[1:v]scale=%1:-2[cam];[0:v][cam]overlay=W-w-24:H-h-24[v]").arg(qMax(64,w/4)/2*2);
    a<<"-map"<<videoMap;
#endif
    CaptureOptions enc=options_;enc.format=options_.format=="webm"?"webm":"mkv";
#ifdef Q_OS_WIN
    enc.audioMode="none";
#endif
    rawVideo_=temporaryDir_+QString("/segment-%1.mkv").arg(segments_.size());
    a+=encoderOutputArguments(enc,rawVideo_);
    process_.start(ffmpegPath(),a);
    if(!process_.waitForStarted(3000)){fail(QStringLiteral("Не удалось запустить FFmpeg."));return;}
    clock_.restart();setState(Recording);ticker_.start();
}
void Recorder::pause(){
    if(state_!=Recording)return;
    if(helper_){process_.write("{\"command\":\"pause\"}\n");return;}
    elapsed_+=clock_.elapsed();pausePending_=true;setState(Starting);process_.write("q\n");
    for(auto *p:audioProcesses_)p->write("stop\n");
}
void Recorder::resume(){
    if(state_!=Paused)return;
    if(helper_){process_.write("{\"command\":\"resume\"}\n");return;}
    pausePending_=false;setState(Starting);startSegment();
}
void Recorder::stop(){
    if(state_==Idle||state_==Failed||state_==Finalizing)return;
    if(state_==Recording)elapsed_+=clock_.elapsed();
    stopping_=true;ticker_.stop();setState(Finalizing);
    if(segmentMerger_.state()!=QProcess::NotRunning)return;
    if(process_.state()!=QProcess::NotRunning){process_.write(helper_?"{\"command\":\"stop\"}\n":"q\n");for(auto *p:audioProcesses_)p->write("stop\n");}
    else finalize();
}
void Recorder::segmentEnded(int code,QProcess::ExitStatus status){
    readHelper();
    if(state_==Failed)return;
    if(postprocessing_){
        postprocessing_=false;
        if(code!=0||status!=QProcess::NormalExit||QFileInfo(encodedOutput_).size()<32){fail(QStringLiteral("Не удалось собрать итоговое видео. ")+QString::fromUtf8(diagnostics_.right(2000)));return;}
        // rename within the destination filesystem: no truncated public result.
        const QString p=options_.outputPath;
        if(QFileInfo::exists(p)||!QFile::rename(encodedOutput_,p)){fail(QStringLiteral("Не удалось переместить готовое видео. Проверьте папку и свободное место."));return;}
        QDir(temporaryDir_).removeRecursively();setState(Idle);emit completed(p);return;
    }
    if(code!=0||status!=QProcess::NormalExit||QFileInfo(rawVideo_).size()<32){fail(QStringLiteral("Запись не завершена. ")+QString::fromUtf8(diagnostics_.right(2000)));return;}
    for(auto *p:audioProcesses_){if(p->state()!=QProcess::NotRunning)p->write("stop\n");p->waitForFinished(3000);if(p->exitCode()!=0){fail(QStringLiteral("Не удалось сохранить звуковую дорожку."));return;}}
#ifdef Q_OS_WIN
    if(!audioFiles_.isEmpty()){
        QStringList a{"-hide_banner","-loglevel","error","-i",rawVideo_};for(const auto &p:audioFiles_)a<<"-i"<<p;
        if(audioFiles_.size()==2)a<<"-filter_complex"<<"[1:a][2:a]amix=inputs=2:duration=longest:normalize=0[a]"<<"-map"<<"0:v"<<"-map"<<"[a]";
        else a<<"-map"<<"0:v"<<"-map"<<"1:a";
        a<<"-c:v"<<"copy"<<"-c:a"<<"aac"<<"-shortest"<<"-y"<<(temporaryDir_+QString("/merged-%1.mkv").arg(segments_.size()));
        segmentMerger_.start(ffmpegPath(),a);return;
    }
#endif
    acceptSegment();
}
void Recorder::acceptSegment(){
    segments_<<rawVideo_;
    saveRecoveryManifest();
    if(helper_||stopping_){finalize();return;}
    if(autoNext_){autoNext_=false;startSegment();return;}
    if(pausePending_){setState(Paused);emit elapsedChanged(elapsed_);return;}
    fail(QStringLiteral("Источник записи неожиданно закрылся. Доступная часть сохранена во временной папке."));
}
void Recorder::saveRecoveryManifest(){
    if(temporaryDir_.isEmpty())return;QJsonArray pieces;for(const auto&p:segments_)pieces.append(QFileInfo(p).fileName());
    QJsonObject obj{{"version",1},{"segments",pieces},{"format",options_.format},{"audio",options_.audioMode},{"outputName",QFileInfo(options_.outputPath).fileName()}};
    QSaveFile f(temporaryDir_+"/session.json");if(f.open(QIODevice::WriteOnly)){f.write(QJsonDocument(obj).toJson());f.commit();}
}
void Recorder::finalize(){
    ticker_.stop();setState(Finalizing);
    if(segments_.isEmpty()){fail(QStringLiteral("Нет записанных кадров."));return;}
#ifdef Q_OS_MACOS
    if(helper_&&options_.format=="mp4"&&options_.audioMode!="both"){
        if(QFileInfo::exists(options_.outputPath)||!QFile::rename(segments_.first(),options_.outputPath)){fail(QStringLiteral("Не удалось сохранить готовое MP4."));return;}
        QDir(temporaryDir_).removeRecursively();setState(Idle);emit completed(options_.outputPath);return;
    }
    if(helper_&&options_.format=="mp4"&&options_.audioMode=="both"){
        encodedOutput_=temporaryDir_+"/final.mp4";
        postprocessing_=true;helper_=false;
        process_.start(companion("kadrolov-capture"),{"--mix-audio",segments_.first(),"--output",encodedOutput_});return;
    }
#elif defined(Q_OS_LINUX)
    if(helper_){
        if(QFileInfo::exists(options_.outputPath)||!QFile::rename(segments_.first(),options_.outputPath)){fail(QStringLiteral("Не удалось сохранить видео из системного портала."));return;}
        QDir(temporaryDir_).removeRecursively();setState(Idle);emit completed(options_.outputPath);return;
    }
#elif defined(Q_OS_WIN)
    if(helper_&&options_.format=="mp4"){
        if(QFileInfo::exists(options_.outputPath)||!QFile::rename(segments_.first(),options_.outputPath)){fail(QStringLiteral("Не удалось сохранить MP4."));return;}
        QDir(temporaryDir_).removeRecursively();setState(Idle);emit completed(options_.outputPath);return;
    }
#endif
    QStringList a{"-hide_banner","-loglevel","warning"};
    if(segments_.size()==1)a<<"-i"<<segments_.first();
    else {
        const QString list=temporaryDir_+"/segments.txt";QFile f(list);if(!f.open(QIODevice::WriteOnly)){fail(QStringLiteral("Не удалось подготовить сборку записи."));return;}
        for(const auto &p:segments_){QString escaped=p;escaped.replace("'","'\\''");f.write(QString("file '%1'\n").arg(escaped).toUtf8());}f.close();
        a<<"-f"<<"concat"<<"-safe"<<"0"<<"-i"<<list;
    }
    if(helper_&&options_.audioMode=="both"&&helperAudioTracks_==2)a<<"-filter_complex"<<"[0:a:0][0:a:1]amix=inputs=2:duration=longest:normalize=0[a]"<<"-map"<<"0:v"<<"-map"<<"[a]";
    else {a<<"-map"<<"0:v";if(options_.audioMode!="none")a<<"-map"<<"0:a:0";}
    encodedOutput_=temporaryDir_+"/final."+options_.format;
    a+=encoderOutputArguments(options_,encodedOutput_);postprocessing_=true;helper_=false;
    process_.start(ffmpegPath(),a);
}
