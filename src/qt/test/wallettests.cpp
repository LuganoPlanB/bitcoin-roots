// Copyright (c) 2015-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/wallettests.h>
#include <qt/test/util.h>

#include <wallet/coincontrol.h>
#include <interfaces/chain.h>
#include <interfaces/node.h>
#include <key_io.h>
#include <logging.h>
#include <policy/feerate.h>
#include <qt/bitcoinamountfield.h>
#include <qt/bitcoinunits.h>
#include <qt/clientmodel.h>
#include <qt/coincontroldialog.h>
#include <qt/optionsmodel.h>
#include <qt/overviewpage.h>
#include <qt/platformstyle.h>
#include <qt/qvalidatedlineedit.h>
#include <qt/receivecoinsdialog.h>
#include <qt/receiverequestdialog.h>
#include <qt/recentrequeststablemodel.h>
#include <qt/sendcoinsdialog.h>
#include <qt/sendcoinsentry.h>
#include <qt/sweepdialog.h>
#include <qt/transactiontablemodel.h>
#include <qt/transactionview.h>
#include <qt/walletmodel.h>
#include <script/solver.h>
#include <test/util/setup_common.h>
#include <util/rbf.h>
#include <validation.h>
#include <wallet/test/util.h>
#include <wallet/wallet.h>

#include <chrono>
#include <functional>
#include <initializer_list>
#include <list>
#include <memory>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QObject>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QListView>
#include <QDialogButtonBox>

using wallet::AddWallet;
using wallet::CWallet;
using wallet::CreateMockableWalletDatabase;
using wallet::RemoveWallet;
using wallet::WALLET_FLAG_DESCRIPTORS;
using wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS;
using wallet::WalletContext;
using wallet::WalletDescriptor;
using wallet::WalletRescanReserver;

namespace
{
class LogCapture
{
public:
    LogCapture()
        : m_connection{LogInstance().PushBackCallback([this](const std::string& message) { output += message; })}
    {
    }

    ~LogCapture() { LogInstance().DeleteCallback(m_connection); }

    std::string output;

private:
    std::list<std::function<void(const std::string&)>>::iterator m_connection;
};

//! Press "Yes" or "Cancel" buttons in modal send confirmation dialog.
void ConfirmSend(QString* text = nullptr, QMessageBox::StandardButton confirm_type = QMessageBox::Yes)
{
    QTimer::singleShot(0, [text, confirm_type]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->inherits("SendConfirmationDialog")) {
                SendConfirmationDialog* dialog = qobject_cast<SendConfirmationDialog*>(widget);
                if (text) *text = dialog->text();
                QAbstractButton* button = dialog->button(confirm_type);
                button->setEnabled(true);
                button->click();
            }
        }
    });
}

//! Send coins to address and return txid.
uint256 SendCoins(CWallet& wallet, SendCoinsDialog& sendCoinsDialog, const CTxDestination& address, CAmount amount, bool rbf,
                  QMessageBox::StandardButton confirm_type = QMessageBox::Yes, QString* confirmation_text = nullptr)
{
    QVBoxLayout* entries = sendCoinsDialog.findChild<QVBoxLayout*>("entries");
    SendCoinsEntry* entry = qobject_cast<SendCoinsEntry*>(entries->itemAt(0)->widget());
    entry->findChild<QValidatedLineEdit*>("payTo")->setText(QString::fromStdString(EncodeDestination(address)));
    entry->findChild<BitcoinAmountField*>("payAmount")->setValue(amount);
    sendCoinsDialog.findChild<QFrame*>("frameFee")
        ->findChild<QFrame*>("frameFeeSelection")
        ->findChild<QCheckBox*>("optInRBF")
        ->setCheckState(rbf ? Qt::Checked : Qt::Unchecked);
    uint256 txid;
    boost::signals2::scoped_connection c(wallet.NotifyTransactionChanged.connect([&txid](const uint256& hash, ChangeType status) {
        if (status == CT_NEW) txid = hash;
    }));
    ConfirmSend(confirmation_text, confirm_type);
    bool invoked = QMetaObject::invokeMethod(&sendCoinsDialog, "sendButtonClicked", Q_ARG(bool, false));
    assert(invoked);
    return txid;
}

//! Find index of txid in transaction list.
QModelIndex FindTx(const QAbstractItemModel& model, const uint256& txid)
{
    QString hash = QString::fromStdString(txid.ToString());
    int rows = model.rowCount({});
    for (int row = 0; row < rows; ++row) {
        QModelIndex index = model.index(row, 0, {});
        if (model.data(index, TransactionTableModel::TxHashRole) == hash) {
            return index;
        }
    }
    return {};
}

//! Invoke bumpfee on txid and check results.
void BumpFee(TransactionView& view, const uint256& txid, bool expectDisabled, std::string expectError, bool cancel)
{
    QTableView* table = view.findChild<QTableView*>("transactionView");
    QModelIndex index = FindTx(*table->selectionModel()->model(), txid);
    QVERIFY2(index.isValid(), "Could not find BumpFee txid");

    // Select row in table, invoke context menu, and make sure bumpfee action is
    // enabled or disabled as expected.
    QAction* action = view.findChild<QAction*>("bumpFeeAction");
    table->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    action->setEnabled(expectDisabled);
    table->customContextMenuRequested({});
    QCOMPARE(action->isEnabled(), !expectDisabled);

    action->setEnabled(true);
    QString text;
    if (expectError.empty()) {
        ConfirmSend(&text, cancel ? QMessageBox::Cancel : QMessageBox::Yes);
    } else {
        ConfirmMessage(&text, 0ms);
    }
    action->trigger();
    QVERIFY(text.indexOf(QString::fromStdString(expectError)) != -1);
}

