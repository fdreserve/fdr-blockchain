// Copyright (c) 2011-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QT_OPTIONSDIALOG_H
#define BITCOIN_QT_OPTIONSDIALOG_H

#include <QDialog>

class OptionsModel;
class QValidatedLineEdit;

QT_BEGIN_NAMESPACE
class QDataWidgetMapper;
QT_END_NAMESPACE

namespace Ui
{
class OptionsDialog;
}

/** Preferences dialog. */
class OptionsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit OptionsDialog(QWidget* parent, bool enableWallet);
    ~OptionsDialog();

    void setModel(OptionsModel* model);
    void setMapper();

    /* Update StakeSplitThreshold's value in wallet */
//    void setStakeSplitThreshold();
    /* Update AutoCombineRewards value in wallet */
//    void setAutoCombineRewards();
    /* Update Settings value in wallet */
    void setWalletOptions();

protected:
    bool eventFilter(QObject* object, QEvent* event);

private slots:
    /* enable OK button */
    void enableOkButton();
    /* disable OK button */
    void disableOkButton();
    /* set OK button state (enabled / disabled) */
    void setOkButtonState(bool fState);
    void on_resetButton_clicked();
    void on_okButton_clicked();
    void on_cancelButton_clicked();

    void showRestartWarning(bool fPersistent = false);
    void clearStatusLabel();
    void doProxyIpChecks(QValidatedLineEdit* pUiProxyIp, int nProxyPort);

    void on_stakeThresholdEdit_valueChanged(int i);
    void on_autoCombineEdit_valueChanged(int i);
    void on_autoCombineLimitEdit_valueChanged(int i);
    void on_stakeThresholdSlider_valueChanged(int value);
    void on_autoCombineSlider_valueChanged(int value);
    void on_autoCombineLimitSlider_valueChanged(int value);
    void on_autoCombineCheckBox_stateChanged(int state);

signals:
    void proxyIpChecks(QValidatedLineEdit* pUiProxyIp, int nProxyPort);

private:
    Ui::OptionsDialog* ui;
    OptionsModel* model;
    QDataWidgetMapper* mapper;
    bool fProxyIpValid;
};

#endif // BITCOIN_QT_OPTIONSDIALOG_H
