#include "timelineedit.h"
#include "globalobjects.h"
#include <QTreeView>
#include <QLabel>
#include <QSplitter>
#include <QAction>
#include <QHeaderView>
#include <QVBoxLayout>
namespace
{
    QString formatTime(int mSec)
    {
        int cs=(mSec>=0?mSec:-mSec)/1000;
        int cmin=cs/60;
        int cls=cs-cmin*60;
        return QString("%0%1:%2").arg(mSec<0?"-":"").arg(cmin,2,10,QChar('0')).arg(cls,2,10,QChar('0'));
    }
}
TimelineEdit::TimelineEdit(DanmuSourceInfo *source, QWidget *parent):currentSource(source),
    CFramelessDialog(tr("Timeline Edit"),parent,true)
{
    timelineModel=new TimeLineInfoModel(source,this);
    SimpleDanumPool *simpleDanmuPool=new SimpleDanumPool(GlobalObjects::danmuPool->getSimpleDanmuInfo(source->id),this);
    simpleDanmuPool->refreshTimeline(timelineModel->getTimeLine());
    TimeLineBar *timelineBar=new TimeLineBar(simpleDanmuPool->getDanmuList(),timelineModel,this);
    timelineBar->setSizePolicy(QSizePolicy::MinimumExpanding,QSizePolicy::MinimumExpanding);

    QTreeView *timelineView=new QTreeView(this);
    timelineView->setRootIsDecorated(false);
    timelineView->setSelectionMode(QAbstractItemView::SingleSelection);
    timelineView->setFont(font());
    timelineView->setAlternatingRowColors(true);
    timelineView->setModel(timelineModel);
    timelineView->setContextMenuPolicy(Qt::ActionsContextMenu);
    timelineView->setSizePolicy(QSizePolicy::MinimumExpanding,QSizePolicy::MinimumExpanding);

    QTreeView *simpleDPView=new QTreeView(this);
    simpleDPView->setRootIsDecorated(false);
    simpleDPView->setFont(font());
    simpleDPView->setAlternatingRowColors(true);
    simpleDPView->setModel(simpleDanmuPool);
    simpleDPView->setSizePolicy(QSizePolicy::MinimumExpanding,QSizePolicy::MinimumExpanding);
    simpleDPView->header()->setStretchLastSection(false);

    QLabel *tipLabel=new QLabel(tr("Double Click: Begin/End Insert Space  Right Click: Cancel"),this);
    tipLabel->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Minimum);
    QLabel *timeTipLabel=new QLabel(this);
    timeTipLabel->setObjectName(QStringLiteral("TimeInfoTip"));
    timeTipLabel->hide();

    QSplitter *viewSplitter=new QSplitter(this);
    viewSplitter->setObjectName(QStringLiteral("NormalSplitter"));
    viewSplitter->setSizePolicy(QSizePolicy::MinimumExpanding,QSizePolicy::MinimumExpanding);
    viewSplitter->addWidget(timelineView);
    viewSplitter->addWidget(simpleDPView);
    viewSplitter->setStretchFactor(0,1);
    viewSplitter->setStretchFactor(1,1);
    viewSplitter->setCollapsible(0,false);
    viewSplitter->setCollapsible(1,false);

    QObject::connect(timelineBar,&TimeLineBar::mouseMove,[timeTipLabel,timelineBar,this](int x,int time,bool isStart){

        if(isStart)
        {
            tmpStartTime=time;
            timeTipLabel->setText(formatTime(time));
        }
        else
        {
            timeTipLabel->setText(tr("End: %1, Duration: %2").arg(formatTime(time)).arg(formatTime(time-tmpStartTime)));
        }
        timeTipLabel->adjustSize();
        timeTipLabel->move(x-timeTipLabel->width()/3,timelineBar->y()-timeTipLabel->height()-2*logicalDpiY()/96);
        timeTipLabel->show();
        timeTipLabel->raise();
    });
    QObject::connect(timelineBar,&TimeLineBar::mouseLeave,[timeTipLabel](){
        timeTipLabel->hide();
    });
    QObject::connect(timelineBar,&TimeLineBar::mousePress,[simpleDanmuPool,simpleDPView](int time){
        simpleDPView->scrollTo(simpleDanmuPool->getIndex(time),QAbstractItemView::PositionAtCenter);
    });
    QObject::connect(timelineBar,&TimeLineBar::addSpace,[this,simpleDanmuPool,timelineBar](int start,int duration){
        timelineModel->addSpace(start,duration);
        simpleDanmuPool->refreshTimeline(timelineModel->getTimeLine());
        timelineBar->updateInfo();
    });
    QAction *deleteAction=new QAction(tr("Delete"),this);
    timelineView->addAction(deleteAction);
    QObject::connect(deleteAction,&QAction::triggered,[timelineView,simpleDanmuPool,timelineBar,this](){
        QItemSelection selection=timelineView->selectionModel()->selection();
        if(selection.size()==0)return;
        timelineModel->removeSpace(selection.indexes().first());
        simpleDanmuPool->refreshTimeline(timelineModel->getTimeLine());
        timelineBar->updateInfo();
    });

    QVBoxLayout *dialogVLayout=new QVBoxLayout(this);
    //dialogVLayout->addSpacing(10*logicalDpiY()/96);
    dialogVLayout->addWidget(timelineBar);
    dialogVLayout->addWidget(viewSplitter);
    dialogVLayout->addWidget(tipLabel);
    resize(800*logicalDpiX()/96,420*logicalDpiY()/96);
}

