// Copyright (c) 2011-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <qt/splashscreen.h>

#include <clientversion.h>
#include <common/system.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <qt/guiutil.h>
#include <qt/networkstyle.h>
#include <qt/walletmodel.h>
#include <util/translation.h>

#include <functional>

#include <Qt>
#include <QtGlobal>
#include <QApplication>
#include <QCloseEvent>
#include <QFontInfo>
#include <QPainter>
#include <QScreen>

namespace {
const QColor SPLASH_TEXT_COLOR{0x03, 0x0b, 0x20};
const QColor SPLASH_MUTED_TEXT_COLOR{0x4d, 0x55, 0x66};

const QString& SplashFontFamily()
{
    static const QString family = [] {
        const QStringList preferred_families{
            QStringLiteral("Inter"),
            QStringLiteral("Segoe UI"),
            QStringLiteral("Arial"),
        };
        for (const QString& preferred_family : preferred_families) {
            const QFontInfo resolved_font{QFont{preferred_family}};
            if (resolved_font.family().compare(preferred_family, Qt::CaseInsensitive) == 0) return preferred_family;
        }
        return QApplication::font().family();
    }();
    return family;
}

QFont SplashFont(qreal point_size, QFont::Weight weight = QFont::Normal)
{
    QFont font{SplashFontFamily()};
    font.setStyleHint(QFont::SansSerif);
    font.setPointSizeF(point_size);
    font.setWeight(weight);
    return font;
}
} // namespace

SplashScreen::SplashScreen(const NetworkStyle* networkStyle)
    : QWidget()
{
    const qreal device_pixel_ratio{
        static_cast<QGuiApplication*>(QCoreApplication::instance())->devicePixelRatio()};
    const QString title_text{CLIENT_NAME};
    const QString version_text{QString("Version %1").arg(QString::fromStdString(FormatFullVersion()))};
    const QString copyright_text{QString::fromUtf8(CopyrightHolders(strprintf("\xc2\xA9 %u-%u ", 2009, COPYRIGHT_YEAR)).c_str())};
    const QString& title_add_text{networkStyle->getTitleAddText()};

    const QSize splash_size{qRound(480 * device_pixel_ratio), qRound(320 * device_pixel_ratio)};
    pixmap = QPixmap{splash_size};
    pixmap.setDevicePixelRatio(device_pixel_ratio);

    QPainter pixPaint(&pixmap);
    pixPaint.fillRect(QRect{QPoint{}, QSize{480, 320}}, Qt::white);
    pixPaint.setPen(SPLASH_TEXT_COLOR);

    constexpr int icon_size{172};
    const QRect icon_rect{QPoint{24, 55}, QSize{icon_size, icon_size}};
    QPixmap icon{":/icons/splash"};
    icon = icon.scaledToWidth(qRound(icon_size * device_pixel_ratio), Qt::SmoothTransformation);
    pixPaint.drawPixmap(icon_rect, icon);

    const QStringList title_parts{title_text.split(' ', Qt::SkipEmptyParts)};
    const QString bitcoin_title{title_parts.value(0, title_text)};
    const QString roots_title{title_parts.size() > 1 ? title_parts.mid(1).join(' ') : QString{}};
    constexpr int text_left{218};
    constexpr int text_width{236};
    auto fit_font = [](QFont font, const QString& text, int max_width) {
        const int width{GUIUtil::TextWidth(QFontMetrics{font}, text)};
        if (width > max_width) font.setPointSizeF(font.pointSizeF() * max_width / width);
        return font;
    };

    pixPaint.setFont(fit_font(SplashFont(30, QFont::Medium), bitcoin_title, text_width));
    pixPaint.drawText(text_left, 105, bitcoin_title);
    pixPaint.setFont(fit_font(SplashFont(43, QFont::DemiBold), roots_title, text_width));
    pixPaint.drawText(text_left, 151, roots_title);
    pixPaint.setFont(fit_font(SplashFont(11, QFont::Medium), version_text, text_width));
    pixPaint.setPen(SPLASH_MUTED_TEXT_COLOR);
    pixPaint.drawText(text_left, 181, version_text);

    pixPaint.setFont(SplashFont(8));
    pixPaint.drawText(QRect{text_left, 210, text_width, 70}, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, copyright_text);

    if (!title_add_text.isEmpty()) {
        pixPaint.setFont(SplashFont(9, QFont::DemiBold));
        const int title_add_text_width{GUIUtil::TextWidth(pixPaint.fontMetrics(), title_add_text)};
        pixPaint.drawText(470 - title_add_text_width, 20, title_add_text);
    }

    pixPaint.end();

    // Set window title
    setWindowTitle(title_text + " " + title_add_text);

    // Resize window and move to center of desktop, disallow resizing
    QRect r{QPoint{}, QSize{qRound(pixmap.size().width() / device_pixel_ratio), qRound(pixmap.size().height() / device_pixel_ratio)}};
    resize(r.size());
    setFixedSize(r.size());
    move(QGuiApplication::primaryScreen()->geometry().center() - r.center());

    installEventFilter(this);

    GUIUtil::handleCloseWindowShortcut(this);
}

