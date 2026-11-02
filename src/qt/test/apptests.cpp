// Copyright (c) 2018-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qt/test/apptests.h>

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <chainparams.h>
#include <key.h>
#include <logging.h>
#include <netmessagemaker.h>
#include <qt/bitcoin.h>
#include <qt/bitcoingui.h>
#include <qt/networkstyle.h>
#include <qt/qrimagewidget.h>
#include <QClipboard>
#include <QTemporaryDir>
#include <qt/rpcconsole.h>
#include <qt/peertablemodel.h>
#include <QLabel>
#include <QFontInfo>
#include <QTableView>
#include <QSortFilterProxyModel>
#include <test/util/setup_common.h>
#include <validation.h>
#include <interfaces/node.h>
#include <util/sock.h>
#include <univalue.h>
#include <tinyformat.h>
#include <QDebug>
#include <QDir>
#include <QTabWidget>
#include <QSplitter>
#include <QScrollArea>
#include <QScrollBar>
#include <QItemSelectionModel>
#include <QScreen>
#include <future>
#include <array>
#include <thread>

#include <QAction>
#include <QLineEdit>
#include <QRegularExpression>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QString>
#include <QTest>
#include <QTextEdit>
#include <QtGlobal>
#include <QtTest/QtTestWidgets>
#include <QtTest/QtTestGui>

