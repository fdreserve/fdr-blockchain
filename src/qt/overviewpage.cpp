// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018-2019 The fdreserve Core developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "overviewpage.h"
#include "ui_overviewpage.h"

#include "bitcoinunits.h"
#include "clientmodel.h"
#include "guiconstants.h"
#include "guiutil.h"
#include "init.h"
#include "obfuscation.h"
#include "obfuscationconfig.h"
#include "optionsmodel.h"
#include "transactionfilterproxy.h"
#include "transactiontablemodel.h"
#include "walletmodel.h"
#include "masternodeman.h"
#include "main.h"
#include "chainparams.h"
#include "amount.h"
#include "addressbookpage.h"
#include "rpcblockchain.cpp"

#include <QAbstractItemDelegate>
#include <QPainter>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QDesktopServices>



#define DECORATION_SIZE 38
#define ICON_OFFSET 16
#define NUM_ITEMS 10

extern CWallet* pwalletMain;

class TxViewDelegate : public QAbstractItemDelegate
{
    Q_OBJECT
public:
    TxViewDelegate() : QAbstractItemDelegate(), unit(BitcoinUnits::FDR)
    {
    }

    inline void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        QSettings settings;
        QString theme = settings.value("theme", "dblue").toString();

        painter->save();

        QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        QRect mainRect = option.rect;
        mainRect.moveLeft(ICON_OFFSET);
        QRect decorationRect(mainRect.topLeft(), QSize(DECORATION_SIZE - 6, DECORATION_SIZE - 6));
        int xspace = DECORATION_SIZE + 8;
        int ypad = 6;
        int halfheight = (mainRect.height() - 2 * ypad) / 2;
        QRect amountRect(mainRect.left() + xspace, mainRect.top() + ypad, mainRect.width() - xspace - ICON_OFFSET, halfheight);
        QRect addressRect(mainRect.left() + xspace, mainRect.top() + ypad + halfheight, mainRect.width() - xspace, halfheight);
        icon.paint(painter, decorationRect);

        QDateTime date = index.data(TransactionTableModel::DateRole).toDateTime();
        QString address = index.data(Qt::DisplayRole).toString();
        qint64 amount = index.data(TransactionTableModel::AmountRole).toLongLong();
        bool confirmed = index.data(TransactionTableModel::ConfirmedRole).toBool();
        //QVariant value = index.data(Qt::ForegroundRole);
        QColor foreground = COLOR_BLACK;

        // paint address
        if (theme.operator==("dark")) foreground = QColor(140, 104, 76); //"#8C684C"
        else if (theme.operator==("dblue")) foreground = QColor(205, 220, 234);
        else foreground = COLOR_BLACK;
        //if (value.canConvert<QBrush>()) {
        //    QBrush brush = qvariant_cast<QBrush>(value);
        //    foreground = brush.color();
        //}
        //if (theme.operator==("dark")) foreground = QColor(144, 144, 144);
        //else if (theme.operator==("dblue")) foreground = QColor(103, 119, 127);
        //else foreground = foreground;
        painter->setPen(foreground);
        QRect boundingRect;
        painter->drawText(addressRect, Qt::AlignLeft | Qt::AlignVCenter, address, &boundingRect);

        // paint amount
        if (amount < 0) {
            if (theme.operator==("dark")) foreground = QColor(220, 50, 50); //"#DC3232"
            else if (theme.operator==("dblue")) foreground = QColor(220, 50, 50);
            else foreground = COLOR_NEGATIVE;
        } else if (!confirmed) {
            if (theme.operator==("dark")) foreground = QColor(151, 135, 117); //"#978775"
            else if (theme.operator==("dblue")) foreground = QColor(205, 220, 234);
            else foreground = COLOR_UNCONFIRMED;
        } else {
            if (theme.operator==("dark")) foreground = QColor(240, 216, 174); //"#F0D8AE"
            else if (theme.operator==("dblue")) foreground = QColor(205, 220, 234);
            else foreground = COLOR_BLACK;
        }
        painter->setPen(foreground);
        QString amountText = BitcoinUnits::formatWithUnit(unit, amount, true, BitcoinUnits::separatorAlways);
        if (!confirmed) {
            amountText = QString("[") + amountText + QString("]");
        }
        painter->drawText(amountRect, Qt::AlignRight | Qt::AlignVCenter, amountText);

