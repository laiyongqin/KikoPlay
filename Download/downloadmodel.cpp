#include "downloadmodel.h"
#include "globalobjects.h"
#include "aria2jsonrpc.h"
#include <QSqlQuery>
#include <QSqlRecord>
DownloadWorker *DownloadModel::downloadWorker=nullptr;
DownloadModel::DownloadModel(QObject *parent) : QAbstractItemModel(parent),currentOffset(0),
    hasMoreTasks(true),rpc(nullptr)
{
    downloadWorker=new DownloadWorker();
    downloadWorker->moveToThread(GlobalObjects::workThread);
}

DownloadModel::~DownloadModel()
{
    saveItemStatus();
    rpc->switchAllPauseStatus(true);
    qDeleteAll(downloadTasks);
}

void DownloadModel::setRPC(Aria2JsonRPC *aria2RPC)
{
    rpc=aria2RPC;
    //QObject::connect(rpc,&Aria2JsonRPC::refreshStatus,this,&DownloadModel::updateItemStatus);
}

void DownloadModel::addTask(DownloadTask *task)
{
    beginInsertRows(QModelIndex(),0,0);
    downloadTasks.prepend(task);
    endInsertRows();
    currentOffset++;
    //tasksMap.insert(task->taskID,task);
    if(!task->gid.isEmpty())
    {
        gidMap.insert(task->gid,task);
    }
    QMetaObject::invokeMethod(downloadWorker,[task](){
        downloadWorker->addTask(task);
    },Qt::QueuedConnection);
}

QString DownloadModel::addUriTask(const QString &uri, const QString &dir)
{
    QString taskID(QCryptographicHash::hash(uri.toUtf8(),QCryptographicHash::Sha1).toHex());
    if(containTask(taskID))
        return QString(tr("The task already exists: \n%1").arg(uri));
    QJsonObject options;
    options.insert("dir", dir);
    options.insert("bt-metadata-only","true");
    options.insert("bt-save-metadata","true");
	options.insert("seed-time", QString::number(GlobalObjects::appSetting->value("Download/SeedTime", 5).toInt()));
	options.insert("bt-tracker", GlobalObjects::appSetting->value("Download/Trackers", QStringList()).toStringList().join(','));

    try
    {
       QString gid=rpc->addUri(uri,options);
       DownloadTask *newTask=new DownloadTask();
       newTask->gid=gid;
       newTask->uri=uri;
       newTask->createTime=QDateTime::currentSecsSinceEpoch();
       newTask->taskID = taskID;
       newTask->dir=dir;
       newTask->title=uri;
       addTask(newTask);
    }
    catch(RPCError &error)
    {
       return error.errorInfo;
    }
    return QString();
}

QString DownloadModel::addTorrentTask(const QByteArray &torrentContent, const QString &infoHash,
                                      const QString &dir, const QString &selIndexes, const QString &magnet)
{
    if(containTask(infoHash))
        return QString(tr("The task already exists: \n%1").arg(infoHash));
    QJsonObject options;
    options.insert("dir", dir);
    options.insert("select-file",selIndexes);
    options.insert("seed-time",QString::number(GlobalObjects::appSetting->value("Download/SeedTime",5).toInt()));
    options.insert("bt-tracker",GlobalObjects::appSetting->value("Download/Trackers",QStringList()).toStringList().join(','));
    try
    {
        QString gid=rpc->addTorrent(torrentContent.toBase64(),options);
        DownloadTask *btTask=new DownloadTask();
        btTask->gid=gid;
        btTask->createTime=QDateTime::currentSecsSinceEpoch();
        btTask->taskID =infoHash;
        btTask->dir=dir;
        btTask->title=infoHash;
        btTask->torrentContent=torrentContent;
        btTask->selectedIndexes=selIndexes;
        btTask->uri=magnet;
        addTask(btTask);
    }
    catch(RPCError &error)
    {
        return error.errorInfo;
    }
    return QString();
}

void DownloadModel::removeItem(DownloadTask *task, bool deleteFile)
{
    int row=downloadTasks.indexOf(task);
    if(row!=-1)
    {
        beginRemoveRows(QModelIndex(),row,row);
        downloadTasks.removeAt(row);
        endRemoveRows();
        currentOffset--;
    }
    QMetaObject::invokeMethod(downloadWorker,[task,this,deleteFile](){
        downloadWorker->deleteTask(task,deleteFile);
        //if(last)emit animeCountChanged();
    },Qt::QueuedConnection);
}