void CompareBalance(WalletModel& walletModel, CAmount expected_balance, QLabel* balance_label_to_check)
{
    BitcoinUnit unit = walletModel.getOptionsModel()->getDisplayUnit();
    QString balanceComparison = BitcoinUnits::formatWithUnit(unit, expected_balance, false, BitcoinUnits::SeparatorStyle::ALWAYS);
    QCOMPARE(balance_label_to_check->text().trimmed(), balanceComparison);
}

// Verify the 'useAvailableBalance' functionality. With and without manually selected coins.
// Case 1: No coin control selected coins.
// 'useAvailableBalance' should fill the amount edit box with the total available balance
// Case 2: With coin control selected coins.
// 'useAvailableBalance' should fill the amount edit box with the sum of the selected coins values.
void VerifyUseAvailableBalance(SendCoinsDialog& sendCoinsDialog, const WalletModel& walletModel)
{
    // Verify first entry amount and "useAvailableBalance" button
    QVBoxLayout* entries = sendCoinsDialog.findChild<QVBoxLayout*>("entries");
    QVERIFY(entries->count() == 1); // only one entry
    SendCoinsEntry* send_entry = qobject_cast<SendCoinsEntry*>(entries->itemAt(0)->widget());
    QVERIFY(send_entry->getValue().amount == 0);
    // Now click "useAvailableBalance", check updated balance (the entire wallet balance should be set)
    Q_EMIT send_entry->useAvailableBalance(send_entry);
    QVERIFY(send_entry->getValue().amount == walletModel.getCachedBalance().balance);

    // Now manually select two coins and click on "useAvailableBalance". Then check updated balance
    // (only the sum of the selected coins should be set).
    int COINS_TO_SELECT = 2;
    auto coins = walletModel.wallet().listCoins();
    CAmount sum_selected_coins = 0;
    int selected = 0;
    QVERIFY(coins.size() == 1); // context check, coins received only on one destination
    for (const auto& [outpoint, tx_out] : coins.begin()->second) {
        sendCoinsDialog.getCoinControl()->Select(outpoint);
        sum_selected_coins += tx_out.txout.nValue;
        if (++selected == COINS_TO_SELECT) break;
    }
    QVERIFY(selected == COINS_TO_SELECT);

    // Now that we have 2 coins selected, "useAvailableBalance" should update the balance label only with
    // the sum of them.
    Q_EMIT send_entry->useAvailableBalance(send_entry);
    QVERIFY(send_entry->getValue().amount == sum_selected_coins);
}

void SyncUpWallet(const std::shared_ptr<CWallet>& wallet, interfaces::Node& node)
{
    WalletRescanReserver reserver(*wallet);
    reserver.reserve();
    CWallet::ScanResult result = wallet->ScanForWalletTransactions(Params().GetConsensus().hashGenesisBlock, /*start_height=*/0, /*max_height=*/{}, reserver, /*fUpdate=*/true, /*save_progress=*/false);
    QCOMPARE(result.status, CWallet::ScanResult::SUCCESS);
    QCOMPARE(result.last_scanned_block, WITH_LOCK(node.context()->chainman->GetMutex(), return node.context()->chainman->ActiveChain().Tip()->GetBlockHash()));
    QVERIFY(result.last_failed_block.IsNull());
}

std::shared_ptr<CWallet> SetupLegacyWatchOnlyWallet(interfaces::Node& node, TestChain100Setup& test)
{
    std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>(node.context()->chain.get(), "", CreateMockableWalletDatabase());
    wallet->LoadWallet();
    {
        LOCK(wallet->cs_wallet);
        wallet->SetWalletFlag(WALLET_FLAG_DISABLE_PRIVATE_KEYS);
        wallet->SetupLegacyScriptPubKeyMan();
        // Add watched key
        CPubKey pubKey = test.coinbaseKey.GetPubKey();
        bool import_keys = wallet->ImportPubKeys({{pubKey.GetID(), false}}, {{pubKey.GetID(), pubKey}} , /*key_origins=*/{}, /*add_keypool=*/false, /*timestamp=*/1);
        assert(import_keys);
        wallet->SetLastBlockProcessed(105, WITH_LOCK(node.context()->chainman->GetMutex(), return node.context()->chainman->ActiveChain().Tip()->GetBlockHash()));
    }
    SyncUpWallet(wallet, node);
    return wallet;
}

