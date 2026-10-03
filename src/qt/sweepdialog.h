// Copyright (c) 2026 The Bitcoin Roots developers
// Distributed under the MIT software license, see the accompanying file COPYING or http://www.opensource.org/licenses/MIT.

#ifndef BITCOIN_QT_SWEEPDIALOG_H
#define BITCOIN_QT_SWEEPDIALOG_H

#include <QDialog>
#include <support/allocators/secure.h>

#include <cstdint>

class QLabel;
class QLineEdit;
class QPushButton;
class WalletModel;

class SweepDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SweepDialog(WalletModel* wallet_model, QWidget* parent = nullptr);
    ~SweepDialog() override;

private Q_SLOTS:
    void preview();
    void broadcast();
    void invalidate();
    void clearSensitive();
    void handleSweepFinished(uint64_t generation, bool broadcast, bool success, const QString& error,
                             qint64 amount, qint64 fee, quint64 inputs, qint64 vsize, bool rbf, const QString& txid);

private:
    void setResult(const QString& text, bool can_broadcast);
    void setBusy(bool busy);
    WalletModel* m_wallet_model;
    QLineEdit* m_private_key;
    QLineEdit* m_destination;
    QLabel* m_result;
    QPushButton* m_preview;
    QPushButton* m_broadcast;
    bool m_busy{false};
    bool m_preview_current{false};
    //! Incremented for every user-visible invalidation. Async completions must
    //! compare their captured generation before updating this dialog.
    uint64_t m_request_generation{0};
    SecureString m_private_key_cache;
    QString m_preview_txid;
};

#endif // BITCOIN_QT_SWEEPDIALOG_H
