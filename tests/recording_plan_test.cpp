#include <QtTest>
#include "RecordingPlan.h"
class RecordingPlanTest : public QObject {
    Q_OBJECT
private slots:
    void invalidConfigurationIsRejected() {
        CaptureOptions o;
        QVERIFY(!validateCaptureOptions(o).isEmpty());
        o.outputPath="/tmp/kadrolov-test.mp4";
        o.fps=0;
        QVERIFY(!validateCaptureOptions(o).isEmpty());
        o.fps=30; o.audioMode="unexpected";
        QVERIFY(!validateCaptureOptions(o).isEmpty());
    }
    void encodeKeepsOutputAsSingleArgument() {
        CaptureOptions o; o.outputPath="/tmp/Видео с пробелами;$(echo x).mp4";
        o.width=1279; o.height=719;
        const auto args=encoderOutputArguments(o,o.outputPath);
        QVERIFY(!args.isEmpty());
        QCOMPARE(args.last(),o.outputPath);
        QVERIFY(args.contains("yuv420p"));
        QVERIFY(args.join(' ').contains("1278:718"));
    }
    void silentRecordingDoesNotAddAudio() {
        CaptureOptions o; o.outputPath="/tmp/a.webm";o.format="webm";
        const auto args=encoderOutputArguments(o,o.outputPath);
        QVERIFY(args.contains("libvpx-vp9"));
        QVERIFY(args.contains("-an"));
    }
};
QTEST_MAIN(RecordingPlanTest)
#include "recording_plan_test.moc"