std::shared_ptr<CWallet> SetupDescriptorsWallet(interfaces::Node& node, TestChain100Setup& test)
{
    std::shared_ptr<CWallet> wallet = std::make_shared<CWallet>(node.context()->chain.get(), "", CreateMockableWalletDatabase());
    wallet->LoadWallet();
    LOCK(wallet->cs_wallet);
    wallet->SetWalletFlag(WALLET_FLAG_DESCRIPTORS);
    wallet->SetupDescriptorScriptPubKeyMans();

    // Add the coinbase key
    FlatSigningProvider provider;
    std::string error;
    auto descs = Parse("combo(" + EncodeSecret(test.coinbaseKey) + ")", provider, error, /* require_checksum=*/ false);
    assert(!descs.empty());
    assert(descs.size() == 1);
    auto& desc = descs.at(0);
    WalletDescriptor w_desc(std::move(desc), 0, 0, 1, 1);
    if (!wallet->AddWalletDescriptor(w_desc, provider, "", false)) assert(false);
    CTxDestination dest = GetDestinationForKey(test.coinbaseKey.GetPubKey(), wallet->m_default_address_type);
    wallet->SetAddressBook(dest, "", wallet::AddressPurpose::RECEIVE);
    wallet->SetLastBlockProcessed(105, WITH_LOCK(node.context()->chainman->GetMutex(), return node.context()->chainman->ActiveChain().Tip()->GetBlockHash()));
    SyncUpWallet(wallet, node);
    wallet->SetBroadcastTransactions(true);
    return wallet;
}

struct MiniGUI {
public:
    SendCoinsDialog sendCoinsDialog;
    TransactionView transactionView;
    OptionsModel optionsModel;
    std::unique_ptr<ClientModel> clientModel;
    std::unique_ptr<WalletModel> walletModel;

    MiniGUI(interfaces::Node& node, const PlatformStyle* platformStyle) : sendCoinsDialog(platformStyle), transactionView(platformStyle), optionsModel(node) {
        bilingual_str error;
        QVERIFY(optionsModel.Init(error));
        clientModel = std::make_unique<ClientModel>(node, &optionsModel);
    }

    void initModelForWallet(interfaces::Node& node, const std::shared_ptr<CWallet>& wallet, const PlatformStyle* platformStyle)
    {
        WalletContext& context = *node.walletLoader().context();
        AddWallet(context, wallet);
        walletModel = std::make_unique<WalletModel>(interfaces::MakeWallet(context, wallet), *clientModel, platformStyle);
        RemoveWallet(context, wallet, /* load_on_start= */ std::nullopt);
        sendCoinsDialog.setModel(walletModel.get());
        transactionView.setModel(walletModel.get());
    }

};

