#include "UI/mainwindow.h"
#include <QApplication>
#include <Windows.h>
#include <DbgHelp.h>
#include <QMessageBox>
#include <QLocalSocket>
#include <QLocalServer>
#include "globalobjects.h"
#include "Play/Playlist/playlist.h"
#include "Play/Video/mpvplayer.h"
#include "Play/Danmu/danmupool.h"
#include "Play/Danmu/danmurender.h"
#pragma comment (lib,"DbgHelp.lib")
LONG AppCrashHandler(EXCEPTION_POINTERS *pException) 
{			
	HANDLE hDumpFile = CreateFile((LPCWSTR)(QCoreApplication::applicationDirPath() + QDateTime::currentDateTime().toString("\\yyyy-MM-dd-hh-mm-ss")+".dmp").utf16(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDumpFile != INVALID_HANDLE_VALUE) 
	{
		MINIDUMP_EXCEPTION_INFORMATION dumpInfo;
		dumpInfo.ExceptionPointers = pException;
		dumpInfo.ThreadId = GetCurrentThreadId();
		dumpInfo.ClientPointers = TRUE;
		MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, MiniDumpNormal, &dumpInfo, NULL, NULL);
	}
	EXCEPTION_RECORD* record = pException->ExceptionRecord;
	QString errCode(QString::number(record->ExceptionCode, 16)), errAdr(QString::number((uint)record->ExceptionAddress, 16)), errMod;
	QMessageBox::critical(nullptr, "Error", QString("Error Code: %1 Error Address: %2").arg(errCode).arg(errAdr),QMessageBox::Ok);
	return EXCEPTION_EXECUTE_HANDLER;
}
void decodeParam()
{
    QStringList args=QCoreApplication::arguments();
    if(args.count()<=1)return;
    args.pop_front();
    QStringList fileList;
    for(const QString &path:args)
    {
        QFileInfo fi(path);
        if(fi.isFile())
        {
            if(GlobalObjects::mpvplayer->videoFileFormats.contains("*."+fi.suffix().toLower()))
            {
                fileList.append(fi.filePath());
            }
        }
    }
    if(fileList.count()>0)
    {
        GlobalObjects::playlist->addItems(fileList,QModelIndex());
        const PlayListItem *curItem = GlobalObjects::playlist->setCurrentItem(fileList.last());
        if (curItem)
        {
            GlobalObjects::mpvplayer->setMedia(curItem->path);
        }
    }
}
bool isRunning()
{
    QLocalSocket socket;
    socket.connectToServer("KikoPlaySingleServer");
    if (socket.waitForConnected())
    {
        QStringList args=QCoreApplication::arguments();
        if(args.count()>1)
        {
            args.pop_front();
            QStringList fileList;
            for(const QString &path:args)
            {
                QFileInfo fi(path);
                if(fi.isFile())
                {
                    if(GlobalObjects::mpvplayer->videoFileFormats.contains("*."+fi.suffix().toLower()))
                    {
                        fileList.append(fi.filePath());
                    }
                }
            }
            if(fileList.count()>0)
            {
                QByteArray data;
                QDataStream s(&data,QIODevice::WriteOnly);
                s << fileList;
                socket.write(data);
                socket.waitForBytesWritten();
            }
        }
        return true;
    }
    return false;
}
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    SetUnhandledExceptionFilter((LPTOP_LEVEL_EXCEPTION_FILTER)AppCrashHandler);
    if(isRunning()) return 0;
    QString qss;
    QFile qssFile(":/res/style.qss");
    qssFile.open(QFile::ReadOnly);

    if(qssFile.isOpen())
    {
        qss = QLatin1String(qssFile.readAll());
        qApp->setStyleSheet(qss);
        qssFile.close();
    }
    QString trans(QString(":/res/lang/%1.qm").arg(QLocale::system().name()));
    QTranslator translator;
    if(translator.load(trans))
        a.installTranslator(&translator);

    GlobalObjects::init();
    MainWindow w;
    decodeParam();
    QLocalServer *singleServer=new QLocalServer(&a);
    QObject::connect(singleServer, &QLocalServer::newConnection, [singleServer,&w](){
        QLocalSocket *localSocket = singleServer->nextPendingConnection();
        localSocket->waitForReadyRead();
        QByteArray data(localSocket->readAll());
        QStringList args;
        QDataStream s(data);
        s >> args;
        if(args.count()>0)
        {
            GlobalObjects::playlist->addItems(args,QModelIndex());
            int playTime=GlobalObjects::mpvplayer->getTime();
            GlobalObjects::playlist->setCurrentPlayTime(playTime);
            const PlayListItem *curItem = GlobalObjects::playlist->setCurrentItem(args.last());
            if (curItem)
            {
                GlobalObjects::danmuPool->reset();
                GlobalObjects::danmuRender->cleanup();
                GlobalObjects::mpvplayer->setMedia(curItem->path);

            }
        }
        w.raise();
        w.activateWindow();
        w.setWindowState((w.windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        w.show();
    });
    if(!singleServer->listen("KikoPlaySingleServer"))
    {
        if(singleServer->serverError() == QAbstractSocket::AddressInUseError)
        {
            QLocalServer::removeServer("KikoPlaySingleServer");
            singleServer->listen("KikoPlaySingleServer");
        }
    }
    w.show();

    return a.exec();
}