SplashScreen::~SplashScreen()
{
    if (m_node) unsubscribeFromCoreSignals();
}

void SplashScreen::setNode(interfaces::Node& node)
{
    assert(!m_node);
    m_node = &node;
    subscribeToCoreSignals();
    if (m_shutdown) m_node->startShutdown();
}

void SplashScreen::shutdown()
{
    m_shutdown = true;
    if (m_node) m_node->startShutdown();
}

bool SplashScreen::eventFilter(QObject * obj, QEvent * ev) {
    if (ev->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(ev);
        if (keyEvent->key() == Qt::Key_Q) {
            shutdown();
        }
    }
    return QObject::eventFilter(obj, ev);
}

static void InitMessage(SplashScreen *splash, const std::string &message)
{
    bool invoked = QMetaObject::invokeMethod(splash, "showMessage",
        Qt::QueuedConnection,
        Q_ARG(QString, QString::fromStdString(message)),
        Q_ARG(int, Qt::AlignBottom|Qt::AlignHCenter),
        Q_ARG(QColor, QColor(55,55,55)));
    assert(invoked);
}

static void ShowProgress(SplashScreen *splash, const std::string &title, int nProgress, bool resume_possible)
{
    InitMessage(splash, title + std::string("\n") +
            (resume_possible ? SplashScreen::tr("(press q to shutdown and continue later)").toStdString()
                                : SplashScreen::tr("press q to shutdown").toStdString()) +
            strprintf("\n%d", nProgress) + "%");
}

void SplashScreen::subscribeToCoreSignals()
{
    // Connect signals to client
    m_handler_init_message = m_node->handleInitMessage(std::bind(InitMessage, this, std::placeholders::_1));
    m_handler_show_progress = m_node->handleShowProgress(std::bind(ShowProgress, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    m_handler_init_wallet = m_node->handleInitWallet([this]() { handleLoadWallet(); });
}

void SplashScreen::handleLoadWallet()
{
#ifdef ENABLE_WALLET
    if (!WalletModel::isWalletEnabled()) return;
    m_handler_load_wallet = m_node->walletLoader().handleLoadWallet([this](std::unique_ptr<interfaces::Wallet> wallet) {
        m_connected_wallet_handlers.emplace_back(wallet->handleShowProgress(std::bind(ShowProgress, this, std::placeholders::_1, std::placeholders::_2, false)));
        m_connected_wallets.emplace_back(std::move(wallet));
    });
#endif
}

void SplashScreen::unsubscribeFromCoreSignals()
{
    // Disconnect signals from client
    m_handler_init_message->disconnect();
    m_handler_show_progress->disconnect();
    for (const auto& handler : m_connected_wallet_handlers) {
        handler->disconnect();
    }
    m_connected_wallet_handlers.clear();
    m_connected_wallets.clear();
}

void SplashScreen::showMessage(const QString &message, int alignment, const QColor &color)
{
    curMessage = message;
    curAlignment = alignment;
    curColor = color;
    update();
}

void SplashScreen::paintEvent(QPaintEvent *event)
{
    QPainter painter(this);
    painter.drawPixmap(0, 0, pixmap);
    QRect r = rect().adjusted(5, 5, -5, -5);
    painter.setPen(curColor);
    painter.drawText(r, curAlignment, curMessage);
}

void SplashScreen::closeEvent(QCloseEvent *event)
{
    shutdown(); // allows an "emergency" shutdown during startup
    event->ignore();
}