//! Simple qt wallet tests.
//
// Test widgets can be debugged interactively calling show() on them and
// manually running the event loop, e.g.:
//
//     sendCoinsDialog.show();
//     QEventLoop().exec();
//
// This also requires overriding the default minimal Qt platform:
//
//     QT_QPA_PLATFORM=xcb     build/bin/test_bitcoin-qt  # Linux
//     QT_QPA_PLATFORM=windows build/bin/test_bitcoin-qt  # Windows
//     QT_QPA_PLATFORM=cocoa   build/bin/test_bitcoin-qt  # macOS
void TestGUI(interfaces::Node& node, const std::shared_ptr<CWallet>& wallet)
{
    wallet->m_signal_rbf = false;

    // Create widgets for sending coins and listing transactions.
    std::unique_ptr<const PlatformStyle> platformStyle(PlatformStyle::instantiate("other"));
    MiniGUI mini_gui(node, platformStyle.get());
    mini_gui.initModelForWallet(node, wallet, platformStyle.get());
    WalletModel& walletModel = *mini_gui.walletModel;
    SendCoinsDialog& sendCoinsDialog = mini_gui.sendCoinsDialog;
    TransactionView& transactionView = mini_gui.transactionView;
    qApp->processEvents(); // Drain the fixture's queued RemoveWallet notification.

    const QString sweep_sentinel{"sweep-ui-secret-sentinel"};
    LogCapture sweep_logs;

    // Verify the guarded sweep dialog contract and asynchronous error path.
    SweepDialog sweep_dialog(&walletModel);
    QLineEdit* sweep_key = sweep_dialog.findChild<QLineEdit*>("sweepPrivateKey");
    QLineEdit* sweep_destination = sweep_dialog.findChild<QLineEdit*>("sweepDestination");
    QPushButton* sweep_preview = sweep_dialog.findChild<QPushButton*>("sweepPreview");
    QPushButton* sweep_broadcast = sweep_dialog.findChild<QPushButton*>("sweepBroadcast");
    QCheckBox* sweep_reveal = sweep_dialog.findChild<QCheckBox*>("sweepReveal");
    QLabel* sweep_result = sweep_dialog.findChild<QLabel*>("sweepResult");
    QVERIFY(sweep_key && sweep_destination && sweep_preview && sweep_broadcast && sweep_reveal && sweep_result);
    QCOMPARE(sweep_key->echoMode(), QLineEdit::Password);
    QCOMPARE(sweep_key->contextMenuPolicy(), Qt::NoContextMenu);
    QCOMPARE(sweep_key->accessibleName(), "Private key");
    QVERIFY(sweep_key->accessibleDescription().contains("Copy, cut"));
    QCOMPARE(sweep_reveal->accessibleName(), "Show private key");
    QVERIFY(sweep_reveal->accessibleDescription().contains("Copy and cut"));
    QCOMPARE(sweep_destination->accessibleName(), "Destination address");
    QVERIFY(sweep_destination->accessibleDescription().contains("selected wallet"));
    QVERIFY(sweep_destination->accessibleDescription().contains("Watch-only"));
    QCOMPARE(sweep_result->accessibleName(), "Sweep preview");
    QVERIFY(sweep_result->accessibleDescription().contains("Non-secret"));
    QCOMPARE(sweep_preview->accessibleName(), "Preview sweep");
    QVERIFY(sweep_preview->accessibleDescription().contains("Replace-By-Fee"));
    QCOMPARE(sweep_broadcast->accessibleName(), "Broadcast sweep");
    QVERIFY(sweep_broadcast->accessibleDescription().contains("fees changed"));
    sweep_dialog.show();
    sweep_key->setFocus();
    QTRY_COMPARE(qApp->focusWidget(), static_cast<QWidget*>(sweep_key));
    QTest::keyClick(sweep_key, Qt::Key_Tab);
    QTRY_COMPARE(qApp->focusWidget(), static_cast<QWidget*>(sweep_reveal));
    QTest::keyClick(sweep_reveal, Qt::Key_Tab);
    QTRY_COMPARE(qApp->focusWidget(), static_cast<QWidget*>(sweep_destination));
    QTest::keyClick(sweep_destination, Qt::Key_Tab);
    QTRY_COMPARE(qApp->focusWidget(), static_cast<QWidget*>(sweep_preview));
    sweep_reveal->setChecked(true);
    QCOMPARE(sweep_key->echoMode(), QLineEdit::Normal);
    sweep_reveal->setChecked(false);
    QCOMPARE(sweep_key->echoMode(), QLineEdit::Password);
    QVERIFY(!sweep_broadcast->isEnabled());
    auto sweep_dest{walletModel.wallet().getNewDestination(OutputType::BECH32, "")};
    QVERIFY(sweep_dest);
    sweep_destination->setText(QString::fromStdString(EncodeDestination(*sweep_dest)));
    sweep_key->setText(sweep_sentinel);
    QVERIFY(QMetaObject::invokeMethod(&sweep_dialog, "preview"));
    QVERIFY(!sweep_key->isEnabled());
    QVERIFY(sweep_key->text().isEmpty());
    QTRY_VERIFY(sweep_key->isEnabled());
    QVERIFY(!sweep_result->text().contains(sweep_sentinel));
    QVERIFY(!QString::fromStdString(sweep_logs.output).contains(sweep_sentinel));

    sweep_key->setText("not-a-private-key");
    QVERIFY(QMetaObject::invokeMethod(&sweep_dialog, "preview"));
    QVERIFY(!sweep_key->isEnabled());
    QVERIFY(sweep_key->text().isEmpty());
    // A competing wallet update must discard the transient key and preview
    // immediately, rather than relying on broadcast-time stale detection.
    walletModel.updateTransaction();
    QVERIFY(sweep_key->isEnabled());
    QVERIFY(sweep_key->text().isEmpty());
    QVERIFY(!sweep_broadcast->isEnabled());
    QVERIFY(sweep_result->text().contains("fresh preview"));
    QTRY_VERIFY_WITH_TIMEOUT(sweep_key->isEnabled(), 5000);
    QVERIFY(!sweep_result->text().contains("not-a-private-key"));
    QVERIFY(!sweep_broadcast->isEnabled());
    sweep_dialog.reject();
    QVERIFY(sweep_key->text().isEmpty());

    const auto settings_contain_sentinel = [&] {
        QSettings settings;
        for (const QString& key : settings.allKeys()) {
            if (key.contains(sweep_sentinel) || settings.value(key).toString().contains(sweep_sentinel)) return true;
        }
        return false;
    };
    const auto widgets_contain_sentinel = [&] {
        for (QWidget* widget : qApp->allWidgets()) {
            if (auto* line_edit = qobject_cast<QLineEdit*>(widget); line_edit && line_edit->text().contains(sweep_sentinel)) return true;
            if (auto* label = qobject_cast<QLabel*>(widget); label && label->text().contains(sweep_sentinel)) return true;
            if (auto* button = qobject_cast<QAbstractButton*>(widget); button && button->text().contains(sweep_sentinel)) return true;
        }
        return false;
    };
    QVERIFY(!settings_contain_sentinel());
    QVERIFY(!widgets_contain_sentinel());
    for (int i = 0; i < 3; ++i) {
        auto dialog = std::make_unique<SweepDialog>(&walletModel);
        QLineEdit* key = dialog->findChild<QLineEdit*>("sweepPrivateKey");
        QVERIFY(key);
        key->setText(sweep_sentinel);
        dialog->reject();
        QVERIFY(key->text().isEmpty());
        dialog.reset();
        QVERIFY(!widgets_contain_sentinel());
    }
    QVERIFY(!settings_contain_sentinel());

    const auto verify_layout = [&](const QFont& font, const QSize& size) {
        SweepDialog dialog(&walletModel);
        dialog.setFont(font);
        dialog.resize(size);
        dialog.show();
        QTRY_VERIFY(dialog.isVisible());
        QVERIFY(dialog.sizeHint().width() <= size.width());
        QVERIFY(dialog.sizeHint().height() <= size.height());
        for (QWidget* widget : std::initializer_list<QWidget*>{dialog.findChild<QLineEdit*>("sweepPrivateKey"), dialog.findChild<QLineEdit*>("sweepDestination"),
                                                               dialog.findChild<QCheckBox*>("sweepReveal"), dialog.findChild<QPushButton*>("sweepPreview"),
                                                               dialog.findChild<QPushButton*>("sweepBroadcast"), dialog.findChild<QLabel*>("sweepResult")}) {
            QVERIFY(widget && widget->isVisible());
            QVERIFY(dialog.contentsRect().contains(widget->mapTo(&dialog, widget->rect().center())));
        }
        dialog.reject();
    };
    verify_layout(sweep_dialog.font(), QSize{1024, 700});
    QFont large_font{sweep_dialog.font()};
    large_font.setPointSize(24);
    verify_layout(large_font, QSize{1600, 1000});

    // Update walletModel cached balance which will trigger an update for the 'labelBalance' QLabel.
    walletModel.pollBalanceChanged();
    // Check balance in send dialog
    CompareBalance(walletModel, walletModel.wallet().getBalance(), sendCoinsDialog.findChild<QLabel*>("labelBalance"));

    // Check 'UseAvailableBalance' functionality
    VerifyUseAvailableBalance(sendCoinsDialog, walletModel);

    // The coin control view presents wallet-owned facts and keeps filtering a
    // display-only operation: it must not alter the selected outpoints.
    CoinControlDialog coin_control_dialog(*sendCoinsDialog.getCoinControl(), &walletModel, platformStyle.get());
    QTreeWidget* coin_tree = coin_control_dialog.findChild<QTreeWidget*>("treeWidget");
    QLineEdit* coin_filter = coin_control_dialog.findChild<QLineEdit*>("lineEditFilter");
    QLabel* available = coin_control_dialog.findChild<QLabel*>("labelAvailable");
    QLabel* fee_rate = coin_control_dialog.findChild<QLabel*>("labelCoinControlFeeRate");
    QLabel* rbf_summary = coin_control_dialog.findChild<QLabel*>("labelCoinControlRbf");
    QLabel* selection_notice = coin_control_dialog.findChild<QLabel*>("labelSelectionNotice");
    QVERIFY(coin_tree && coin_filter && available && fee_rate && rbf_summary && selection_notice);
    QCOMPARE(coin_tree->headerItem()->text(6), "Status");
    QCOMPARE(coin_tree->headerItem()->text(7), "Effective value");
    QCOMPARE(coin_tree->headerItem()->text(8), "Input bytes");
    QVERIFY(available->text().contains("eligible"));
    QVERIFY(rbf_summary->text().contains("Replace-By-Fee"));
    QCOMPARE(coin_filter->accessibleName(), "Filter coin selection");
    QVERIFY(coin_tree->accessibleDescription().contains("Status column"));
    const auto selected_before_filter = sendCoinsDialog.getCoinControl()->ListSelected();
    coin_filter->setText("not-a-wallet-coin");
    qApp->processEvents();
    for (int row = 0; row < coin_tree->topLevelItemCount(); ++row) QVERIFY(coin_tree->topLevelItem(row)->isHidden());
    QVERIFY(sendCoinsDialog.getCoinControl()->ListSelected() == selected_before_filter);
    coin_filter->clear();
    QVERIFY(coin_tree->focusPolicy() != Qt::NoFocus);
    QVERIFY(QMetaObject::invokeMethod(&coin_control_dialog, "walletChanged"));
    QVERIFY(!sendCoinsDialog.getCoinControl()->HasSelected());
    QVERIFY(!selection_notice->isHidden());
    QVERIFY(selection_notice->text().contains("cleared"));
    coin_control_dialog.reject();

    // Effective value must use the active coin-control fee rate, not fall back
    // to face value. Check the formatted value, its numeric sorting key, and
    // the item sorting implementation independently of localized labels.
    const CFeeRate effective_fee_rate{1000};
    const auto coin_groups = walletModel.wallet().listCoins();
    QVERIFY(!coin_groups.empty());
    const COutPoint effective_outpoint{std::get<0>(coin_groups.begin()->second.front())};
    const auto effective_coins = walletModel.wallet().getCoins({effective_outpoint}, effective_fee_rate);
    QCOMPARE(effective_coins.size(), 1U);
    const auto& effective_coin = effective_coins.front();
    QVERIFY(effective_coin.effective_value);
    QVERIFY(*effective_coin.effective_value < effective_coin.txout.nValue);

    wallet::CCoinControl effective_control;
    effective_control.m_feerate = effective_fee_rate;
    CoinControlDialog effective_dialog(effective_control, &walletModel, platformStyle.get());
    QRadioButton* list_mode = effective_dialog.findChild<QRadioButton*>("radioListMode");
    QTreeWidget* effective_tree = effective_dialog.findChild<QTreeWidget*>("treeWidget");
    QVERIFY(list_mode && effective_tree);
    list_mode->setChecked(true);
    qApp->processEvents();
    QTreeWidgetItem* effective_item{nullptr};
    for (int row = 0; row < effective_tree->topLevelItemCount(); ++row) {
        QTreeWidgetItem* item = effective_tree->topLevelItem(row);
        if (item->data(3, Qt::UserRole).toString() == QString::fromStdString(effective_outpoint.hash.GetHex()) &&
            item->data(3, Qt::UserRole + 1).toUInt() == effective_outpoint.n) {
            effective_item = item;
            break;
        }
    }
    QVERIFY(effective_item);
    QCOMPARE(effective_item->text(7), BitcoinUnits::format(walletModel.getOptionsModel()->getDisplayUnit(), *effective_coin.effective_value));
    QCOMPARE(effective_item->data(7, Qt::UserRole).toLongLong(), qlonglong{*effective_coin.effective_value});

    QTreeWidget effective_sort_tree;
    effective_sort_tree.setColumnCount(9);
    auto* larger_effective = new CCoinControlWidgetItem(&effective_sort_tree);
    larger_effective->setText(7, "0.00000001");
    larger_effective->setData(7, Qt::UserRole, qlonglong{*effective_coin.effective_value + 1});
    auto* smaller_effective = new CCoinControlWidgetItem(&effective_sort_tree);
    smaller_effective->setText(7, "9.99999999");
    smaller_effective->setData(7, Qt::UserRole, qlonglong{*effective_coin.effective_value});
    effective_sort_tree.sortItems(7, Qt::AscendingOrder);
    QCOMPARE(effective_sort_tree.topLevelItem(0)->data(7, Qt::UserRole).toLongLong(), qlonglong{*effective_coin.effective_value});
    effective_dialog.reject();

    // Send two transactions, and verify they are added to transaction list.
    TransactionTableModel* transactionTableModel = walletModel.getTransactionTableModel();
    QCOMPARE(transactionTableModel->rowCount({}), 105);
    QCheckBox* rbf_checkbox = sendCoinsDialog.findChild<QCheckBox*>("optInRBF");
    QVERIFY(rbf_checkbox);
    QCOMPARE(rbf_checkbox->text(), "Enable Replace-By-Fee");
    QVERIFY(rbf_checkbox->focusPolicy() != Qt::NoFocus);
    QVERIFY(!rbf_checkbox->isChecked());

    QString no_rbf_confirmation;
    uint256 txid1 = SendCoins(*wallet.get(), sendCoinsDialog, PKHash(), 5 * COIN, /*rbf=*/false, QMessageBox::Yes, &no_rbf_confirmation);
    QString rbf_confirmation;
    uint256 txid2 = SendCoins(*wallet.get(), sendCoinsDialog, PKHash(), 10 * COIN, /*rbf=*/true, QMessageBox::Yes, &rbf_confirmation);
    {
        LOCK(wallet->cs_wallet);
        QVERIFY(!SignalsOptInRBF(*wallet->mapWallet.at(txid1).tx));
        QVERIFY(SignalsOptInRBF(*wallet->mapWallet.at(txid2).tx));
    }
    QVERIFY(no_rbf_confirmation.contains("Not signalling Replace-By-Fee, BIP-125."));
    QVERIFY(rbf_confirmation.contains("signals Replace-By-Fee (BIP-125)"));
    QVERIFY(rbf_confirmation.contains("Effective coin control"));
    QVERIFY(rbf_confirmation.contains("Inputs:"));
    QVERIFY(rbf_confirmation.contains("Fee rate:"));
    QVERIFY(rbf_confirmation.contains("Change:"));

    wallet->m_signal_rbf = true;
    SendCoinsDialog default_rbf_dialog(platformStyle.get());
    default_rbf_dialog.setModel(&walletModel);
    QVERIFY(default_rbf_dialog.findChild<QCheckBox*>("optInRBF")->isChecked());
    QString default_on_override_confirmation;
    uint256 txid3 = SendCoins(*wallet.get(), default_rbf_dialog, PKHash(), COIN, /*rbf=*/false, QMessageBox::Yes, &default_on_override_confirmation);
    {
        LOCK(wallet->cs_wallet);
        QVERIFY(!SignalsOptInRBF(*wallet->mapWallet.at(txid3).tx));
    }
    QVERIFY(default_on_override_confirmation.contains("Not signalling Replace-By-Fee, BIP-125."));
    // Transaction table model updates on a QueuedConnection, so process events to ensure it's updated.
    qApp->processEvents();
    QCOMPARE(transactionTableModel->rowCount({}), 108);
    QVERIFY(FindTx(*transactionTableModel, txid1).isValid());
    QVERIFY(FindTx(*transactionTableModel, txid2).isValid());
    QVERIFY(FindTx(*transactionTableModel, txid3).isValid());

    // Call bumpfee. Test disabled, canceled, enabled, then failing cases.
    BumpFee(transactionView, txid1, /*expectDisabled=*/true, /*expectError=*/"not BIP 125 replaceable", /*cancel=*/false);
    BumpFee(transactionView, txid2, /*expectDisabled=*/false, /*expectError=*/{}, /*cancel=*/true);
    BumpFee(transactionView, txid2, /*expectDisabled=*/false, /*expectError=*/{}, /*cancel=*/false);
    BumpFee(transactionView, txid2, /*expectDisabled=*/true, /*expectError=*/"already bumped", /*cancel=*/false);
    BumpFee(transactionView, txid3, /*expectDisabled=*/true, /*expectError=*/"not BIP 125 replaceable", /*cancel=*/false);

    // Check current balance on OverviewPage
    OverviewPage overviewPage(platformStyle.get());
    overviewPage.setWalletModel(&walletModel);
    walletModel.pollBalanceChanged(); // Manual balance polling update
    CompareBalance(walletModel, walletModel.wallet().getBalance(), overviewPage.findChild<QLabel*>("labelBalance"));

    // Check Request Payment button
    ReceiveCoinsDialog receiveCoinsDialog(platformStyle.get());
    receiveCoinsDialog.setModel(&walletModel);
    RecentRequestsTableModel* requestTableModel = walletModel.getRecentRequestsTableModel();

    // Label input
    QLineEdit* labelInput = receiveCoinsDialog.findChild<QLineEdit*>("reqLabel");
    labelInput->setText("TEST_LABEL_1");

    // Amount input
    BitcoinAmountField* amountInput = receiveCoinsDialog.findChild<BitcoinAmountField*>("reqAmount");
    amountInput->setValue(1);

    // Message input
    QLineEdit* messageInput = receiveCoinsDialog.findChild<QLineEdit*>("reqMessage");
    messageInput->setText("TEST_MESSAGE_1");
    int initialRowCount = requestTableModel->rowCount({});
    QPushButton* requestPaymentButton = receiveCoinsDialog.findChild<QPushButton*>("receiveButton");
    requestPaymentButton->click();
    QString address;
    for (QWidget* widget : QApplication::topLevelWidgets()) {
        if (widget->inherits("ReceiveRequestDialog")) {
            ReceiveRequestDialog* receiveRequestDialog = qobject_cast<ReceiveRequestDialog*>(widget);
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("payment_header")->text(), QString("Payment information"));
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("uri_tag")->text(), QString("URI:"));
            QString uri = receiveRequestDialog->QObject::findChild<QLabel*>("uri_content")->text();
            QCOMPARE(uri.count("bitcoin:"), 2);
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("address_tag")->text(), QString("Address:"));
            QVERIFY(address.isEmpty());
            address = receiveRequestDialog->QObject::findChild<QLabel*>("address_content")->text();
            QVERIFY(!address.isEmpty());

            QCOMPARE(uri.count("amount=0.00000001"), 2);
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("amount_tag")->text(), QString("Amount:"));
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("amount_content")->text(), QString::fromStdString("0.00000001 " + CURRENCY_UNIT));

            QCOMPARE(uri.count("label=TEST_LABEL_1"), 2);
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("label_tag")->text(), QString("Label:"));
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("label_content")->text(), QString("TEST_LABEL_1"));

            QCOMPARE(uri.count("message=TEST_MESSAGE_1"), 2);
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("message_tag")->text(), QString("Message:"));
            QCOMPARE(receiveRequestDialog->QObject::findChild<QLabel*>("message_content")->text(), QString("TEST_MESSAGE_1"));
        }
    }

    // Clear button
    QPushButton* clearButton = receiveCoinsDialog.findChild<QPushButton*>("clearButton");
    clearButton->click();
    QCOMPARE(labelInput->text(), QString(""));
    QCOMPARE(amountInput->value(), CAmount(0));
    QCOMPARE(messageInput->text(), QString(""));

    // Check addition to history
    int currentRowCount = requestTableModel->rowCount({});
    QCOMPARE(currentRowCount, initialRowCount+1);

    // Check addition to wallet
    std::vector<std::string> requests = walletModel.wallet().getAddressReceiveRequests();
    QCOMPARE(requests.size(), size_t{1});
    RecentRequestEntry entry;
    DataStream{MakeUCharSpan(requests[0])} >> entry;
    QCOMPARE(entry.nVersion, int{1});
    QCOMPARE(entry.id, int64_t{1});
    QVERIFY(entry.date.isValid());
    QCOMPARE(entry.recipient.address, address);
    QCOMPARE(entry.recipient.label, QString{"TEST_LABEL_1"});
    QCOMPARE(entry.recipient.amount, CAmount{1});
    QCOMPARE(entry.recipient.message, QString{"TEST_MESSAGE_1"});
    QCOMPARE(entry.recipient.sPaymentRequest, std::string{});
    QCOMPARE(entry.recipient.authenticatedMerchant, QString{});

    // Check Remove button
    QTableView* table = receiveCoinsDialog.findChild<QTableView*>("recentRequestsView");
    table->selectRow(currentRowCount-1);
    QPushButton* removeRequestButton = receiveCoinsDialog.findChild<QPushButton*>("removeRequestButton");
    removeRequestButton->click();
    QCOMPARE(requestTableModel->rowCount({}), currentRowCount-1);

    // Check removal from wallet
    QCOMPARE(walletModel.wallet().getAddressReceiveRequests().size(), size_t{0});
}