        // paint date
        if (theme.operator==("dark")) foreground = QColor(240, 216, 174); //"#F0D8AE"
        else if (theme.operator==("dblue")) foreground = QColor(205, 220, 234);
        else foreground = COLOR_BLACK;
        painter->drawText(amountRect, Qt::AlignLeft | Qt::AlignVCenter, GUIUtil::dateTimeStr(date));

        painter->restore();
    }

    inline QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        return QSize(DECORATION_SIZE, DECORATION_SIZE);
    }

    int unit;
};
#include "overviewpage.moc"

OverviewPage::OverviewPage(QWidget* parent) : QWidget(parent),
                                              ui(new Ui::OverviewPage),
                                              clientModel(0),
                                              walletModel(0),
                                              currentBalance(-1),
                                              currentUnconfirmedBalance(-1),
                                              currentImmatureBalance(-1),
                                              currentWatchOnlyBalance(-1),
                                              currentWatchUnconfBalance(-1),
                                              currentWatchImmatureBalance(-1),
                                              txdelegate(new TxViewDelegate()),
                                              filter(0)
{
    nDisplayUnit = 0; // just make sure it's not unitialized
    ui->setupUi(this);

    ui->pushButton_Website->setIcon(QIcon(GUIUtil::getThemeImage(":/icons/website")));
    ui->pushButton_Website->setStatusTip(tr("Go to FDR Website"));
    ui->pushButton_Discord->setIcon(QIcon(GUIUtil::getThemeImage(":/icons/discord")));
    ui->pushButton_Discord->setStatusTip(tr("Join FDR Discord"));
    ui->pushButton_Telegram->setIcon(QIcon(GUIUtil::getThemeImage(":/icons/telegram")));
    ui->pushButton_Telegram->setStatusTip(tr("Join FDR Telegram"));
    ui->pushButton_Twitter->setIcon(QIcon(GUIUtil::getThemeImage(":/icons/twitter")));
    ui->pushButton_Twitter->setStatusTip(tr("Read FDR Twitter"));
    ui->pushButton_Explorer->setIcon(QIcon(GUIUtil::getThemeImage(":/icons/explorer")));
    ui->pushButton_Explorer->setStatusTip(tr("Go to FDR Explorer"));
    ui->pushButton_Facebook->setIcon(QIcon(GUIUtil::getThemeImage(":/icons/facebook")));
    ui->pushButton_Facebook->setStatusTip(tr("Read FDR Facebook"));
    ui->pushButton_Medium->setIcon(QIcon(GUIUtil::getThemeImage(":/icons/medium")));
    ui->pushButton_Medium->setStatusTip(tr("Read FDR Medium"));

    // Recent transactions
    ui->listTransactions->setItemDelegate(txdelegate);
    ui->listTransactions->setIconSize(QSize(DECORATION_SIZE, DECORATION_SIZE));
  //ui->listTransactions->setMinimumHeight(NUM_ITEMS * (DECORATION_SIZE + 2));
    ui->listTransactions->setAttribute(Qt::WA_MacShowFocusRect, false);

    connect(ui->listTransactions, SIGNAL(clicked(QModelIndex)), this, SLOT(handleTransactionClicked(QModelIndex)));
    //ui->AdditionalFeatures->setTabEnabled(1,false);

    // init "out of sync" warning labels
    ui->labelWalletStatus->setText("(" + tr("out of sync") + ")");
  //  ui->labelObfuscationSyncStatus->setText("(" + tr("out of sync") + ")");
    ui->labelTransactionsStatus->setText("(" + tr("out of sync") + ")");

    if (fLiteMode) {
        //ui->frameObfuscation->setVisible(false);
    } else {
        if (fMasterNode) {
            // ui->toggleObfuscation->setText("(" + tr("Disabled") + ")");
            // ui->obfuscationAuto->setText("(" + tr("Disabled") + ")");
            // ui->obfuscationReset->setText("(" + tr("Disabled") + ")");
            // ui->frameObfuscation->setEnabled(false);
        } else {
            if (!fEnableObfuscation) {
                //ui->toggleObfuscation->setText(tr("Start Obfuscation"));
            } else {
                //ui->toggleObfuscation->setText(tr("Stop Obfuscation"));
            }
            timer = new QTimer(this);
            connect(timer, SIGNAL(timeout()), this, SLOT(obfuScationStatus()));
            timer->start(1000);
        }
    }
    //information block update
    timerinfo_mn = new QTimer(this);
    connect(timerinfo_mn, SIGNAL(timeout()), this, SLOT(updateMasternodeInfo()));
    timerinfo_mn->start(1000);

    timerinfo_blockchain = new QTimer(this);
    connect(timerinfo_blockchain, SIGNAL(timeout()), this, SLOT(updateBlockChainInfo()));
    timerinfo_blockchain->start(1000); //30sec

    // start with displaying the "out of sync" warnings
    showOutOfSyncWarning(true);
}

