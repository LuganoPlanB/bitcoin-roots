// Copyright (c) 2026 The Bitcoin Roots developers
// Distributed under the MIT software license, see the accompanying file COPYING or http://www.opensource.org/licenses/MIT.

#include <qt/sweepdialog.h>

#include <key_io.h>
#include <qt/bitcoinunits.h>
#include <qt/optionsmodel.h>
#include <qt/walletmodel.h>
#include <util/translation.h>
#include <uint256.h>

#include <QDialogButtonBox>
#include <QCheckBox>
#include <QFormLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {
class SecureLineEdit final : public QLineEdit
{
public:
    using QLineEdit::QLineEdit;

protected:
    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->matches(QKeySequence::Copy) || event->matches(QKeySequence::Cut)) {
            event->accept();
            return;
        }
        QLineEdit::keyPressEvent(event);
    }
};
} // namespace

SweepDialog::SweepDialog(WalletModel* wallet_model, QWidget* parent)
    : QDialog(parent), m_wallet_model(wallet_model)
{
    setWindowTitle(tr("Sweep Private Key"));
    setModal(true);
    auto* layout = new QVBoxLayout(this);
    auto* warning = new QLabel(tr("A private key is a bearer secret. It is used only for this sweep and is never saved in this wallet."), this);
    warning->setWordWrap(true);
    layout->addWidget(warning);
    auto* form = new QFormLayout;
    m_private_key = new SecureLineEdit(this);
    m_private_key->setObjectName("sweepPrivateKey");
    m_private_key->setEchoMode(QLineEdit::Password);
    m_private_key->setContextMenuPolicy(Qt::NoContextMenu);
    m_private_key->setDragEnabled(false);
    m_private_key->setAccessibleName(tr("Private key"));
    m_private_key->setAccessibleDescription(tr("Masked private key. Copy, cut, and context-menu actions are disabled."));
    auto* reveal = new QCheckBox(tr("Show private key"), this);
    reveal->setObjectName("sweepReveal");
    reveal->setAccessibleName(tr("Show private key"));
    reveal->setAccessibleDescription(tr("Temporarily reveal the private key on screen. Copy and cut remain disabled."));
    m_destination = new QLineEdit(this);
    m_destination->setObjectName("sweepDestination");
    m_destination->setAccessibleName(tr("Destination address"));
    m_destination->setAccessibleDescription(tr("A destination address controlled by the selected wallet."));
    form->addRow(tr("Private key:"), m_private_key);
    form->addRow(QString{}, reveal);
    form->addRow(tr("Destination:"), m_destination);
    layout->addLayout(form);
    m_result = new QLabel(this);
    m_result->setObjectName("sweepResult");
    m_result->setWordWrap(true);
    m_result->setAccessibleName(tr("Sweep preview"));
    m_result->setAccessibleDescription(tr("Non-secret sweep preview, status, and error details."));
    layout->addWidget(m_result);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_preview = buttons->addButton(tr("Preview"), QDialogButtonBox::ActionRole);
    m_broadcast = buttons->addButton(tr("Broadcast sweep"), QDialogButtonBox::AcceptRole);
    m_preview->setObjectName("sweepPreview");
    m_broadcast->setObjectName("sweepBroadcast");
    m_broadcast->setEnabled(false);
    m_preview->setAccessibleName(tr("Preview sweep"));
    m_broadcast->setAccessibleName(tr("Broadcast sweep"));
    m_preview->setAccessibleDescription(tr("Scan for eligible coins and create a transaction preview without broadcasting."));
    m_broadcast->setAccessibleDescription(tr("Broadcast the currently displayed sweep transaction. This cannot be undone."));
    layout->addWidget(buttons);
    setTabOrder(m_private_key, reveal);
    setTabOrder(reveal, m_destination);
    setTabOrder(m_destination, m_preview);
    setTabOrder(m_preview, m_broadcast);
    connect(m_preview, &QPushButton::clicked, this, &SweepDialog::preview);
    connect(m_broadcast, &QPushButton::clicked, this, &SweepDialog::broadcast);
    connect(buttons, &QDialogButtonBox::rejected, this, &SweepDialog::reject);
    connect(m_private_key, &QLineEdit::textChanged, this, &SweepDialog::invalidate);
    connect(m_destination, &QLineEdit::textChanged, this, &SweepDialog::invalidate);
    connect(reveal, &QCheckBox::toggled, this, [this](bool checked) {
        m_private_key->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
    });
    connect(this, &QDialog::finished, this, &SweepDialog::clearSensitive);
    connect(m_wallet_model, &WalletModel::unload, this, &SweepDialog::clearSensitive);
    connect(m_wallet_model, &WalletModel::sweepRequestInvalidated, this, [this](uint64_t) {
        m_request_generation = 0;
        const QSignalBlocker block{m_private_key};
        m_private_key->clear();
        m_private_key_cache.clear();
        m_preview_txid.clear();
        setBusy(false);
        m_preview_current = false;
        m_broadcast->setEnabled(false);
        m_result->setText(tr("Sweep state changed. Enter the private key and create a fresh preview."));
    });
    connect(m_wallet_model, &WalletModel::sweepFinished, this, &SweepDialog::handleSweepFinished);
}