void TestGUIWatchOnly(interfaces::Node& node, TestChain100Setup& test)
{
    const std::shared_ptr<CWallet>& wallet = SetupLegacyWatchOnlyWallet(node, test);

    // Create widgets and init models
    std::unique_ptr<const PlatformStyle> platformStyle(PlatformStyle::instantiate("other"));
    MiniGUI mini_gui(node, platformStyle.get());
    mini_gui.initModelForWallet(node, wallet, platformStyle.get());
    WalletModel& walletModel = *mini_gui.walletModel;
    SendCoinsDialog& sendCoinsDialog = mini_gui.sendCoinsDialog;

    // Update walletModel cached balance which will trigger an update for the 'labelBalance' QLabel.
    walletModel.pollBalanceChanged();
    // Check balance in send dialog
    CompareBalance(walletModel, walletModel.wallet().getBalances().watch_only_balance,
                   sendCoinsDialog.findChild<QLabel*>("labelBalance"));

    // Set change address
    sendCoinsDialog.getCoinControl()->destChange = GetDestinationForKey(test.coinbaseKey.GetPubKey(), OutputType::LEGACY);

    // Time to reject "save" PSBT dialog ('SendCoins' locks the main thread until the dialog receives the event).
    QTimer timer;
    timer.setInterval(500);
    QObject::connect(&timer, &QTimer::timeout, [&](){
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->inherits("QMessageBox") && widget->objectName().compare("psbt_copied_message") == 0) {
                QMessageBox* dialog = qobject_cast<QMessageBox*>(widget);
                QAbstractButton* button = dialog->button(QMessageBox::Discard);
                button->setEnabled(true);
                button->click();
                timer.stop();
                break;
            }
        }
    });
    timer.start(500);

    // Send tx and verify PSBT copied to the clipboard.
    QString psbt_confirmation;
    SendCoins(*wallet.get(), sendCoinsDialog, PKHash(), 5 * COIN, /*rbf=*/false, QMessageBox::Save, &psbt_confirmation);
    QVERIFY(psbt_confirmation.contains("Effective coin control"));
    QVERIFY(psbt_confirmation.contains("Inputs:"));
    const std::string& psbt_string = QApplication::clipboard()->text().toStdString();
    QVERIFY(!psbt_string.empty());

    // Decode psbt
    std::optional<std::vector<unsigned char>> decoded_psbt = DecodeBase64(psbt_string);
    QVERIFY(decoded_psbt);
    PartiallySignedTransaction psbt;
    std::string err;
    QVERIFY(DecodeRawPSBT(psbt, MakeByteSpan(*decoded_psbt), err));
    QVERIFY(!SignalsOptInRBF(CTransaction{*psbt.tx}));

    SendCoinsDialog rbf_psbt_dialog(platformStyle.get());
    rbf_psbt_dialog.setModel(&walletModel);
    rbf_psbt_dialog.getCoinControl()->destChange = GetDestinationForKey(test.coinbaseKey.GetPubKey(), OutputType::LEGACY);
    QTimer rbf_timer;
    rbf_timer.setInterval(500);
    QObject::connect(&rbf_timer, &QTimer::timeout, [&](){
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            if (widget->inherits("QMessageBox") && widget->objectName().compare("psbt_copied_message") == 0) {
                QMessageBox* dialog = qobject_cast<QMessageBox*>(widget);
                QAbstractButton* button = dialog->button(QMessageBox::Discard);
                button->setEnabled(true);
                button->click();
                rbf_timer.stop();
                break;
            }
        }
    });
    rbf_timer.start(500);

    SendCoins(*wallet.get(), rbf_psbt_dialog, PKHash(), 5 * COIN, /*rbf=*/true, QMessageBox::Save);
    decoded_psbt = DecodeBase64(QApplication::clipboard()->text().toStdString());
    QVERIFY(decoded_psbt);
    psbt = {};
    err.clear();
    QVERIFY(DecodeRawPSBT(psbt, MakeByteSpan(*decoded_psbt), err));
    QVERIFY(SignalsOptInRBF(CTransaction{*psbt.tx}));
}

