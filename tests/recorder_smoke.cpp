#include <QApplication>
#include <QTimer>
#include <QDebug>
#include "Recorder.h"
int main(int argc,char**argv){QApplication app(argc,argv);if(argc<2)return 2;Recorder r;CaptureOptions o;o.screen=app.primaryScreen();o.outputPath=QString::fromLocal8Bit(argv[1]);o.audioMode=argc>2?QString::fromLatin1(argv[2]):"none";o.width=1280;o.height=720;
 QObject::connect(&r,&Recorder::error,&app,[&](const QString&e){qWarning().noquote()<<e;app.exit(1);});
 QObject::connect(&r,&Recorder::completed,&app,[&](const QString&p){qInfo()<<"COMPLETE"<<p;app.exit(0);});
 QObject::connect(&r,&Recorder::stateChanged,&app,[&](Recorder::State s){qInfo()<<"STATE"<<s;if(s==Recorder::Recording){static int runs=0;if(++runs==1)QTimer::singleShot(1500,&r,&Recorder::pause);else QTimer::singleShot(1500,&r,&Recorder::stop);}if(s==Recorder::Paused)QTimer::singleShot(1000,&r,&Recorder::resume);});
 QTimer::singleShot(0,&app,[&]{if(!r.start(o))app.exit(1);});QTimer::singleShot(45000,&app,[&]{qWarning()<<"TIMEOUT";app.exit(3);});return app.exec();}