SweepDialog::~SweepDialog() { clearSensitive(); }

void SweepDialog::invalidate()
{
    if (m_request_generation != 0) m_wallet_model->invalidateSweepRequests();
    m_request_generation = 0;
    m_private_key_cache.clear();
    m_preview_txid.clear();
    m_preview_current = false;
    m_broadcast->setEnabled(false);
    m_result->setText(tr("A new preview is required before broadcasting."));
}

void SweepDialog::setBusy(bool busy)
{
    m_busy = busy;
    m_private_key->setEnabled(!busy);
    m_destination->setEnabled(!busy);
    m_preview->setEnabled(!busy);
    if (busy) m_broadcast->setEnabled(false);
}

void SweepDialog::setResult(const QString& text, bool can_broadcast)
{
    m_result->setText(text);
    m_preview_current = can_broadcast;
    m_broadcast->setEnabled(can_broadcast);
}

void SweepDialog::preview()
{
    const CTxDestination destination{DecodeDestination(m_destination->text().toStdString())};
    if (!IsValidDestination(destination)) { setResult(tr("Enter a valid destination address."), false); return; }
    m_private_key_cache = SecureString{m_private_key->text().toStdString()};
    if (m_private_key_cache.empty()) { setResult(tr("Enter a private key."), false); return; }
    const QSignalBlocker block{m_private_key};
    m_private_key->clear();
    setBusy(true);
    m_result->setText(tr("Scanning for eligible coins and preparing a preview…"));
    m_request_generation = m_wallet_model->requestSweep(m_private_key_cache, destination, false);
}

void SweepDialog::broadcast()
{
    if (!m_preview_current || m_private_key_cache.empty()) return;
    const auto answer = QMessageBox::warning(this, tr("Broadcast sweep"),
        tr("Broadcast this exact preview now? This action is irreversible and may fail if the chain state or fee conditions changed."),
        QMessageBox::Cancel | QMessageBox::Yes, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) return;
    const CTxDestination destination{DecodeDestination(m_destination->text().toStdString())};
    const auto expected_txid{uint256::FromHex(m_preview_txid.toStdString())};
    if (!expected_txid) { setResult(tr("The preview is no longer valid. Create a fresh preview."), false); return; }
    setBusy(true);
    m_preview_current = false;
    m_result->setText(tr("Rechecking the preview and broadcasting the sweep…"));
    m_request_generation = m_wallet_model->requestSweep(m_private_key_cache, destination, true, expected_txid);
}

void SweepDialog::handleSweepFinished(uint64_t generation, bool broadcast, bool success, const QString& error,
                                      qint64 amount, qint64 fee, quint64 inputs, qint64 vsize, bool rbf, const QString& txid)
{
    if (generation != m_request_generation) return;
    m_request_generation = 0;
    setBusy(false);
    if (!success) {
        setResult(broadcast
                ? tr("Broadcast failed: %1. Create a fresh preview before trying again.").arg(error)
                : tr("Preview failed: %1").arg(error), false);
        m_private_key_cache.clear();
        m_preview_txid.clear();
        return;
    }
    if (broadcast) {
        setResult(tr("Sweep broadcast. Transaction %1 was submitted.").arg(txid), false);
        clearSensitive();
        return;
    }
    m_preview_txid = txid;
    const qint64 fee_rate{vsize > 0 ? (fee + vsize - 1) / vsize : 0};
    setResult(tr("Wallet: %1\nDestination: %2\n%3 input(s), total %4, fee %5, net received %6.\nFee rate: %7 sat/vB. Replace-By-Fee: %8. Review these facts before irreversible broadcast.")
                  .arg(m_wallet_model->getDisplayName(), m_destination->text())
                  .arg(inputs).arg(BitcoinUnits::formatWithUnit(m_wallet_model->getOptionsModel()->getDisplayUnit(), amount))
                  .arg(BitcoinUnits::formatWithUnit(m_wallet_model->getOptionsModel()->getDisplayUnit(), fee))
                  .arg(BitcoinUnits::formatWithUnit(m_wallet_model->getOptionsModel()->getDisplayUnit(), amount - fee))
                  .arg(fee_rate)
                  .arg(rbf ? tr("signaled") : tr("not signaled")), true);
}

void SweepDialog::clearSensitive()
{
    if (m_request_generation != 0) m_wallet_model->invalidateSweepRequests();
    m_request_generation = 0;
    const QSignalBlocker block{m_private_key};
    m_private_key->clear();
    m_private_key_cache.clear();
    m_preview_txid.clear();
    m_preview_current = false;
    setBusy(false);
    m_broadcast->setEnabled(false);
}