bool DownloadModel::containTask(const QString &taskId)
{
    QSqlQuery query(QSqlDatabase::database("MT"));
    query.prepare("select * from download where TaskID=?");
    query.bindValue(0,taskId);
    query.exec();
    if(query.first()) return true;
    return false;
//    QEventLoop eventLoop;
//    bool contains=false;
//    QMetaObject::invokeMethod(downloadWorker,[&contains,&eventLoop,&taskId](){
//        contains=downloadWorker->containTask(taskId);
//        eventLoop.quit();
//    });
//    eventLoop.exec();
//    return contains;
}

void DownloadModel::removeItem(QModelIndexList &removeIndexes, bool deleteFile)
{
    std::sort(removeIndexes.begin(),removeIndexes.end(),[](const QModelIndex &index1,const QModelIndex &index2){
        return index1.row()>index2.row();
    });
    foreach (const QModelIndex &index, removeIndexes)
    {
        if(!index.isValid())return;
        DownloadTask *task=downloadTasks.at(index.row());
        beginRemoveRows(QModelIndex(), index.row(), index.row());
        downloadTasks.removeAt(index.row());
        endRemoveRows();
        if(!task->gid.isEmpty())
        {
            gidMap.remove(task->gid);
            rpc->removeTask(task->gid);
        }
        //tasksMap.remove(task->taskID);
        bool last=(index==removeIndexes.last());
        currentOffset--;
        QMetaObject::invokeMethod(downloadWorker,[task,this,last,deleteFile](){
            downloadWorker->deleteTask(task,deleteFile);
            //if(last)emit animeCountChanged();
        },Qt::QueuedConnection);
    }
}

void DownloadModel::updateItemStatus(const QJsonObject &statusObj)
{
    QString gid(statusObj.value("gid").toString());
    DownloadTask *item=gidMap.value(gid,nullptr);
    if(!item)return;
    item->totalLength=statusObj.value("totalLength").toString().toLongLong();
    item->completedLength=statusObj.value("completedLength").toString().toLongLong();
    item->downloadSpeed=statusObj.value("downloadSpeed").toString().toInt();
    item->uploadSpeed=statusObj.value("uploadSpeed").toString().toInt();
    if(statusObj.contains("bittorrent"))
    {
        QJsonObject btObj(statusObj.value("bittorrent").toObject());
        QString newTitle(btObj.value("info").toObject().value("name").toString());
        if(newTitle!=item->title && !newTitle.isEmpty())
        {
            item->title=newTitle;
            QMetaObject::invokeMethod(downloadWorker,[item](){
                downloadWorker->updateTaskInfo(item);
            },Qt::QueuedConnection);
        }
    }
    else if(statusObj.contains("files"))
    {
        QJsonArray fileArray(statusObj.value("files").toArray());
        QString path(fileArray.first().toObject().value("path").toString());
        QFileInfo info(path);
        if(item->title!=info.fileName() && !info.fileName().isEmpty())
        {
            item->title=info.fileName();
            QMetaObject::invokeMethod(downloadWorker,[item](){
                downloadWorker->updateTaskInfo(item);
            },Qt::QueuedConnection);
        }
    }
    QString status(statusObj.value("status").toString());
    DownloadTask::Status newStatus;
    if(status=="active")
    {
        if(item->totalLength>0)
            newStatus=(item->totalLength==item->completedLength?DownloadTask::Seeding:DownloadTask::Downloading);
        else
            newStatus=DownloadTask::Downloading;
    }
    else if(status=="waiting")
    {
        newStatus=DownloadTask::Waiting;
    }
    else if(status=="paused")
    {
        newStatus=DownloadTask::Paused;
    }
    else if(status=="error")
    {
        newStatus=DownloadTask::Error;
    }
    else if(status=="complete")
    {
        newStatus=DownloadTask::Complete;
    }
    if(newStatus!=item->status)
    {
        switch (newStatus)
        {
        case DownloadTask::Complete:
        {
            if(!statusObj.value("infoHash").isUndefined() && item->torrentContent.isEmpty())
            {
                QFileInfo info(item->dir,statusObj.value("infoHash").toString()+".torrent");
                gidMap.remove(gid);
                emit removeTask(gid);
                emit magnetDone(info.absoluteFilePath(),item->uri);
                removeItem(item,true);
            }
            else
            {
                QMetaObject::invokeMethod(downloadWorker, [item]() {
                    downloadWorker->updateTaskInfo(item);
                }, Qt::QueuedConnection);
            }
        }
        case DownloadTask::Seeding:
        {
            item->finishTime=QDateTime::currentSecsSinceEpoch();
            break;
        }
        default:
            break;
        }
        item->status=newStatus;
    }
    QModelIndex itemIndex(createIndex(downloadTasks.indexOf(item),0));
    emit dataChanged(itemIndex,itemIndex.sibling(itemIndex.row(),headers.count()-1));
}

