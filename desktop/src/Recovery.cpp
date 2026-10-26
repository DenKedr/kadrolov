#include "Recovery.h"
#include "Recorder.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QFileDialog>
#include <QProcess>
#include <QProgressDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QPushButton>
#include <QUuid>
void Recovery::offer(QWidget *parent,const QString &folder){
 const auto dirs=QDir(folder).entryInfoList({".kadrolov-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot,QDir::Time);int seen=0;
 for(const auto &dir:dirs){if(++seen>100)break;QFile session(dir.filePath()+"/session.json");if(!session.open(QIODevice::ReadOnly))continue;const auto obj=QJsonDocument::fromJson(session.read(1024*1024)).object();
  QStringList pieces;for(const auto &v:obj["segments"].toArray()){const QString p=dir.filePath()+"/"+QFileInfo(v.toString()).fileName();if(QFileInfo(p).size()>32)pieces<<p;}
  if(pieces.isEmpty()){for(const auto &p:QDir(dir.filePath()).entryInfoList({"capture.mp4","capture.mkv","capture.webm","segment-*.mkv"},QDir::Files,QDir::Name))if(p.size()>32)pieces<<p.absoluteFilePath();}
  if(pieces.isEmpty())continue;
  QMessageBox question(QMessageBox::Question,QStringLiteral("Найдена незавершённая запись"),QStringLiteral("В папке видео остались записанные части. Можно создать восстановленную копию или открыть папку. Исходные части останутся на месте."),QMessageBox::NoButton,parent);
  auto *recover=question.addButton(QStringLiteral("Восстановить копию"),QMessageBox::AcceptRole);auto *open=question.addButton(QStringLiteral("Открыть папку"),QMessageBox::ActionRole);question.addButton(QStringLiteral("Позже"),QMessageBox::RejectRole);question.exec();
  if(question.clickedButton()==open){QDesktopServices::openUrl(QUrl::fromLocalFile(dir.filePath()));return;}if(question.clickedButton()!=recover)return;
  if(Recorder::ffmpegPath().isEmpty()){QMessageBox::information(parent,QStringLiteral("Восстановление"),QStringLiteral("Для восстановления незавершённых частей установите FFmpeg. Готовые части можно открыть в папке."));return;}
  const QString output=QFileDialog::getSaveFileName(parent,QStringLiteral("Сохранить восстановленную копию"),QDir(folder).filePath(QStringLiteral("Кадролов_восстановлено.mp4")),QStringLiteral("Видео MP4 (*.mp4)"));if(output.isEmpty())return;if(QFileInfo::exists(output)){QMessageBox::information(parent,QStringLiteral("Выберите другое имя"),QStringLiteral("Файл уже существует. Восстановление не перезаписывает файлы."));return;}
  QStringList args{"-hide_banner","-loglevel","error"};if(pieces.size()==1)args<<"-i"<<pieces.first();else{QFile list(dir.filePath()+"/recovery.txt");if(!list.open(QIODevice::WriteOnly))return;for(auto p:pieces){p.replace("'","'\\''");list.write(QString("file '%1'\n").arg(p).toUtf8());}list.close();args<<"-f"<<"concat"<<"-safe"<<"0"<<"-i"<<list.fileName();}
  const QString result=dir.filePath()+"/recovered-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".mp4";
  args<<"-map"<<"0:v:0"<<"-map"<<"0:a:0?"<<"-c:v"<<"libx264"<<"-preset"<<"veryfast"<<"-c:a"<<"aac"<<"-movflags"<<"+faststart"<<"-n"<<result;
  auto*p=new QProcess(parent);auto*progress=new QProgressDialog(QStringLiteral("Восстанавливаем записанные части…"),QStringLiteral("Отмена"),0,0,parent);progress->setAutoClose(false);progress->show();QObject::connect(progress,&QProgressDialog::canceled,p,&QProcess::kill);
  QObject::connect(p,QOverload<int,QProcess::ExitStatus>::of(&QProcess::finished),parent,[p,progress,parent,output,result](int code,QProcess::ExitStatus status){bool cancelled=progress->wasCanceled();progress->close();progress->deleteLater();if(code==0&&status==QProcess::NormalExit&&QFileInfo(result).size()>32){if(!QFileInfo::exists(output)&&QFile::rename(result,output))QMessageBox::information(parent,QStringLiteral("Копия восстановлена"),output);else QMessageBox::warning(parent,QStringLiteral("Готовая копия сохранена временно"),result);}else{QFile::remove(result);if(!cancelled)QMessageBox::warning(parent,QStringLiteral("Не удалось восстановить"),QStringLiteral("Исходные части сохранены. ")+QString::fromUtf8(p->readAllStandardError()).right(1000));}p->deleteLater();});
  QObject::connect(p,&QProcess::errorOccurred,parent,[p,progress,parent](QProcess::ProcessError e){if(e!=QProcess::FailedToStart)return;progress->close();QMessageBox::warning(parent,QStringLiteral("Восстановление не запустилось"),p->errorString());p->deleteLater();progress->deleteLater();});p->start(Recorder::ffmpegPath(),args);return;
 }
}