void OverviewPage::handleTransactionClicked(const QModelIndex& index)
{
    if (filter)
        emit transactionClicked(filter->mapToSource(index));
}

OverviewPage::~OverviewPage()
{
    if (!fLiteMode && !fMasterNode) disconnect(timer, SIGNAL(timeout()), this, SLOT(obfuScationStatus()));
    delete ui;
}

void OverviewPage::setBalance(const CAmount& balance, const CAmount& unconfirmedBalance, const CAmount& immatureBalance, const CAmount& anonymizedBalance, const CAmount& watchOnlyBalance, const CAmount& watchUnconfBalance, const CAmount& watchImmatureBalance)
{
    currentBalance = balance - immatureBalance;
    currentUnconfirmedBalance = unconfirmedBalance;
    currentImmatureBalance = immatureBalance;
    currentAnonymizedBalance = anonymizedBalance;
    currentWatchOnlyBalance = watchOnlyBalance;
    currentWatchUnconfBalance = watchUnconfBalance;
    currentWatchImmatureBalance = watchImmatureBalance;

    // fdreserve labels

    if(balance != 0)
        ui->labelBalance->setText(BitcoinUnits::floorHtmlWithoutUnit(nDisplayUnit, currentBalance, false, BitcoinUnits::separatorNever));
    ui->labelUnconfirmed->setText(BitcoinUnits::floorHtmlWithoutUnit(nDisplayUnit, unconfirmedBalance, false, BitcoinUnits::separatorNever));
    ui->labelImmature->setText(BitcoinUnits::floorHtmlWithoutUnit(nDisplayUnit, immatureBalance, false, BitcoinUnits::separatorNever));
    //ui->labelAnonymized->setText(BitcoinUnits::floorHtmlWithoutUnit(nDisplayUnit, anonymizedBalance, false, BitcoinUnits::separatorAlways));
    ui->labelTotal->setText(BitcoinUnits::floorHtmlWithoutUnit(nDisplayUnit, currentBalance + unconfirmedBalance + immatureBalance, false, BitcoinUnits::separatorNever));


    // Watchonly labels
      // only show immature (newly mined) balance if it's non-zero, so as not to complicate things
    // for the non-mining users
    bool showImmature = immatureBalance != 0;
    bool showWatchOnlyImmature = watchImmatureBalance != 0;

    // for symmetry reasons also show immature label when the watch-only one is shown
    ui->labelImmature->setVisible(showImmature || showWatchOnlyImmature);
    ui->labelImmatureText->setVisible(showImmature || showWatchOnlyImmature);
//    ui->label_fdreserve4->setVisible(showImmature || showWatchOnlyImmature);

   // ui->labelWatchImmature->setVisible(showWatchOnlyImmature); // show watch-only immature balance

    updateObfuscationProgress();

    static int cachedTxLocks = 0;

    if (cachedTxLocks != nCompleteTXLocks) {
        cachedTxLocks = nCompleteTXLocks;
        ui->listTransactions->update();
    }
}

// show/hide watch-only labels
void OverviewPage::updateWatchOnlyLabels(bool showWatchOnly)
{
       if (!showWatchOnly) {
       // ui->labelWatchImmature->hide();
    } else {
        ui->labelBalance->setIndent(20);
        ui->labelUnconfirmed->setIndent(20);
        ui->labelImmature->setIndent(20);
        ui->labelTotal->setIndent(20);
    }
}