DownloadTask *DownloadModel::getDownloadTask(const QModelIndex &index)
{
    if(!index.isValid())return nullptr;
    return downloadTasks.at(index.row());
}

QString DownloadModel::restartDownloadTask(DownloadTask *task, bool allowOverwrite)
{
    QJsonObject options;
    options.insert("dir", task->dir);
    if(task->torrentContent.isEmpty())
    {
        options.insert("bt-metadata-only","true");
        options.insert("bt-save-metadata","true");
    }
    else
    {
        options.insert("select-file",task->selectedIndexes);
        options.insert("seed-time",QString::number(GlobalObjects::appSetting->value("Download/SeedTime",5).toInt()));
        options.insert("bt-tracker",GlobalObjects::appSetting->value("Download/Trackers",QStringList()).toStringList().join(','));
    }
    if(allowOverwrite)
    {
        options.insert("allow-overwrite","true");
    }
    try
    {
       QString gid=task->torrentContent.isEmpty()?rpc->addUri(task->uri,options):
                                                  rpc->addTorrent(task->torrentContent.toBase64(),options);
       task->gid=gid;
       gidMap.insert(gid,task);
    }
    catch(RPCError &error)
    {
       return error.errorInfo;
    }
    return QString();
}

void DownloadModel::queryItemStatus()
{
    for(auto iter=gidMap.cbegin();iter!=gidMap.cend();iter++)
    {
        rpc->tellStatus(iter.key());
    }
}

void DownloadModel::saveItemStatus()
{
    for(auto iter=gidMap.begin();iter!=gidMap.end();iter++)
    {
        DownloadTask *task=iter.value();
        if(task->status!=DownloadTask::Complete)
        {
            QSqlQuery query(QSqlDatabase::database("MT"));
            query.prepare("update download set Title=?,FTime=?,TLength=?,CLength=?,SFIndexes=? where TaskID=?");
            query.bindValue(0,task->title);
            query.bindValue(1,task->finishTime);
            query.bindValue(2,task->totalLength);
            query.bindValue(3,task->completedLength);
            query.bindValue(4,task->selectedIndexes);
            query.bindValue(5,task->taskID);
            query.exec();
        }
    }
}

void DownloadModel::saveItemStatus(const DownloadTask *task)
{
    QMetaObject::invokeMethod(downloadWorker,[task](){
        downloadWorker->updateTaskInfo(task);
    },Qt::QueuedConnection);
}

QVariant DownloadModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid()) return QVariant();
    const DownloadTask *downloadItem=downloadTasks.at(index.row());
    int col=index.column();
    switch (col)
    {
    case 0:
        if(role==Qt::DisplayRole)
            return status.at(downloadItem->status);
        else if(role==Qt::DecorationRole)
            return statusIcons[downloadItem->status];
        break;
    case 1:
        if(role==Qt::DisplayRole || role==Qt::ToolTipRole)
            return downloadItem->title;
        break;
    case 3:
        if(role==Qt::DisplayRole)
            return formatSize(false,downloadItem->totalLength);
        break;
    case 4:
        if(role==Qt::DisplayRole)
            return formatSize(true,downloadItem->downloadSpeed);
        break;
    case 5:
        if(role==Qt::DisplayRole)
            return formatSize(true,downloadItem->uploadSpeed);
        break;
    default:
        break;
    }
    switch (role)
    {
    case DataRole::TotalLengthRole:
        return downloadItem->totalLength;
    case DataRole::CompletedLengthRole:
        return downloadItem->completedLength;
    }
    return QVariant();
}

QVariant DownloadModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role == Qt::DisplayRole&&orientation == Qt::Horizontal)
    {
        if(section<headers.count())return headers.at(section);
    }
    return QVariant();
}

void DownloadModel::fetchMore(const QModelIndex &)
{
    QList<DownloadTask *> moreTasks;
    QMetaObject::invokeMethod(downloadWorker,[this,&moreTasks](){
        downloadWorker->loadTasks(&moreTasks,currentOffset,limitCount);
    },Qt::QueuedConnection);
    QEventLoop eventLoop;
    QObject::connect(downloadWorker,&DownloadWorker::loadDone, &eventLoop,&QEventLoop::quit);
    eventLoop.exec();
    if(moreTasks.count()==0)
    {
        hasMoreTasks=false;
    }
    else
    {
        beginInsertRows(QModelIndex(),downloadTasks.count(),downloadTasks.count()+moreTasks.count()-1);
        downloadTasks.append(moreTasks);
        endInsertRows();
        currentOffset+=moreTasks.count();
        //for(DownloadTask *task:moreTasks)
        //{
        //    tasksMap.insert(task->taskID,task);
        //}
    }
}