namespace {
//! Regex find a string group inside of the console output
QString FindInConsole(const QString& output, const QString& pattern)
{
    const QRegularExpression re(pattern);
    return re.match(output).captured(1);
}

//! Call getblockchaininfo RPC and check first field of JSON output.
void TestRpcCommand(RPCConsole* console)
{
    QTextEdit* messagesWidget = console->findChild<QTextEdit*>("messagesWidget");
    QLineEdit* lineEdit = console->findChild<QLineEdit*>("lineEdit");
    QSignalSpy mw_spy(messagesWidget, &QTextEdit::textChanged);
    QVERIFY(mw_spy.isValid());
    QTest::keyClicks(lineEdit, "getblockchaininfo");
    QTest::keyClick(lineEdit, Qt::Key_Return);
    QVERIFY(mw_spy.wait(1000));
    QCOMPARE(mw_spy.count(), 4);
    const QString output = messagesWidget->toPlainText();
    const QString pattern = QStringLiteral("\"chain\": \"(\\w+)\"");
    QCOMPARE(FindInConsole(output, pattern), QString("regtest"));
}
void TestPeerRefreshState(RPCConsole* console)
{
    auto* peer_widget = console->findChild<QTableView*>("peerWidget");
    QVERIFY(peer_widget);
    auto* peer_proxy = qobject_cast<QSortFilterProxyModel*>(peer_widget->model());
    QVERIFY(peer_proxy);
    auto* peer_model = qobject_cast<PeerTableModel*>(peer_proxy->sourceModel());
    QVERIFY(peer_model);
    QCOMPARE(peer_model->rowCount(), 0);

    QSignalSpy data_changed_spy(peer_model, &QAbstractItemModel::dataChanged);
    QVERIFY(data_changed_spy.isValid());
    peer_model->refresh();
    QCOMPARE(data_changed_spy.count(), 0);

    const auto set_label = [console](const char* name) {
        QLabel* label = console->findChild<QLabel*>(name);
        if (label) label->setText(QStringLiteral("stale"));
        return label;
    };
    QLabel* time_offset = set_label("timeoffset");
    QLabel* services = set_label("peerServices");
    QLabel* sync_height = set_label("peerSyncHeight");
    QLabel* common_height = set_label("peerCommonHeight");
    QLabel* starting_height = set_label("peerHeight");
    QLabel* ping_wait = set_label("peerPingWait");
    QLabel* addr_relay = set_label("peerAddrRelayEnabled");
    QLabel* addr_processed = set_label("peerAddrProcessed");
    QLabel* addr_rate_limited = set_label("peerAddrRateLimited");
    QLabel* relay_txs = set_label("peerRelayTxes");

    QVERIFY(time_offset);
    QVERIFY(services);
    QVERIFY(sync_height);
    QVERIFY(common_height);
    QVERIFY(starting_height);
    QVERIFY(ping_wait);
    QVERIFY(addr_relay);
    QVERIFY(addr_processed);
    QVERIFY(addr_rate_limited);
    QVERIFY(relay_txs);
    QVERIFY(QMetaObject::invokeMethod(console, "resetPeerStateStats"));
    QCOMPARE(time_offset->text(), QStringLiteral("N/A"));
    QCOMPARE(services->text(), QStringLiteral("N/A"));
    QCOMPARE(sync_height->text(), QStringLiteral("Unknown"));
    QCOMPARE(common_height->text(), QStringLiteral("Unknown"));
    QCOMPARE(starting_height->text(), QStringLiteral("Unknown"));
    QCOMPARE(ping_wait->text(), QStringLiteral("N/A"));
    QCOMPARE(addr_relay->text(), QStringLiteral("N/A"));
    QCOMPARE(addr_processed->text(), QStringLiteral("N/A"));
    QCOMPARE(addr_rate_limited->text(), QStringLiteral("N/A"));
    QCOMPARE(relay_txs->text(), QStringLiteral("N/A"));
}

// Exercise the actual VERSION -> PeerManager -> node interface -> table ->
// detail path. Distinct advertised heights must survive selection and refresh;
// a busy state lock must clear the selected peer's previous value.
void TestPeerStartingHeight(RPCConsole* console, interfaces::Node& node)
{
    auto* table{console->findChild<QTableView*>("peerWidget")};
    auto* height{console->findChild<QLabel*>("peerHeight")};
    QVERIFY(table && height);
    auto* proxy{qobject_cast<QSortFilterProxyModel*>(table->model())};
    QVERIFY(proxy);
    auto* model{qobject_cast<PeerTableModel*>(proxy->sourceModel())};
    QVERIFY(model);
    model->stopAutoRefresh();
    QCOMPARE(model->rowCount(), 0);

    std::vector<std::unique_ptr<Sock>> remotes;
    const auto ready = [&] {
        model->refresh();
        if (model->rowCount() != static_cast<int>(remotes.size())) return false;
        for (int row = 0; row < model->rowCount(); ++row) {
            const auto* stats{model->index(row, 0).data(PeerTableModel::StatsRole).value<CNodeCombinedStats*>()};
            if (stats->nodeStats.nVersion != PROTOCOL_VERSION || !stats->fNodeStateStatsAvailable) return false;
        }
        return true;
    };
    const std::array<int32_t, 3> advertised{845123, 0, -1};
    for (const int32_t starting_height : advertised) {
        const auto handle{static_cast<SOCKET>(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))};
        QVERIFY(handle != INVALID_SOCKET);
        Sock listener{handle};
        QVERIFY(listener.SetNonBlocking());
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        QVERIFY(listener.Bind(reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        QVERIFY(listener.Listen(1) == 0);
        socklen_t length{sizeof(address)};
        QVERIFY(listener.GetSockName(reinterpret_cast<sockaddr*>(&address), &length) == 0);
        UniValue params{UniValue::VARR};
        params.push_back(strprintf("127.0.0.1:%u", ntohs(address.sin_port)));
        params.push_back("onetry");
        params.push_back(false); // Deliberately use the native V1 transport.
        node.executeRpc("addnode", params, "");
        std::unique_ptr<Sock> remote;
        QTRY_VERIFY_WITH_TIMEOUT(remote || (remote = listener.Accept(nullptr, nullptr)), 10000);
        QVERIFY(remote->SetNonBlocking());
        V1Transport transport{NodeId{0}};
        auto version{NetMsg::Make(NetMsgType::VERSION, PROTOCOL_VERSION,
                                 uint64_t{NODE_NETWORK | NODE_WITNESS}, int64_t{},
                                 uint64_t{}, CNetAddr::V1(CService{}),
                                 uint64_t{}, CNetAddr::V1(CService{}), uint64_t{1},
                                 std::string{"/QtStartingHeightTest/"}, starting_height, true)};
        std::array messages{std::move(version), NetMsg::Make(NetMsgType::VERACK)};
        for (auto& message : messages) {
            QVERIFY(transport.SetMessageToSend(message));
            while (true) {
                const auto& [bytes, more, type]{transport.GetBytesToSend(false)};
                if (bytes.empty()) break;
                QCOMPARE(remote->Send(bytes.data(), bytes.size(), MSG_NOSIGNAL), static_cast<ssize_t>(bytes.size()));
                transport.MarkBytesSent(bytes.size());
            }
        }
        remotes.push_back(std::move(remote));
        QTRY_VERIFY_WITH_TIMEOUT(ready(), 10000);
    }
    auto* tabs{console->findChild<QTabWidget*>("tabWidget")};
    QVERIFY(tabs);
    tabs->setCurrentIndex(3);
    table->sortByColumn(PeerTableModel::NetNodeId, Qt::AscendingOrder);
    for (int row = 0; row < static_cast<int>(advertised.size()); ++row) {
        table->selectRow(row);
        const auto* stats{table->selectionModel()->selectedRows().first().data(PeerTableModel::StatsRole).value<CNodeCombinedStats*>()};
        QVERIFY(stats->fNodeStateStatsAvailable);
        QCOMPARE(stats->nodeStateStats.m_starting_height, advertised[row]);
        QCOMPARE(height->text(), advertised[row] >= 0 ? QString::number(advertised[row]) : QStringLiteral("Unknown"));
    }
    table->selectRow(0);
    const QPersistentModelIndex selected{table->selectionModel()->selectedRows().first()};
    QTRY_VERIFY_WITH_TIMEOUT(ready(), 10000);
    QCOMPARE(selected.data(PeerTableModel::StatsRole).value<CNodeCombinedStats*>()->nodeStateStats.m_starting_height, advertised[0]);
    QCOMPARE(height->text(), QStringLiteral("845123"));
    table->sortByColumn(PeerTableModel::NetNodeId, Qt::DescendingOrder);
    QTRY_VERIFY_WITH_TIMEOUT(ready(), 10000);
    QCOMPARE(table->selectionModel()->selectedRows().first().data(), selected.data());
    QCOMPARE(height->text(), QStringLiteral("845123"));
    const auto capture_dir{qEnvironmentVariable("ROOTS_PEER_CAPTURE_DIR")};
    if (!capture_dir.isEmpty()) {
        const auto size{qEnvironmentVariable("ROOTS_PEER_CAPTURE_SIZE", "1280x800").split('x')};
        QCOMPARE(size.size(), 2);
        console->resize(size[0].toInt(), size[1].toInt());
        console->show();
        console->findChild<QSplitter*>("splitter")->setSizes({console->width() / 4, 3 * console->width() / 4});
        QTest::qWait(100);
        console->findChild<QScrollArea*>("scrollArea")->ensureWidgetVisible(height);
        QVERIFY(QDir().mkpath(capture_dir));
        QVERIFY(console->grab().save(capture_dir + "/starting-height.png"));
    }

    std::promise<void> locked, release;
    auto released{release.get_future()};
    std::thread blocker{[&] {
        LOCK(cs_main);
        locked.set_value();
        released.wait_for(10s);
    }};
    const bool lock_acquired{locked.get_future().wait_for(5s) == std::future_status::ready};
    model->refresh();
    const bool unavailable{!selected.data(PeerTableModel::StatsRole).value<CNodeCombinedStats*>()->fNodeStateStatsAvailable};
    const QString unavailable_height{height->text()};
    release.set_value();
    blocker.join();
    QVERIFY(lock_acquired);
    QVERIFY(unavailable);
    QCOMPARE(unavailable_height, QStringLiteral("Unknown"));
    QTRY_VERIFY_WITH_TIMEOUT((model->refresh(), height->text() == QStringLiteral("845123")), 10000);
    QVERIFY(QMetaObject::invokeMethod(console, "resetPeerStateStats"));
    QCOMPARE(height->text(), QStringLiteral("Unknown"));
    QTRY_VERIFY_WITH_TIMEOUT((model->refresh(), height->text() == QStringLiteral("845123")), 10000);

    // Closing every fixture connection removes the selection and detail pane.
    remotes.clear();
    QTRY_VERIFY_WITH_TIMEOUT((model->refresh(), model->rowCount() == 0), 10000);
    QVERIFY(table->selectionModel()->selectedRows().isEmpty());
    QVERIFY(console->findChild<QWidget*>("peersTabRightPanel")->isHidden());
    qInfo() << "Starting Block regression: VERSION sentinel, zero, unknown, selection, current-row refresh, unavailable, reset and disconnect passed";
}

// Optional runtime evidence profile. Peers are real regtest nodes supplied by
// the caller; unavailable state is obtained through the production try-lock.
void TestLivePeerView(RPCConsole* console, interfaces::Node& node)
{
    const auto endpoints{qEnvironmentVariable("ROOTS_PEER_ENDPOINTS").split(',', Qt::SkipEmptyParts)};
    if (endpoints.isEmpty()) return;
    QVERIFY(endpoints.size() >= 2);
    auto* table = console->findChild<QTableView*>("peerWidget");
    auto* tabs = console->findChild<QTabWidget*>("tabWidget");
    QVERIFY(table && tabs);
    auto* proxy = qobject_cast<QSortFilterProxyModel*>(table->model());
    QVERIFY(proxy);
    auto* model = qobject_cast<PeerTableModel*>(proxy->sourceModel());
    QVERIFY(model);
    model->stopAutoRefresh();
    for (const auto& endpoint : endpoints) {
        UniValue params{UniValue::VARR};
        params.push_back(endpoint.toStdString());
        params.push_back("onetry");
        node.executeRpc("addnode", params, "");
    }
    QTRY_VERIFY_WITH_TIMEOUT((model->refresh(), model->rowCount() == endpoints.size()), 10000);
    const auto ready = [&] {
        model->refresh();
        if (model->rowCount() != endpoints.size()) return false;
        for (int row = 0; row < model->rowCount(); ++row) {
            const auto* stats{model->index(row, 0).data(PeerTableModel::StatsRole).value<CNodeCombinedStats*>()};
            if (stats->nodeStats.nVersion == 0 || !stats->fNodeStateStatsAvailable) return false;
        }
        return true;
    };
    QTRY_VERIFY_WITH_TIMEOUT(ready(), 10000);
    const auto size{qEnvironmentVariable("ROOTS_PEER_CAPTURE_SIZE", "1280x800").split('x')};
    QCOMPARE(size.size(), 2);
    console->resize(size[0].toInt(), size[1].toInt());
    tabs->setCurrentIndex(3); // Native Peers tab.
    console->show();
    QTest::qWait(100);
    QCOMPARE(console->size(), QSize(size[0].toInt(), size[1].toInt()));
    qInfo() << "Live peer capture:" << console->size() << "font points" << table->font().pointSizeF()
            << "font family" << QFontInfo{table->font()}.family()
            << "platform" << QGuiApplication::platformName() << "Qt" << qVersion()
            << "device pixel ratio" << console->devicePixelRatioF()
            << "logical DPI" << console->screen()->logicalDotsPerInch()
            << "physical DPI" << console->screen()->physicalDotsPerInch();
    const auto capture_dir{qEnvironmentVariable("ROOTS_PEER_CAPTURE_DIR")};
    const auto capture = [&](const QString& name) {
        if (capture_dir.isEmpty()) return true;
        return QDir().mkpath(capture_dir) && console->grab().save(capture_dir + '/' + name + ".png");
    };
    table->clearSelection();
    QVERIFY(capture("table"));
    table->selectRow(0);
    const auto selected_id{table->selectionModel()->selectedRows().first().data().toLongLong()};
    table->sortByColumn(PeerTableModel::Received, Qt::DescendingOrder);
    model->refresh();
    QCOMPARE(table->selectionModel()->selectedRows().size(), 1);
    QCOMPARE(table->selectionModel()->selectedRows().first().data().toLongLong(), selected_id);
    QVERIFY(console->findChild<QLabel*>("peerSubversion")->text() != QStringLiteral("N/A"));
    // Exercise the native resizable splitter so unavailable values are legible
    // at 150% font scale as well as in the full-width table capture.
    console->findChild<QSplitter*>("splitter")->setSizes({console->width() / 4, 3 * console->width() / 4});
    QTest::qWait(100); // Let the shown detail pane receive its layout geometry.
    QVERIFY(console->findChild<QWidget*>("peersTabRightPanel")->isVisible());
    QVERIFY(capture("detail"));
    auto* detail_scroll{console->findChild<QScrollArea*>("scrollArea")};
    detail_scroll->horizontalScrollBar()->setValue(detail_scroll->horizontalScrollBar()->maximum());
    QVERIFY(capture("detail-right"));
    detail_scroll->horizontalScrollBar()->setValue(0);

    std::promise<void> locked, release;
    auto released = release.get_future();
    std::thread blocker{[&] {
        LOCK(cs_main);
        locked.set_value();
        released.wait_for(10s);
    }};
    const bool lock_acquired{locked.get_future().wait_for(5s) == std::future_status::ready};
    model->refresh();
    const auto previous_row{table->selectionModel()->selectedRows().first().row()};
    table->selectRow(previous_row == 0 ? 1 : 0);
    const bool unavailable{!table->selectionModel()->selectedRows().first().data(PeerTableModel::StatsRole).value<CNodeCombinedStats*>()->fNodeStateStatsAvailable};
    const bool cleared{console->findChild<QLabel*>("peerServices")->text() == QStringLiteral("N/A") &&
        console->findChild<QLabel*>("peerSyncHeight")->text() == QStringLiteral("Unknown") &&
        console->findChild<QLabel*>("peerHeight")->text() == QStringLiteral("Unknown") &&
        console->findChild<QLabel*>("timeoffset")->text() == QStringLiteral("N/A")};
    const bool captured{capture("unavailable")};
    auto* scroll = console->findChild<QScrollArea*>("scrollArea");
    scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    const bool captured_bottom{capture("unavailable-bottom")};
    scroll->verticalScrollBar()->setValue(0);
    release.set_value();
    blocker.join();
    QVERIFY(lock_acquired);
    QVERIFY(unavailable);
    QVERIFY(cleared);
    QVERIFY(captured);
    QVERIFY(captured_bottom);
    QTRY_VERIFY_WITH_TIMEOUT((model->refresh(), console->findChild<QLabel*>("peerServices")->text() != QStringLiteral("N/A")), 10000);
    // Removing the selected peer must not leave its detail pane visible.
    UniValue params{UniValue::VARR};
    params.push_back("");
    params.push_back(table->selectionModel()->selectedRows().first().data().toLongLong());
    node.executeRpc("disconnectnode", params, "");
    QTRY_VERIFY_WITH_TIMEOUT((model->refresh(), model->rowCount() == endpoints.size() - 1), 10000);
    QVERIFY(table->selectionModel()->selectedRows().isEmpty());
}
void TestQrQuietZone()
{
#if defined(ENABLE_WALLET) && defined(USE_QRCODE)
    QRImageWidget qr;
    QVERIFY(qr.setQR(QStringLiteral("bitcoin:bc1qz0j5w6w8t8d5alw5lp4n4t3g6d7n7ld5cqqqqq")));
    const QImage image{qr.exportImage()};
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path{directory.filePath("qr.png")};
    QVERIFY(image.save(path, "PNG"));
    QCOMPARE(QImage{path}, image);
    qr.copyImage();
    QCOMPARE(QApplication::clipboard()->image(), image);
    QCOMPARE(image.width(), QR_IMAGE_SIZE + 2 * QR_IMAGE_MARGIN);
    QCOMPARE(image.height(), QR_IMAGE_SIZE + 2 * QR_IMAGE_MARGIN);
    // The first matrix pixel remains white only when the four-module quiet zone
    // survives the scale-to-display path.
    QCOMPARE(image.pixelColor(QR_IMAGE_MARGIN, QR_IMAGE_MARGIN), QColor{Qt::white});
#endif
}

void TestRootsResources()
{
    const QPixmap icon{QStringLiteral(":/icons/bitcoin")};
    QVERIFY(!icon.isNull());
    const QSize expected_icon_size{256, 256};
    QCOMPARE(icon.size(), expected_icon_size);
    const QPixmap splash{QStringLiteral(":/icons/splash")};
    QVERIFY(!splash.isNull());
    const QSize expected_size{960, 960};
    QCOMPARE(splash.size(), expected_size);
}
} // namespace

//! Entry point for BitcoinApplication tests.
void AppTests::appTests()
{
    const QFont original_font{m_app.font()};
    const double font_scale{qEnvironmentVariable("ROOTS_PEER_FONT_SCALE", "1.0").toDouble()};
    QFont font{original_font};
    font.setPointSizeF(original_font.pointSizeF() * font_scale);
    m_app.setFont(font);
    qRegisterMetaType<interfaces::BlockAndHeaderTipInfo>("interfaces::BlockAndHeaderTipInfo");
    m_app.parameterSetup();
    QVERIFY(m_app.createOptionsModel(/*resetSettings=*/true));
    QScopedPointer<const NetworkStyle> style(NetworkStyle::instantiate(Params().GetChainType()));
    m_app.setupPlatformStyle();
    m_app.createWindow(style.data());
    connect(&m_app, &BitcoinApplication::windowShown, this, &AppTests::guiTests);
    expectCallback("guiTests");
    m_app.baseInitialize();
    m_app.requestInitialize();
    m_app.exec();
    m_app.requestShutdown();
    m_app.exec();

    // Reset global state to avoid interfering with later tests.
    LogInstance().DisconnectTestLogger();
    m_app.setFont(original_font);
}

//! Entry point for BitcoinGUI tests.
void AppTests::guiTests(BitcoinGUI* window)
{
    HandleCallback callback{"guiTests", *this};
    connect(window, &BitcoinGUI::consoleShown, this, &AppTests::consoleTests);
    expectCallback("consoleTests");
    QAction* action = window->findChild<QAction*>("openRPCConsoleAction");
    action->activate(QAction::Trigger);
}

//! Entry point for RPCConsole tests.
void AppTests::consoleTests(RPCConsole* console)
{
    HandleCallback callback{"consoleTests", *this};
    TestQrQuietZone();
    TestRootsResources();
    TestRpcCommand(console);
    TestPeerRefreshState(console);
    TestPeerStartingHeight(console, m_app.node());
    TestLivePeerView(console, m_app.node());
}

//! Destructor to shut down after the last expected callback completes.
AppTests::HandleCallback::~HandleCallback()
{
    auto& callbacks = m_app_tests.m_callbacks;
    auto it = callbacks.find(m_callback);
    assert(it != callbacks.end());
    callbacks.erase(it);
    if (callbacks.empty()) {
        m_app_tests.m_app.exit(0);
    }
}