void TestGUI(interfaces::Node& node)
{
    // Set up wallet and chain with 105 blocks (5 mature blocks for spending).
    TestChain100Setup test;
    for (int i = 0; i < 5; ++i) {
        test.CreateAndProcessBlock({}, GetScriptForRawPubKey(test.coinbaseKey.GetPubKey()));
    }
    auto wallet_loader = interfaces::MakeWalletLoader(*test.m_node.chain, *Assert(test.m_node.args));
    test.m_node.wallet_loader = wallet_loader.get();
    node.setContext(&test.m_node);

    // "Full" GUI tests, use descriptor wallet
    const std::shared_ptr<CWallet>& desc_wallet = SetupDescriptorsWallet(node, test);
    TestGUI(node, desc_wallet);

    // Legacy watch-only wallet test
    // Verify PSBT creation.
    TestGUIWatchOnly(node, test);
}

} // namespace

void WalletTests::walletTests()
{
#ifdef Q_OS_MACOS
    if (QApplication::platformName() == "minimal") {
        // Disable for mac on "minimal" platform to avoid crashes inside the Qt
        // framework when it tries to look up unimplemented cocoa functions,
        // and fails to handle returned nulls
        // (https://bugreports.qt.io/browse/QTBUG-49686).
        qWarning() << "Skipping WalletTests on mac build with 'minimal' platform set due to Qt bugs. To run AppTests, invoke "
                      "with 'QT_QPA_PLATFORM=cocoa test_bitcoin-qt' on mac, or else use a linux or windows build.";
        return;
    }
#endif
    TestGUI(m_node);
}