void TimelineEdit::onAccept()
{
    currentSource->timelineInfo=timelineModel->getTimeLine();
    CFramelessDialog::onAccept();
}

TimeLineBar::TimeLineBar(const QList<SimpleDanmuInfo> *sDanmuList, TimeLineInfoModel *timelineModel, QWidget *parent):QWidget(parent),simpleDanmuList(sDanmuList)
{
    setMouseTracking(true);
    currentState=-1;
    this->timelineModel=timelineModel;
    refreshStatisInfo();
    updateInfo();
    setMinimumSize(200*logicalDpiX()/96,60*logicalDpiY()/96);
    setFocusPolicy(Qt::StrongFocus);
}

void TimeLineBar::updateInfo()
{
    if(simpleDanmuList->count()==0)
        duration=24*60;
    else
        duration=simpleDanmuList->last().originTime/1000+10;
    update();
}

void TimeLineBar::refreshStatisInfo()
{
    statisInfo.countOfMinute.clear();
    statisInfo.maxCountOfMinute=0;
    int curMinuteCount=0;
    int startTime=0;
    for(auto iter=simpleDanmuList->cbegin();iter!=simpleDanmuList->cend();++iter)
    {
        if(iter==simpleDanmuList->cbegin())
        {
            startTime=(*iter).originTime;
        }
        if((*iter).originTime-startTime<1000)
            curMinuteCount++;
        else
        {
            statisInfo.countOfMinute.append(QPair<int,int>(startTime/1000,curMinuteCount));
            if(curMinuteCount>statisInfo.maxCountOfMinute)
                statisInfo.maxCountOfMinute=curMinuteCount;
            curMinuteCount=1;
            startTime=(*iter).originTime;
        }
    }
    statisInfo.countOfMinute.append(QPair<int, int>(startTime / 1000, curMinuteCount));
    if (curMinuteCount>statisInfo.maxCountOfMinute)
        statisInfo.maxCountOfMinute = curMinuteCount;
}