void DownloadWorker::loadTasks(QList<DownloadTask *> *items, int offset, int limit)
{
    QSqlQuery query(QSqlDatabase::database("WT"));
    query.exec(QString("select * from download order by CTime desc limit %1 offset %2").arg(limit).arg(offset));
    int idNo=query.record().indexOf("TaskID"),
        titleNo=query.record().indexOf("Title"),
        dirNo=query.record().indexOf("Dir"),
        cTimeNo=query.record().indexOf("CTime"),
        fTimeNo=query.record().indexOf("FTime"),
        tLengthNo=query.record().indexOf("TLength"),
        cLengthNo=query.record().indexOf("CLength"),
        uriNo=query.record().indexOf("URI"),
        sfIndexNo=query.record().indexOf("SFIndexes"),
        torrentNo=query.record().indexOf("Torrent");
    int count=0;
    while (query.next())
    {
        DownloadTask *task=new DownloadTask();
        task->taskID=query.value(idNo).toString();
        task->dir=query.value(dirNo).toString();
        task->title=query.value(titleNo).toString();
        task->createTime=query.value(cTimeNo).toLongLong();
        task->finishTime=query.value(fTimeNo).toLongLong();
        task->totalLength=query.value(tLengthNo).toLongLong();
        task->completedLength=query.value(cLengthNo).toLongLong();
        task->status=((task->totalLength==task->completedLength && task->totalLength>0)?DownloadTask::Complete:DownloadTask::Paused);
        task->uri=query.value(uriNo).toString();
        task->selectedIndexes=query.value(sfIndexNo).toString();
        task->torrentContent=query.value(torrentNo).toByteArray();
        items->append(task);
        count++;
    }
    emit loadDone(count);
}

void DownloadWorker::addTask(DownloadTask *task)
{
    QSqlQuery query(QSqlDatabase::database("WT"));
    query.prepare("insert into download(TaskID,Dir,CTime,URI,SFIndexes,Torrent) values(?,?,?,?,?,?)");
    query.bindValue(0,task->taskID);
    query.bindValue(1,task->dir);
    query.bindValue(2,task->createTime);
    query.bindValue(3,task->uri);
    query.bindValue(4,task->selectedIndexes);
    query.bindValue(5,task->torrentContent);
    query.exec();
}

void DownloadWorker::deleteTask(DownloadTask *task, bool deleteFile)
{
    QSqlQuery query(QSqlDatabase::database("WT"));
    query.prepare("delete from download where TaskID=?");
    query.bindValue(0,task->taskID);
    query.exec();
    if(deleteFile && !task->title.isEmpty())
    {
        QFileInfo fi(task->dir,task->title);
        if(fi.exists())
        {
            if(fi.isDir())
            {
                QDir dir(fi.absoluteFilePath());
                dir.removeRecursively();
            }
            else
            {
                fi.dir().remove(fi.fileName());
            }
        }
        fi.setFile(task->dir,task->title+".aria2");
        if(fi.exists())
        {
            fi.dir().remove(fi.fileName());
        }
    }
    delete task;
}

void DownloadWorker::updateTaskInfo(const DownloadTask *task)
{
    QSqlQuery query(QSqlDatabase::database("WT"));
    query.prepare("update download set Title=?,FTime=?,TLength=?,CLength=?,SFIndexes=? where TaskID=?");
    query.bindValue(0,task->title);
    query.bindValue(1,task->finishTime);
    query.bindValue(2,task->totalLength);
    query.bindValue(3,task->completedLength);
    query.bindValue(4,task->selectedIndexes);
    query.bindValue(5,task->taskID);
    query.exec();
}

bool DownloadWorker::containTask(const QString &taskId)
{
    QSqlQuery query(QSqlDatabase::database("WT"));
    query.prepare("select * from download where TaskID=?");
    query.bindValue(0,taskId);
    query.exec();
    if(query.first()) return true;
    return false;
}

bool TaskFilterProxyModel::filterAcceptsRow(int source_row, const QModelIndex &source_parent) const
{
    QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
    DownloadTask *task=static_cast<DownloadModel *>(sourceModel())->getDownloadTask(index);
    if(!task->title.contains(filterRegExp()))return false;
    switch (taskStatus)
    {
    case 0: //all
        return true;
    case 1: //active
        return task->status==DownloadTask::Downloading || task->status==DownloadTask::Seeding ||
                task->status==DownloadTask::Waiting || task->status==DownloadTask::Paused;
    case 2: //completed
        return task->status==DownloadTask::Complete || task->status==DownloadTask::Error;
    default:
        return true;
    }
}