void OverviewPage::setClientModel(ClientModel* model)
{
    this->clientModel = model;
    if (model) {
        // Show warning if this is a prerelease version
        connect(model, SIGNAL(alertsChanged(QString)), this, SLOT(updateAlerts(QString)));
        updateAlerts(model->getStatusBarWarnings());
    }
}

void OverviewPage::setWalletModel(WalletModel* model)
{
    this->walletModel = model;
    if (model && model->getOptionsModel()) {
        // Set up transaction list
        filter = new TransactionFilterProxy();
        filter->setSourceModel(model->getTransactionTableModel());
        filter->setLimit(NUM_ITEMS);
        filter->setDynamicSortFilter(true);
        filter->setSortRole(Qt::EditRole);
        filter->setShowInactive(false);
        filter->sort(TransactionTableModel::Date, Qt::DescendingOrder);

        ui->listTransactions->setModel(filter);
        ui->listTransactions->setModelColumn(TransactionTableModel::ToAddress);



        //----------
        // Keep up to date with wallet
//        setBalance(model->getBalance(), model->getUnconfirmedBalance(), model->getImmatureBalance(), model->getAnonymizedBalance(),
//        model->getWatchBalance(), model->getWatchUnconfirmedBalance(), model->getWatchImmatureBalance());
        connect(model, SIGNAL(balanceChanged(CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount)), this, SLOT(setBalance(CAmount, CAmount, CAmount, CAmount, CAmount, CAmount, CAmount)));

        connect(model->getOptionsModel(), SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

        // connect(ui->obfuscationAuto, SIGNAL(clicked()), this, SLOT(obfuscationAuto()));
        // connect(ui->obfuscationReset, SIGNAL(clicked()), this, SLOT(obfuscationReset()));
        // connect(ui->toggleObfuscation, SIGNAL(clicked()), this, SLOT(toggleObfuscation()));
        connect(model, SIGNAL(notifyWatchonlyChanged(bool)), this, SLOT(updateWatchOnlyLabels(bool)));
        connect(ui->blabel_fdreserve, SIGNAL(clicked()), this, SLOT(openMyAddresses()));

        emit model->makeBalance();
    }

    // update the display unit, to not use the default ("fdreserve")
    updateDisplayUnit();
}

void OverviewPage::updateDisplayUnit()
{
    if (walletModel && walletModel->getOptionsModel()) {
        nDisplayUnit = walletModel->getOptionsModel()->getDisplayUnit();
        if (currentBalance != -1)
            setBalance(currentBalance, currentUnconfirmedBalance, currentImmatureBalance, currentAnonymizedBalance,
                currentWatchOnlyBalance, currentWatchUnconfBalance, currentWatchImmatureBalance);

        // Update txdelegate->unit with the current unit
        txdelegate->unit = nDisplayUnit;

        ui->listTransactions->update();
    }
}

void OverviewPage::updateAlerts(const QString& warnings)
{
  //  this->ui->labelAlerts->setVisible(!warnings.isEmpty());
  //  this->ui->labelAlerts->setText(warnings);
}

double roi1, roi2, roi3;

void OverviewPage::updateMasternodeInfo()
{
  int CurrentBlock = clientModel->getNumBlocks();

  if (masternodeSync.IsBlockchainSynced() && masternodeSync.IsSynced())
  {

   int mn1=0;
   int mn2=0;
   int mn3=0;
   int totalmn=0;
   std::vector<CMasternode> vMasternodes = mnodeman.GetFullMasternodeMap();
    for(auto& mn : vMasternodes)
    {
       switch ( mn.Level())
       {
           case 1:
           mn1++;break;
           case 2:
           mn2++;break;
           case 3:
           mn3++;break;
       }

    }
    totalmn=mn1+mn2+mn3;
    ui->labelMnTotal_Value->setText(QString::number(totalmn));
    int maxMnValue = std::max( { mn1, mn2, mn3}, [](const int& s1, const int& s2) { return s1 < s2; });

    ui->graphMN1->setMaximum(maxMnValue);
    ui->graphMN2->setMaximum(maxMnValue);
    ui->graphMN3->setMaximum(maxMnValue);
    ui->graphMN1->setValue(mn1);
    ui->graphMN2->setValue(mn2);
    ui->graphMN3->setValue(mn3);

    // TODO: need a read actual 24h blockcount from chain
    int BlockCount24h = block24hCount > 0 ? block24hCount : 1440;

    // update ROI
    double BlockReward = GetBlockValue(CurrentBlock);
    BlockReward -= BlockReward * GetSporkValue(SPORK_11_DEV_FEE) / 100;
    (mn1==0) ? roi1 = 0 : roi1 = (GetMasternodePayment(1, BlockReward)*BlockCount24h)/mn1/COIN;
    (mn2==0) ? roi2 = 0 : roi2 = (GetMasternodePayment(2, BlockReward)*BlockCount24h)/mn2/COIN;
    (mn3==0) ? roi3 = 0 : roi3 = (GetMasternodePayment(3, BlockReward)*BlockCount24h)/mn3/COIN;
    if (CurrentBlock >= 0) {
        ui->roi_11->setText(mn1==0 ? "-" : QString::number(roi1,'f',0).append("  |"));
        ui->roi_21->setText(mn2==0 ? "-" : QString::number(roi2,'f',0).append("  |"));
        ui->roi_31->setText(mn3==0 ? "-" : QString::number(roi3,'f',0).append("  |"));

        ui->roi_12->setText(mn1==0 ? " " : QString::number(  1000/roi1,'f',1).append(" days"));
        ui->roi_22->setText(mn2==0 ? " " : QString::number( 10000/roi2,'f',1).append(" days"));
        ui->roi_32->setText(mn3==0 ? " " : QString::number( 50000/roi3,'f',1).append(" days"));
    }
    CAmount tNodesSumm = mn1*1000 + mn2*10000 + mn3*50000;
    CAmount tMoneySupply = chainActive.Tip()->nMoneySupply;
    double tLocked = tMoneySupply > 0 ? 100 * static_cast<double>(tNodesSumm) / static_cast<double>(tMoneySupply / COIN) : 0;
    ui->label_LockedCoin_value->setText(QString::number(tNodesSumm).append(" (" + QString::number(tLocked,'f',1) + "%)"));

    // update timer
    if (timerinfo_mn->interval() == 1000)
            timerinfo_mn->setInterval(10000);
  }

  // update collateral info
  if (CurrentBlock >= 0) {
      ui->label_lcolat->setText("1000 FDR");
      ui->label_mcolat->setText("10000 FDR");
      ui->label_fcolat->setText("50000 FDR");
  }

}

void OverviewPage::updateBlockChainInfo()
{
    if (masternodeSync.IsBlockchainSynced())
    {
        int CurrentBlock = clientModel->getNumBlocks();
        int64_t netHashRate = chainActive.GetNetworkHashPS(24, CurrentBlock-1);
        double BlockReward = GetBlockValue(CurrentBlock);
        double BlockRewardfdreserve =  static_cast<double>(BlockReward/COIN);
        double CurrentDiff = GetDifficulty();

        ui->label_CurrentBlock_value->setText(QString::number(CurrentBlock));

        ui->label_Nethash->setText(tr("Difficulty:"));
        ui->label_Nethash_value->setText(QString::number(CurrentDiff,'f',4));

        ui->label_CurrentBlockReward_value->setText(QString::number(BlockRewardfdreserve, 'f', 1).append(" | ") + QString::number(GetSporkValue(SPORK_11_DEV_FEE)).append("%"));

        ui->label_Supply_value->setText(QString::number(chainActive.Tip()->nMoneySupply / COIN).append(" FDR"));

        ui->label_24hBlock_value->setText(QString::number(block24hCount));
        ui->label_24hPoS_value->setText(QString::number(static_cast<double>(posMin)/COIN,'f',1).append(" | ") + QString::number(static_cast<double>(posMax)/COIN,'f',1));
        ui->label_24hPoSMedian_value->setText(QString::number(static_cast<double>(posMedian)/COIN,'f',1));
    }
}

void OverviewPage::openMyAddresses()
{
    AddressBookPage* dlg = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::ReceivingTab, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setModel(walletModel->getAddressTableModel());
    dlg->show();
}

void OverviewPage::showOutOfSyncWarning(bool fShow)
{
    ui->labelWalletStatus->setVisible(fShow);
    //ui->labelObfuscationSyncStatus->setVisible(fShow);
    ui->labelTransactionsStatus->setVisible(fShow);
}

void OverviewPage::updateObfuscationProgress()
{
    return; // not used at this time

    if (!masternodeSync.IsBlockchainSynced() || ShutdownRequested()) return;

    if (!pwalletMain) return;

    QString strAmountAndRounds;
    QString strAnonymizePhcAmount = BitcoinUnits::formatHtmlWithUnit(nDisplayUnit, nAnonymizePhcAmount * COIN, false, BitcoinUnits::separatorAlways);

    if (currentBalance == 0) {
        // ui->obfuscationProgress->setValue(0);
        // ui->obfuscationProgress->setToolTip(tr("No inputs detected"));

        // when balance is zero just show info from settings
        strAnonymizePhcAmount = strAnonymizePhcAmount.remove(strAnonymizePhcAmount.indexOf("."), BitcoinUnits::decimals(nDisplayUnit) + 1);
        strAmountAndRounds = strAnonymizePhcAmount + " / " + tr("%n Rounds", "", nObfuscationRounds);

        // ui->labelAmountRounds->setToolTip(tr("No inputs detected"));
        // ui->labelAmountRounds->setText(strAmountAndRounds);
        return;
    }

    CAmount nDenominatedConfirmedBalance;
    CAmount nDenominatedUnconfirmedBalance;
    CAmount nAnonymizableBalance;
    CAmount nNormalizedAnonymizedBalance;
    double nAverageAnonymizedRounds;

    {
        TRY_LOCK(cs_main, lockMain);
        if (!lockMain) return;

        nDenominatedConfirmedBalance = pwalletMain->GetDenominatedBalance();
        nDenominatedUnconfirmedBalance = pwalletMain->GetDenominatedBalance(true);
        nAnonymizableBalance = pwalletMain->GetAnonymizedBalance();
        nNormalizedAnonymizedBalance = pwalletMain->GetNormalizedAnonymizedBalance();
        nAverageAnonymizedRounds = pwalletMain->GetAverageAnonymizedRounds();
    }

    CAmount nMaxToAnonymize = nAnonymizableBalance + currentAnonymizedBalance + nDenominatedUnconfirmedBalance;

    // If it's more than the anon threshold, limit to that.
    if (nMaxToAnonymize > nAnonymizePhcAmount * COIN) nMaxToAnonymize = nAnonymizePhcAmount * COIN;

    if (nMaxToAnonymize == 0) return;

    if (nMaxToAnonymize >= nAnonymizePhcAmount * COIN) {
        // ui->labelAmountRounds->setToolTip(tr("Found enough compatible inputs to anonymize %1")
        //                                       .arg(strAnonymizePhcAmount));
        strAnonymizePhcAmount = strAnonymizePhcAmount.remove(strAnonymizePhcAmount.indexOf("."), BitcoinUnits::decimals(nDisplayUnit) + 1);
        strAmountAndRounds = strAnonymizePhcAmount + " / " + tr("%n Rounds", "", nObfuscationRounds);
    } else {
        QString strMaxToAnonymize = BitcoinUnits::formatHtmlWithUnit(nDisplayUnit, nMaxToAnonymize, false, BitcoinUnits::separatorAlways);
        // ui->labelAmountRounds->setToolTip(tr("Not enough compatible inputs to anonymize <span style='color:red;'>%1</span>,<br>"
        //                                      "will anonymize <span style='color:red;'>%2</span> instead")
        //                                       .arg(strAnonymizePhcAmount)
        //                                       .arg(strMaxToAnonymize));
        strMaxToAnonymize = strMaxToAnonymize.remove(strMaxToAnonymize.indexOf("."), BitcoinUnits::decimals(nDisplayUnit) + 1);
        strAmountAndRounds = "<span style='color:red;'>" +
                             QString(BitcoinUnits::factor(nDisplayUnit) == 1 ? "" : "~") + strMaxToAnonymize +
                             " / " + tr("%n Rounds", "", nObfuscationRounds) + "</span>";
    }
    //ui->labelAmountRounds->setText(strAmountAndRounds);

    // calculate parts of the progress, each of them shouldn't be higher than 1
    // progress of denominating
    float denomPart = 0;
    // mixing progress of denominated balance
    float anonNormPart = 0;
    // completeness of full amount anonimization
    float anonFullPart = 0;

    CAmount denominatedBalance = nDenominatedConfirmedBalance + nDenominatedUnconfirmedBalance;
    denomPart = (float)denominatedBalance / nMaxToAnonymize;
    denomPart = denomPart > 1 ? 1 : denomPart;
    denomPart *= 100;

    anonNormPart = (float)nNormalizedAnonymizedBalance / nMaxToAnonymize;
    anonNormPart = anonNormPart > 1 ? 1 : anonNormPart;
    anonNormPart *= 100;

    anonFullPart = (float)currentAnonymizedBalance / nMaxToAnonymize;
    anonFullPart = anonFullPart > 1 ? 1 : anonFullPart;
    anonFullPart *= 100;

    // apply some weights to them ...
    float denomWeight = 1;
    float anonNormWeight = nObfuscationRounds;
    float anonFullWeight = 2;
    float fullWeight = denomWeight + anonNormWeight + anonFullWeight;
    // ... and calculate the whole progress
    float denomPartCalc = ceilf((denomPart * denomWeight / fullWeight) * 100) / 100;
    float anonNormPartCalc = ceilf((anonNormPart * anonNormWeight / fullWeight) * 100) / 100;
    float anonFullPartCalc = ceilf((anonFullPart * anonFullWeight / fullWeight) * 100) / 100;
    float progress = denomPartCalc + anonNormPartCalc + anonFullPartCalc;
    if (progress >= 100) progress = 100;

    //ui->obfuscationProgress->setValue(progress);

    QString strToolPip = ("<b>" + tr("Overall progress") + ": %1%</b><br/>" +
                          tr("Denominated") + ": %2%<br/>" +
                          tr("Mixed") + ": %3%<br/>" +
                          tr("Anonymized") + ": %4%<br/>" +
                          tr("Denominated inputs have %5 of %n rounds on average", "", nObfuscationRounds))
                             .arg(progress)
                             .arg(denomPart)
                             .arg(anonNormPart)
                             .arg(anonFullPart)
                             .arg(nAverageAnonymizedRounds);
    //ui->obfuscationProgress->setToolTip(strToolPip);
}


void OverviewPage::obfuScationStatus()
{
    static int64_t nLastDSProgressBlockTime = 0;

    int nBestHeight = chainActive.Tip()->nHeight;

    // we we're processing more then 1 block per second, we'll just leave
    if (((nBestHeight - obfuScationPool.cachedNumBlocks) / (GetTimeMillis() - nLastDSProgressBlockTime + 1) > 1)) return;
    nLastDSProgressBlockTime = GetTimeMillis();

    if (!fEnableObfuscation) {
        if (nBestHeight != obfuScationPool.cachedNumBlocks) {
            obfuScationPool.cachedNumBlocks = nBestHeight;
            updateObfuscationProgress();

            // ui->obfuscationEnabled->setText(tr("Disabled"));
            // ui->obfuscationStatus->setText("");
            // ui->toggleObfuscation->setText(tr("Start Obfuscation"));
        }

        return;
    }

    // check obfuscation status and unlock if needed
    if (nBestHeight != obfuScationPool.cachedNumBlocks) {
        // Balance and number of transactions might have changed
        obfuScationPool.cachedNumBlocks = nBestHeight;
        updateObfuscationProgress();

        // ui->obfuscationEnabled->setText(tr("Enabled"));
    }

    QString strStatus = QString(obfuScationPool.GetStatus().c_str());

    QString s = strStatus;

    // if (s != ui->obfuscationStatus->text())
    //     LogPrintf("Last Obfuscation message: %s\n", strStatus.toStdString());

    //ui->obfuscationStatus->setText(s);

    if (obfuScationPool.sessionDenom == 0) {
        //ui->labelSubmittedDenom->setText(tr("N/A"));
    } else {
        std::string out;
        obfuScationPool.GetDenominationsToString(obfuScationPool.sessionDenom, out);
        QString s2(out.c_str());
        //ui->labelSubmittedDenom->setText(s2);
    }
}

void OverviewPage::obfuscationAuto()
{
    obfuScationPool.DoAutomaticDenominating();
}

void OverviewPage::obfuscationReset()
{
    obfuScationPool.Reset();

    QMessageBox::warning(this, tr("Obfuscation"),
        tr("Obfuscation was successfully reset."),
        QMessageBox::Ok, QMessageBox::Ok);
}

void OverviewPage::toggleObfuscation()
{
    QSettings settings;
    // Popup some information on first mixing
    QString hasMixed = settings.value("hasMixed").toString();
    if (hasMixed.isEmpty()) {
        QMessageBox::information(this, tr("Obfuscation"),
            tr("If you don't want to see internal Obfuscation fees/transactions select \"Most Common\" as Type on the \"Transactions\" tab."),
            QMessageBox::Ok, QMessageBox::Ok);
        settings.setValue("hasMixed", "hasMixed");
    }
    if (!fEnableObfuscation) {
        int64_t balance = currentBalance;
        float minAmount = 14.90 * COIN;
        if (balance < minAmount) {
            QString strMinAmount(BitcoinUnits::formatWithUnit(nDisplayUnit, minAmount));
            QMessageBox::warning(this, tr("Obfuscation"),
                tr("Obfuscation requires at least %1 to use.").arg(strMinAmount),
                QMessageBox::Ok, QMessageBox::Ok);
            return;
        }

        // if wallet is locked, ask for a passphrase
        if (walletModel->getEncryptionStatus() == WalletModel::Locked) {
            WalletModel::UnlockContext ctx(walletModel->requestUnlock(false));
            if (!ctx.isValid()) {
                //unlock was cancelled
                obfuScationPool.cachedNumBlocks = std::numeric_limits<int>::max();
                QMessageBox::warning(this, tr("Obfuscation"),
                    tr("Wallet is locked and user declined to unlock. Disabling Obfuscation."),
                    QMessageBox::Ok, QMessageBox::Ok);
                if (fDebug) LogPrintf("Wallet is locked and user declined to unlock. Disabling Obfuscation.\n");
                return;
            }
        }
    }

    fEnableObfuscation = !fEnableObfuscation;
    obfuScationPool.cachedNumBlocks = std::numeric_limits<int>::max();

    if (!fEnableObfuscation) {
        //ui->toggleObfuscation->setText(tr("Start Obfuscation"));
        obfuScationPool.UnlockCoins();
    } else {
        //ui->toggleObfuscation->setText(tr("Stop Obfuscation"));

        /* show obfuscation configuration if client has defaults set */

        if (nAnonymizePhcAmount == 0) {
            ObfuscationConfig dlg(this);
            dlg.setModel(walletModel);
            dlg.exec();
        }
    }
}

void OverviewPage::on_pushButton_Website_clicked() {
    QDesktopServices::openUrl(QUrl("https://www.fdreserve.com/", QUrl::TolerantMode));
}
void OverviewPage::on_pushButton_Discord_clicked() {
    QDesktopServices::openUrl(QUrl("https://discord.gg/cQSuPhF", QUrl::TolerantMode));
}
void OverviewPage::on_pushButton_Telegram_clicked() {
    QDesktopServices::openUrl(QUrl("https://t.me/FDR_Project_EN", QUrl::TolerantMode));
}
void OverviewPage::on_pushButton_Twitter_clicked() {
    QDesktopServices::openUrl(QUrl("https://twitter.com/FD_Reserve", QUrl::TolerantMode));
}
/*
void OverviewPage::on_pushButton_Reddit_clicked() {
    QDesktopServices::openUrl(QUrl("", QUrl::TolerantMode));
}
*/
void OverviewPage::on_pushButton_Medium_clicked() {
    QDesktopServices::openUrl(QUrl("https://medium.com/@FDR_Project_EN", QUrl::TolerantMode));
}
void OverviewPage::on_pushButton_Facebook_clicked() {
    QDesktopServices::openUrl(QUrl("https://www.facebook.com/FrenchDigitalReserve/", QUrl::TolerantMode));
}
void OverviewPage::on_pushButton_Explorer_clicked() {
    QDesktopServices::openUrl(QUrl("https://explorer.fdreserve.com/", QUrl::TolerantMode));
}