void TimeLineBar::paintEvent(QPaintEvent *event)
{
    QRect bRect(event->rect());
    QPainter painter(this);
    painter.fillRect(bRect,QColor(0,0,0,150));
    if(duration==0)return;
    bRect.adjust(1, 0, -1, 0);
    float hRatio=(float)bRect.height()/statisInfo.maxCountOfMinute;
    //float margin=8*logicalDpiX()/96;
    float wRatio=(float)(bRect.width())/duration;
    float bHeight=bRect.height();

    QColor barColor(51,168,255,200);
    for(auto iter=statisInfo.countOfMinute.cbegin();iter!=statisInfo.countOfMinute.cend();++iter)
    {
        float l((*iter).first*wRatio);
        float h(floor((*iter).second*hRatio));
        painter.fillRect(l,bHeight-h,wRatio<1.f?1.f:wRatio,h,barColor);
    }

    QColor pSpaceColor(255,255,255,200);
    auto &timelineInfo=timelineModel->getTimeLine();
    for(auto &spaceItem:timelineInfo)
    {
        float l(spaceItem.first/1000*wRatio);
        painter.fillRect(l,0,1,bHeight,pSpaceColor);
    }

    if(currentState!=-1)
    {
        QColor lineColor(255,255,0);
        painter.fillRect(mouseTimeStartPos/1000*wRatio,0,1,bHeight,lineColor);
        if(currentState==1)
        {
            painter.fillRect(mouseTimeEndPos/1000*wRatio,0,1,bHeight,lineColor);
        }
    }
    painter.setPen(QColor(255,255,255));
    painter.drawText(bRect,Qt::AlignLeft|Qt::AlignTop,tr("Total:%1 Max:%2").arg(QString::number(simpleDanmuList->count())).arg(statisInfo.maxCountOfMinute));
}

void TimeLineBar::mousePressEvent(QMouseEvent *event)
{
    emit mousePress(currentState==0?mouseTimeStartPos:mouseTimeEndPos);
    if(event->button()==Qt::RightButton)
    {
        currentState=0;
        update();
    }
}

void TimeLineBar::mouseMoveEvent(QMouseEvent *event)
{
    if(currentState==-1)return;
    xPos=qBound(0,event->x(),width());
    if(currentState==0)
    {
        mouseTimeStartPos=(float)xPos/(float)width()*duration*1000;
        emit mouseMove(xPos,mouseTimeStartPos,true);
    }
    else
    {
        mouseTimeEndPos=(float)xPos/(float)width()*duration*1000;
        emit mouseMove(xPos,mouseTimeEndPos,false);
    }
    update();
}

void TimeLineBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    if(event->button()!=Qt::LeftButton || currentState==-1)return;
    if(currentState==0)
    {
        currentState=1;
    }
    else
    {
        currentState=0;
        emit addSpace(mouseTimeStartPos,mouseTimeEndPos-mouseTimeStartPos);
    }
    update();
}

void TimeLineBar::enterEvent(QEvent *)
{
    if(currentState==-1)currentState=0;
}

void TimeLineBar::leaveEvent(QEvent *)
{
    emit mouseLeave();
    if(currentState==0)currentState=-1;
    update();
}

void TimeLineBar::keyPressEvent(QKeyEvent *event)
{
    if(currentState==-1)return;
    int key = event->key();
    switch (key)
    {
    case Qt::Key_Right:
        xPos++;
        break;
    case Qt::Key_Left:
        xPos--;
        break;
    default:
        QWidget::keyPressEvent(event);
        return;
    }
    xPos=qBound(0,xPos,width());
    if(currentState==0)
    {
        mouseTimeStartPos=(float)xPos/(float)width()*duration*1000;
        emit mouseMove(xPos,mouseTimeStartPos,true);
    }
    else
    {
        mouseTimeEndPos=(float)xPos/(float)width()*duration*1000;
        emit mouseMove(xPos,mouseTimeEndPos,false);
    }
    update();
}

TimeLineInfoModel::TimeLineInfoModel(DanmuSourceInfo *source, QObject *parent):QAbstractItemModel(parent)
{
    timelineInfo=source->timelineInfo;
}

