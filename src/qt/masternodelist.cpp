#include "masternodelist.h"
#include "ui_masternodelist.h"

#include "../activemasternode.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "../init.h"
#include "../masternode-sync.h"
#include "../masternodeconfig.h"
#include "../masternodeman.h"
#include "../sync.h"
#include "../wallet.h"
#include "walletmodel.h"
#include "../rpcserver.h"

#include <QMessageBox>
#include <QTimer>
#include <QTime>
#include <QDebug>

CCriticalSection cs_masternodes;

int GetOffsetFromUtc()
{
#if QT_VERSION < 0x050200
    const QDateTime dateTime1 = QDateTime::currentDateTime();
    const QDateTime dateTime2 = QDateTime(dateTime1.date(), dateTime1.time(), Qt::UTC);
    return dateTime1.secsTo(dateTime2);
#else
    return QDateTime::currentDateTime().offsetFromUtc();
#endif
}



MasternodeList::MasternodeList(QWidget* parent) : QWidget(parent),
                                                  ui(new Ui::MasternodeList),
                                                  clientModel(0),
                                                  walletModel(0)
{
    ui->setupUi(this);

    ui->startButton->setEnabled(false);

    int columnAliasWidth = 100;
    int columnAddressWidth = 180;
    int columnLevelWidth = 60;
    int columnProtocolWidth = 80;
    int columnStatusWidth = 80;
    int columnActiveWidth = 100;
    int columnLastSeenWidth = 100;
    int columnPayeeWidth = 270;
    int column24hCoinsWidth = 80;
    int columnLuckWidth = 60;

    ui->tableWidgetMyMasternodes->setColumnWidth(0, columnAliasWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(1, columnAddressWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(2, columnLevelWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(3, columnProtocolWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(4, columnStatusWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(5, columnActiveWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(6, columnLastSeenWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(7, columnPayeeWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(8, column24hCoinsWidth);
    ui->tableWidgetMyMasternodes->setColumnWidth(9, columnLuckWidth);

    ui->tableWidgetMasternodes->setColumnWidth(0, columnAddressWidth);
    ui->tableWidgetMasternodes->setColumnWidth(1, columnLevelWidth);
    ui->tableWidgetMasternodes->setColumnWidth(2, columnProtocolWidth);
    ui->tableWidgetMasternodes->setColumnWidth(3, columnStatusWidth);
    ui->tableWidgetMasternodes->setColumnWidth(4, columnActiveWidth);
    ui->tableWidgetMasternodes->setColumnWidth(5, columnLastSeenWidth);
    ui->tableWidgetMasternodes->setColumnWidth(6, columnPayeeWidth);
    ui->tableWidgetMasternodes->setColumnWidth(7, column24hCoinsWidth);
    ui->tableWidgetMasternodes->setColumnWidth(8, columnLuckWidth);

    ui->tableWidgetMyMasternodes->setContextMenuPolicy(Qt::CustomContextMenu);

    QAction* startAliasAction = new QAction(tr("Start alias"), this);
    contextMenu = new QMenu();
    contextMenu->addAction(startAliasAction);
    connect(ui->tableWidgetMyMasternodes, SIGNAL(customContextMenuRequested(const QPoint&)), this, SLOT(showContextMenu(const QPoint&)));
    connect(startAliasAction, SIGNAL(triggered()), this, SLOT(on_startButton_clicked()));

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateNodeList()));
    connect(timer, SIGNAL(timeout()), this, SLOT(updateMyNodeList()));
    timer->start(1000);

    // Fill MN list
    fFilterUpdated = true;
    nTimeFilterUpdated = GetTime();
    updateNodeList();
}

MasternodeList::~MasternodeList()
{
    delete ui;
}

void MasternodeList::setClientModel(ClientModel* model)
{
    this->clientModel = model;
    if(model) {
        // try to update list when masternode count changes
//        connect(clientModel, SIGNAL(strMasternodesChanged(QString)), this, SLOT(updateNodeList()));
        connect(model, SIGNAL(strMasternodesChanged(QString)), this, SLOT(updateNodeList()));
    }
}

void MasternodeList::setWalletModel(WalletModel* model)
{
    this->walletModel = model;
}

void MasternodeList::showContextMenu(const QPoint& point)
{
    QTableWidgetItem* item = ui->tableWidgetMyMasternodes->itemAt(point);
    if (item) contextMenu->exec(QCursor::pos());
}

bool RpcStartMasternode(std::string &Alias,std::string &Overall, std::string &Error, std::string &Result){
    UniValue Params(UniValue::VARR);
    UniValue resultObj(UniValue::VOBJ);
    Params.push_back("alias");
    Params.push_back("0");
    Params.push_back(Alias);
    try {
        resultObj = startmasternode(Params,false);
    } catch (const std::runtime_error& e) {
        Error = e.what();
        return false;
    }
    Overall = resultObj[0].get_str();
    Result =  resultObj[1][0][1].get_str();
    if(Result == "failed"){
        Error = resultObj[1][0][2].get_str();
        return false;
    }
    return true;
}

void MasternodeList::StartAlias(std::string strAlias)
{
    std::string strStatusHtml;
    strStatusHtml += "<center>Alias: " + strAlias;
    for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {
        if (mne.getAlias() == strAlias) {
            std::string strError;
            std::string strOverall;
            std::string strResult;
            bool fSuccess = RpcStartMasternode(strAlias,strOverall,strError,strResult);
            if(fSuccess){
                strStatusHtml += "<br>" +strOverall + "<br>";
            }
            else{
                strStatusHtml += "<br>Failed to start masternode.<br>Error: " + strError;
            }
            break;
        }
    }
    strStatusHtml += "</center>";

    QMessageBox msg;
    msg.setText(QString::fromStdString(strStatusHtml));
    msg.exec();

    updateMyNodeList(true);
}

void MasternodeList::StartAll(std::string strCommand)
{
    int nCountSuccessful = 0;
    int nCountFailed = 0;
    std::string strFailedHtml;

    for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {
        std::string strError;
        std::string strAlias = mne.getAlias();
        std::string strOverall;
        std::string strResult;

        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CMasternode* pmn = mnodeman.Find(txin);

        if (strCommand == "start-missing" && pmn) continue;

        bool fSuccess = RpcStartMasternode(strAlias,strOverall,strError,strResult);

        if (fSuccess) {
            nCountSuccessful++;
        } else {
            nCountFailed++;
            strFailedHtml += "\nFailed to start " + mne.getAlias() + ". Error: " + strError;
        }
    }
    pwalletMain->Lock();

    std::string returnObj;
    returnObj = strprintf("Successfully started %d masternodes, failed to start %d, total %d", nCountSuccessful, nCountFailed, nCountFailed + nCountSuccessful);
    if (nCountFailed > 0) {
        returnObj += strFailedHtml;
    }

    QMessageBox msg;
    msg.setText(QString::fromStdString(returnObj));
    msg.exec();

    updateMyNodeList(true);
}

void MasternodeList::updateMyMasternodeInfo(QString strAlias, QString strAddr, CMasternode* pmn)
{
    LOCK(cs_mnlistupdate);
    bool fOldRowFound = false;
    int nNewRow = 0;

    for (int i = 0; i < ui->tableWidgetMyMasternodes->rowCount(); i++) {
        if (ui->tableWidgetMyMasternodes->item(i, 0)->text() == strAlias) {
            fOldRowFound = true;
            nNewRow = i;
            break;
        }
    }

    if (nNewRow == 0 && !fOldRowFound) {
        nNewRow = ui->tableWidgetMyMasternodes->rowCount();
        ui->tableWidgetMyMasternodes->insertRow(nNewRow);
    }

    std::string mnLevelText = "";
    double tLuck = 0;
    CAmount masternodeCoins = 0;
    std::string pubkey = "";

    if (pmn) {
        pubkey = CBitcoinAddress(pmn->pubKeyCollateralAddress.GetID()).ToString();
        {
            LOCK(cs_stat);
            qDebug() << __FUNCTION__ << ": LOCK(cs_stat)";

            auto it = masternodeRewards.find(pubkey);
            if (it != masternodeRewards.end())
                masternodeCoins = (*it).second;
        }

        switch (pmn->Level())
        {
            case 1: mnLevelText = "Security node"; if (roi1 > 1) tLuck = ((masternodeCoins/COIN) / roi1)*100; break;
            case 2: mnLevelText = "Cash node"; if (roi2 > 1) tLuck = ((masternodeCoins/COIN) / roi2)*100; break;
            case 3: mnLevelText = "Reserve node"; if (roi3 > 1) tLuck = ((masternodeCoins/COIN) / roi3)*100; break;
        }
    }

    //    QTableWidgetItem* levelItem = new QTableWidgetItem(QString::number(pmn ? pmn->Level() : 0u));
    QTableWidgetItem* levelItem = new QTableWidgetItem(QString::fromStdString(mnLevelText));

    QTableWidgetItem* aliasItem = new QTableWidgetItem(strAlias);
    QTableWidgetItem* addrItem = new QTableWidgetItem(pmn ? QString::fromStdString(pmn->addr.ToString()) : strAddr);
    QTableWidgetItem* protocolItem = new QTableWidgetItem(QString::number(pmn ? pmn->protocolVersion : -1));
    //protocolItem->setTextAlignment(Qt::AlignHCenter);
    QTableWidgetItem* statusItem = new QTableWidgetItem(QString::fromStdString(pmn ? pmn->Status() : "MISSING"));
    int64_t activeSeconds = pmn ? pmn->lastPing.sigTime - pmn->sigTime : 0;
    GUIUtil::DHMSTableWidgetItem* activeSecondsItem = new GUIUtil::DHMSTableWidgetItem(pmn ? activeSeconds : 0);
    QTableWidgetItem* lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", pmn ? pmn->lastPing.sigTime + GetOffsetFromUtc() : 0)));
    QTableWidgetItem* pubkeyItem = new QTableWidgetItem(QString::fromStdString(pmn ? pubkey : ""));
    QTableWidgetItem* mnReward = new QTableWidgetItem(QString::number(masternodeCoins/COIN,'f',1));
    //mnReward->setTextAlignment(Qt::AlignRight);
    QTableWidgetItem* mnLuck = new QTableWidgetItem(activeSeconds > 30*60*60 ? QString::number(tLuck,'f',1).append("%") : QString::fromStdString("-"));
    //mnLuck->setTextAlignment(Qt::AlignHCenter);

    ui->tableWidgetMyMasternodes->setItem(nNewRow, 0, aliasItem);
    ui->tableWidgetMyMasternodes->setItem(nNewRow, 1, addrItem);
    ui->tableWidgetMyMasternodes->setItem(nNewRow, 2, levelItem);
    ui->tableWidgetMyMasternodes->setItem(nNewRow, 3, protocolItem);
    ui->tableWidgetMyMasternodes->setItem(nNewRow, 4, statusItem);
    ui->tableWidgetMyMasternodes->setItem(nNewRow, 5, activeSecondsItem);
    ui->tableWidgetMyMasternodes->setItem(nNewRow, 6, lastSeenItem);
    ui->tableWidgetMyMasternodes->setItem(nNewRow, 7, pubkeyItem);
    ui->tableWidgetMyMasternodes->setItem(nNewRow, 8, mnReward);
    ui->tableWidgetMyMasternodes->setItem(nNewRow, 9, mnLuck);

}

void MasternodeList::updateMyNodeList(bool fForce)
{
    static int64_t nTimeMyListUpdated = 0;

    // automatically update my masternode list only once in MY_MASTERNODELIST_UPDATE_SECONDS seconds,
    // this update still can be triggered manually at any time via button click
    int64_t nSecondsTillUpdate = nTimeMyListUpdated + MY_MASTERNODELIST_UPDATE_SECONDS - GetTime();
    ui->secondsLabel->setText(QString::number(nSecondsTillUpdate));

    if (nSecondsTillUpdate > 0 && !fForce) return;
    nTimeMyListUpdated = GetTime();

    ui->tableWidgetMyMasternodes->setSortingEnabled(false);
    for (CMasternodeConfig::CMasternodeEntry mne : masternodeConfig.getEntries()) {
        int nIndex;
        if(!mne.castOutputIndex(nIndex))
            continue;

        CTxIn txin = CTxIn(uint256S(mne.getTxHash()), uint32_t(nIndex));
        CMasternode* pmn = mnodeman.Find(txin);
        updateMyMasternodeInfo(QString::fromStdString(mne.getAlias()), QString::fromStdString(mne.getIp()), pmn);
    }
    ui->tableWidgetMyMasternodes->setSortingEnabled(true);

    // reset "timer"
    ui->secondsLabel->setText("0");
}

void MasternodeList::updateNodeList()
{
    static int64_t nTimeListUpdated = GetTime();

    // to prevent high cpu usage update only once in MASTERNODELIST_UPDATE_SECONDS seconds
    // or MASTERNODELIST_FILTER_COOLDOWN_SECONDS seconds after filter was last changed
    int64_t nSecondsToWait = fFilterUpdated
                            ? nTimeFilterUpdated - GetTime() + MASTERNODELIST_FILTER_COOLDOWN_SECONDS
                            : nTimeListUpdated - GetTime() + MASTERNODELIST_UPDATE_SECONDS;

    if(fFilterUpdated) ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", nSecondsToWait)));
    if(nSecondsToWait > 0) return;

    nTimeListUpdated = GetTime();
    fFilterUpdated = false;

    TRY_LOCK(cs_masternodes, lockMasternodes);
    if (!lockMasternodes) return;
    TRY_LOCK(cs_stat, lockStat);
    if (!lockStat) return;
    qDebug() << __FUNCTION__ << ": TRY_LOCK(cs_stat, lockStat)";

    QString strToFilter;
    ui->countLabel->setText("Updating...");
    ui->tableWidgetMasternodes->setSortingEnabled(false);
    ui->tableWidgetMasternodes->clearContents();
    ui->tableWidgetMasternodes->setRowCount(0);
    std::vector<CMasternode> vMasternodes = mnodeman.GetFullMasternodeMap();
    int offsetFromUtc = GetOffsetFromUtc();

    std::string mnLevelText = "";

    for(auto& mn : vMasternodes)
    {
        // populate list
        // Address, Protocol, Status, Active Seconds, Last Seen, Pub Key
        QTableWidgetItem *addressItem = new QTableWidgetItem(QString::fromStdString(mn.addr.ToString()));

        double tLuck = 0;
        CAmount masternodeCoins = 0;
        std::string pubkey = CBitcoinAddress(mn.pubKeyCollateralAddress.GetID()).ToString();
        auto it = masternodeRewards.find(pubkey);
        if (it != masternodeRewards.end())
            masternodeCoins = (*it).second;

        switch (mn.Level())
        {
            case 1: mnLevelText = "Security node"; if (roi1 > 1) tLuck = ((masternodeCoins/COIN) / roi1)*100; break;
            case 2: mnLevelText = "Cash node"; if (roi2 > 1) tLuck = ((masternodeCoins/COIN) / roi2)*100; break;
            case 3: mnLevelText = "Reserve node"; if (roi3 > 1) tLuck = ((masternodeCoins/COIN) / roi3)*100; break;
        }

        QTableWidgetItem *levelItem = new QTableWidgetItem(QString::fromStdString(mnLevelText));
//        QTableWidgetItem *levelItem = new QTableWidgetItem(QString::number(mn.Level()));

        QTableWidgetItem *protocolItem = new QTableWidgetItem(QString::number(mn.protocolVersion));
        //protocolItem->setTextAlignment(Qt::AlignHCenter);
        QTableWidgetItem *statusItem = new QTableWidgetItem(QString::fromStdString(mn.Status()));
        int64_t activeSeconds = mn.lastPing.sigTime - mn.sigTime;
        QTableWidgetItem *activeSecondsItem = new QTableWidgetItem(QString::fromStdString(DurationToDHMS(activeSeconds)));
        QTableWidgetItem *lastSeenItem = new QTableWidgetItem(QString::fromStdString(DateTimeStrFormat("%Y-%m-%d %H:%M", mn.lastPing.sigTime + offsetFromUtc)));
        QTableWidgetItem *pubkeyItem = new QTableWidgetItem(QString::fromStdString(pubkey));
        QTableWidgetItem *mnReward = new QTableWidgetItem(QString::number(masternodeCoins/COIN,'f',1));
        //mnReward->setTextAlignment(Qt::AlignRight);
        QTableWidgetItem *mnLuck = new QTableWidgetItem(activeSeconds > 30*60*60 ? QString::number(tLuck,'f',1).append("%") : QString::fromStdString("-"));
        //mnLuck->setTextAlignment(Qt::AlignHCenter);

        if (strCurrentFilter != "")
        {
            strToFilter =   addressItem->text() + " " +
                            protocolItem->text() + " " +
                            statusItem->text() + " " +
                            activeSecondsItem->text() + " " +
                            lastSeenItem->text() + " " +
                            pubkeyItem->text();
            if (!strToFilter.contains(strCurrentFilter)) continue;
        }

        ui->tableWidgetMasternodes->insertRow(0);
        ui->tableWidgetMasternodes->setItem(0, 0, addressItem);
        ui->tableWidgetMasternodes->setItem(0, 1, levelItem);
        ui->tableWidgetMasternodes->setItem(0, 2, protocolItem);
        ui->tableWidgetMasternodes->setItem(0, 3, statusItem);
        ui->tableWidgetMasternodes->setItem(0, 4, activeSecondsItem);
        ui->tableWidgetMasternodes->setItem(0, 5, lastSeenItem);
        ui->tableWidgetMasternodes->setItem(0, 6, pubkeyItem);
        ui->tableWidgetMasternodes->setItem(0, 7, mnReward);
        ui->tableWidgetMasternodes->setItem(0, 8, mnLuck);

    }

    ui->countLabel->setText(QString::number(ui->tableWidgetMasternodes->rowCount()));
    ui->tableWidgetMasternodes->setSortingEnabled(true);
}

void MasternodeList::on_filterLineEdit_textChanged(const QString &strFilterIn)
{
    strCurrentFilter = strFilterIn;
    nTimeFilterUpdated = GetTime();
    fFilterUpdated = true;
    ui->countLabel->setText(QString::fromStdString(strprintf("Please wait... %d", MASTERNODELIST_FILTER_COOLDOWN_SECONDS)));
}

void MasternodeList::on_startButton_clicked()
{
    // Find selected node alias
    QItemSelectionModel* selectionModel = ui->tableWidgetMyMasternodes->selectionModel();
    QModelIndexList selected = selectionModel->selectedRows();

    if (selected.count() == 0) return;

    QModelIndex index = selected.at(0);
    int nSelectedRow = index.row();
    std::string strAlias = ui->tableWidgetMyMasternodes->item(nSelectedRow, 0)->text().toStdString();

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm masternode start"),
        tr("Are you sure you want to start masternode %1?").arg(QString::fromStdString(strAlias)),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAlias(strAlias);
        return;
    }

    StartAlias(strAlias);
}

void MasternodeList::on_startAllButton_clicked()
{
    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this, tr("Confirm all masternodes start"),
        tr("Are you sure you want to start ALL masternodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll();
        return;
    }

    StartAll();
}

void MasternodeList::on_startMissingButton_clicked()
{
    if (!masternodeSync.IsMasternodeListSynced()) {
        QMessageBox::critical(this, tr("Command is not available right now"),
            tr("You can't use this command until masternode list is synced"));
        return;
    }

    // Display message box
    QMessageBox::StandardButton retval = QMessageBox::question(this,
        tr("Confirm missing masternodes start"),
        tr("Are you sure you want to start MISSING masternodes?"),
        QMessageBox::Yes | QMessageBox::Cancel,
        QMessageBox::Cancel);

    if (retval != QMessageBox::Yes) return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    if (encStatus == walletModel->Locked || encStatus == walletModel->UnlockedForAnonymizationOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock());

        if (!ctx.isValid()) return; // Unlock wallet was cancelled

        StartAll("start-missing");
        return;
    }

    StartAll("start-missing");
}

void MasternodeList::on_tableWidgetMyMasternodes_itemSelectionChanged()
{
    if (ui->tableWidgetMyMasternodes->selectedItems().count() > 0) {
        ui->startButton->setEnabled(true);
    }
}

void MasternodeList::on_UpdateButton_clicked()
{
    updateMyNodeList(true);
}
