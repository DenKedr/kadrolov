#pragma once
#include "CaptureOptions.h"
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QElapsedTimer>
#include <QVector>
class ClickOverlay;
class Recorder : public QObject {
    Q_OBJECT
public:
    enum State { Idle, Starting, Recording, Paused, Finalizing, Failed };
    Q_ENUM(State)
    explicit Recorder(QObject *parent = nullptr);
    ~Recorder() override;
    bool start(const CaptureOptions &options);
    State state() const { return state_; }
    static QString ffmpegPath();
    static bool supportsClicks();
    static bool supportsSystemAudio();
    static QString capabilityNote();
public slots:
    void pause();
    void resume();
    void stop();
signals:
    void stateChanged(Recorder::State state);
    void elapsedChanged(qint64 milliseconds);
    void completed(const QString &path);
    void error(const QString &message);
private:
    void setState(State state);
    void readHelper();
    void startSegment();
    void segmentEnded(int code, QProcess::ExitStatus status);
    void finalize();
    void fail(const QString &message);
    CaptureOptions options_;
    State state_ = Idle;
    QProcess process_;
    QTimer ticker_;
    QElapsedTimer clock_;
    qint64 elapsed_ = 0;
    QStringList segments_;
    QString temporaryDir_;
    QByteArray helperBuffer_, diagnostics_;
    bool helper_ = false, stopping_ = false, pausePending_ = false;
    int helperAudioTracks_ = 0;
    QVector<QProcess*> audioProcesses_;
    QStringList audioFiles_;
    bool postprocessing_ = false;
    QString rawVideo_;
    QString encodedOutput_;
    QProcess segmentMerger_;
    void acceptSegment();
    ClickOverlay *clickOverlay_ = nullptr;
    bool autoNext_ = false;
    void saveRecoveryManifest();
};