void TimeLineInfoModel::addSpace(int start, int duration)
{
    int i=0;
    while(i<timelineInfo.count() && timelineInfo.at(i).first<start) i++;
    if(i<timelineInfo.count() && timelineInfo.at(i).first==start)return;
    beginInsertRows(QModelIndex(),i,i);
    timelineInfo.insert(i,QPair<int,int>(start,duration));
    endInsertRows();
}

void TimeLineInfoModel::removeSpace(const QModelIndex &index)
{
    if(!index.isValid())return;
    beginRemoveRows(QModelIndex(),index.row(),index.row());
    timelineInfo.removeAt(index.row());
    endRemoveRows();
}

QVariant TimeLineInfoModel::data(const QModelIndex &index, int role) const
{
    if(!index.isValid()) return QVariant();
    auto &space=timelineInfo.at(index.row());
    int col=index.column();
    if(role==Qt::DisplayRole && col<3)
    {
        return formatTime((col==2?0:space.first)+(col==0?0:space.second));
    }
    return QVariant();
}

QVariant TimeLineInfoModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    static QString headers[]={tr("Start"),tr("End"),tr("Duration")};
    if (role == Qt::DisplayRole&&orientation == Qt::Horizontal)
    {
        if(section<3)return headers[section];
    }
    return QVariant();
}

SimpleDanumPool::SimpleDanumPool(QList<SimpleDanmuInfo> sDanmuList, QObject *parent):QAbstractItemModel(parent),simpleDanmuList(sDanmuList)
{
    std::sort(simpleDanmuList.begin(),simpleDanmuList.end(),
                 [](const SimpleDanmuInfo &danmu1,const SimpleDanmuInfo &danmu2){return danmu1.originTime<danmu2.originTime;});
}

QModelIndex SimpleDanumPool::getIndex(int time)
{
    int pos=std::lower_bound(simpleDanmuList.begin(),simpleDanmuList.end(),time,
                             [](const SimpleDanmuInfo &danmu,int time){return danmu.originTime<time;})-simpleDanmuList.begin();
    return createIndex(pos,0);
}

void SimpleDanumPool::refreshTimeline(const QList<QPair<int, int> > &timelineInfo)
{
    beginResetModel();
    int timelinePos=0,currentDelay=0;
    for(auto iter=simpleDanmuList.begin();iter!=simpleDanmuList.end();++iter)
    {
        SimpleDanmuInfo &sdi=*iter;
        while(timelinePos<timelineInfo.count())
        {
            if(timelineInfo.at(timelinePos).first<sdi.originTime)
            {
                currentDelay+=timelineInfo.at(timelinePos).second;
                timelinePos++;
            }
            else
            {
                break;
            }
        }
        sdi.time=sdi.originTime+currentDelay<0?sdi.originTime:sdi.originTime+currentDelay;
    }
    //std::sort(simpleDanmuList.begin(),simpleDanmuList.end(),
    //          [](const SimpleDanmuInfo &danmu1,const SimpleDanmuInfo &danmu2){return danmu1.ori<danmu2.time;});
    endResetModel();
}

QVariant SimpleDanumPool::data(const QModelIndex &index, int role) const
{
    if(!index.isValid()) return QVariant();
    auto &danmu=simpleDanmuList.at(index.row());
    int col=index.column();
    switch (role)
    {
    case Qt::DisplayRole:
    {
        if(col==0)
        {
            return formatTime(danmu.originTime);
        }
        else if(col==1)
        {
            return formatTime(danmu.time);
        }
        else if(col==2)
        {
            return danmu.text;
        }
    }
    default:
        return QVariant();
    }
    return QVariant();
}

QVariant SimpleDanumPool::headerData(int section, Qt::Orientation orientation, int role) const
{
    static QString headers[]={tr("Original"),tr("Adjusted"),tr("Content")};
    if (role == Qt::DisplayRole&&orientation == Qt::Horizontal)
    {
        if(section<3)return headers[section];
    }
    return QVariant();
}
