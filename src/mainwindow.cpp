#include "mainwindow.h"

#include "batterygauge.h"
#include "batteryreader.h"

#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLinearGradient>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QSizePolicy>
#include <QStyle>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QIcon createAppIcon()
{
    QPixmap pixmap(128, 128);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QLinearGradient background(0, 0, 128, 128);
    background.setColorAt(0.0, QColor(QStringLiteral("#0B1120")));
    background.setColorAt(1.0, QColor(QStringLiteral("#13263D")));
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawRoundedRect(QRectF(3, 3, 122, 122), 29, 29);

    QPen outline(QColor(QStringLiteral("#22D3EE")), 6.0, Qt::SolidLine,
                 Qt::RoundCap, Qt::RoundJoin);
    painter.setPen(outline);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(28, 42, 63, 47), 10, 10);
    painter.drawRoundedRect(QRectF(94, 56, 9, 19), 4, 4);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#34D399")));
    painter.drawRoundedRect(QRectF(37, 51, 31, 29), 5, 5);

    QPainterPath bolt;
    bolt.moveTo(72, 51);
    bolt.lineTo(61, 69);
    bolt.lineTo(70, 69);
    bolt.lineTo(66, 82);
    bolt.lineTo(80, 63);
    bolt.lineTo(71, 63);
    bolt.closeSubpath();
    painter.setBrush(QColor(QStringLiteral("#FDE68A")));
    painter.drawPath(bolt);

    return QIcon(pixmap);
}

class MouseGlyph final : public QWidget
{
public:
    explicit MouseGlyph(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(52, 52);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QLinearGradient body(8, 4, 42, 50);
        body.setColorAt(0.0, QColor(QStringLiteral("#1E3A5F")));
        body.setColorAt(1.0, QColor(QStringLiteral("#0F1B2D")));

        painter.setBrush(body);
        painter.setPen(QPen(QColor(56, 189, 248, 125), 1.5));
        painter.drawRoundedRect(QRectF(13, 4, 26, 43), 13, 13);

        painter.setPen(QPen(QColor(148, 163, 184, 80), 1.2));
        painter.drawLine(QPointF(13.5, 20.5), QPointF(38.5, 20.5));

        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#22D3EE")));
        painter.drawRoundedRect(QRectF(23.5, 10, 5, 9), 2.5, 2.5);

        painter.setBrush(QColor(52, 211, 153, 200));
        painter.drawEllipse(QPointF(26, 36), 2.4, 2.4);
    }
};

} // namespace

MainWindow::MainWindow(bool demoMode, QWidget *parent)
    : QWidget(parent)
    , m_demoMode(demoMode)
{
    qRegisterMetaType<BatteryReading>("BatteryReading");

    setObjectName(QStringLiteral("root"));
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle(QStringLiteral("鼠标电量"));
    setWindowIcon(createAppIcon());
    setMinimumSize(440, 650);
    resize(460, 690);

    buildUi();

    if (m_demoMode) {
        QTimer::singleShot(220, this, &MainWindow::showDemoReading);
        return;
    }

    m_workerThread = new QThread(this);
    m_reader = new BatteryReader;
    m_reader->moveToThread(m_workerThread);

    connect(this, &MainWindow::refreshRequested, m_reader, &BatteryReader::refresh,
            Qt::QueuedConnection);
    connect(this, &MainWindow::dpiWriteRequested, m_reader, &BatteryReader::applyDpi,
            Qt::QueuedConnection);
    connect(m_reader, &BatteryReader::readingReady, this, &MainWindow::applyReading,
            Qt::QueuedConnection);
    connect(m_reader, &BatteryReader::dpiWriteFinished, this,
            &MainWindow::handleDpiWriteFinished, Qt::QueuedConnection);

    m_workerThread->start();
    QTimer::singleShot(180, this, &MainWindow::refresh);

    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        move(available.center() - rect().center());
    }
}

MainWindow::~MainWindow()
{
    if (m_autoRefreshTimer != nullptr) {
        m_autoRefreshTimer->stop();
    }
    if (m_workerThread != nullptr) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
    delete m_reader;
    m_reader = nullptr;
}

void MainWindow::buildUi()
{
    setStyleSheet(QStringLiteral(R"(
        QWidget#root {
            background-color: #0B1120;
        }
        QLabel#title {
            color: #F8FAFC;
            font-size: 23px;
            font-weight: 700;
        }
        QLabel#subtitle {
            color: #94A3B8;
            font-size: 11px;
        }
        QLabel#statusPill {
            border-radius: 11px;
            padding: 6px 11px;
            font-size: 11px;
            font-weight: 600;
        }
        QLabel#statusPill[state="ok"] {
            color: #6EE7B7;
            background-color: rgba(52, 211, 153, 30);
        }
        QLabel#statusPill[state="busy"] {
            color: #67E8F9;
            background-color: rgba(34, 211, 238, 26);
        }
        QLabel#statusPill[state="error"] {
            color: #FCA5A5;
            background-color: rgba(248, 113, 113, 26);
        }
        QFrame#heroCard {
            background-color: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #14233B, stop:0.55 #0F1B2D, stop:1 #0B1626);
            border: 1px solid rgba(56, 189, 248, 38);
            border-radius: 26px;
        }
        QFrame#statCard {
            background-color: rgba(30, 41, 59, 165);
            border: 1px solid rgba(148, 163, 184, 28);
            border-radius: 15px;
        }
        QLabel#statCaption {
            color: #7C8CA5;
            font-size: 10px;
            font-weight: 500;
        }
        QLabel#statValue {
            color: #E2E8F0;
            font-size: 15px;
            font-weight: 600;
        }
        QPushButton#primaryButton {
            color: #05232C;
            background-color: #22D3EE;
            border: none;
            border-radius: 13px;
            padding: 10px 20px;
            font-size: 13px;
            font-weight: 700;
        }
        QPushButton#primaryButton:hover {
            background-color: #67E8F9;
        }
        QPushButton#primaryButton:pressed {
            background-color: #06B6D4;
        }
        QPushButton#primaryButton:disabled {
            color: rgba(5, 35, 44, 120);
            background-color: rgba(34, 211, 238, 75);
        }
        QPushButton#autoButton {
            color: #94A3B8;
            background-color: transparent;
            border: 1px solid rgba(148, 163, 184, 45);
            border-radius: 12px;
            padding: 8px 12px;
            font-size: 11px;
            text-align: left;
        }
        QPushButton#autoButton:hover {
            color: #CBD5E1;
            border-color: rgba(148, 163, 184, 90);
        }
        QPushButton#autoButton:checked {
            color: #67E8F9;
            background-color: rgba(34, 211, 238, 24);
            border-color: rgba(34, 211, 238, 70);
        }
        QPushButton#dpiButton {
            color: #A5F3FC;
            background-color: rgba(34, 211, 238, 18);
            border: 1px solid rgba(34, 211, 238, 55);
            border-radius: 12px;
            padding: 8px 12px;
            font-size: 11px;
        }
        QPushButton#dpiButton:hover {
            background-color: rgba(34, 211, 238, 34);
            border-color: rgba(34, 211, 238, 95);
        }
        QPushButton#dpiButton:disabled {
            color: #64748B;
            background-color: transparent;
            border-color: rgba(148, 163, 184, 35);
        }
        QPushButton#helpButton {
            color: #94A3B8;
            background: transparent;
            border: none;
            padding: 6px 8px;
            font-size: 11px;
        }
        QPushButton#helpButton:hover {
            color: #F8FAFC;
        }
        QLabel#errorLabel {
            color: #FCA5A5;
            background-color: rgba(248, 113, 113, 18);
            border: 1px solid rgba(248, 113, 113, 46);
            border-radius: 12px;
            padding: 10px 12px;
            font-size: 11px;
        }
        QLabel#footerLabel {
            color: #475569;
            font-size: 10px;
        }
        QToolTip {
            color: #E2E8F0;
            background-color: #1E293B;
            border: 1px solid #334155;
            padding: 5px;
        }
    )"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 24, 18);
    layout->setSpacing(14);

    auto *header = new QHBoxLayout;
    header->setSpacing(14);
    auto *glyph = new MouseGlyph(this);
    auto *titleColumn = new QVBoxLayout;
    titleColumn->setSpacing(3);

    auto *title = new QLabel(QStringLiteral("鼠标电量"), this);
    title->setObjectName(QStringLiteral("title"));
    m_subtitle = new QLabel(QStringLiteral("正在检测接收器…"), this);
    m_subtitle->setObjectName(QStringLiteral("subtitle"));
    m_subtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    titleColumn->addWidget(title);
    titleColumn->addWidget(m_subtitle);

    m_statusPill = new QLabel(QStringLiteral("检测中"), this);
    m_statusPill->setObjectName(QStringLiteral("statusPill"));
    m_statusPill->setProperty("state", QStringLiteral("busy"));

    header->addWidget(glyph, 0, Qt::AlignVCenter);
    header->addLayout(titleColumn, 1);
    header->addWidget(m_statusPill, 0, Qt::AlignTop);

    auto *hero = new QFrame(this);
    hero->setObjectName(QStringLiteral("heroCard"));
    auto *heroLayout = new QVBoxLayout(hero);
    heroLayout->setContentsMargins(12, 8, 12, 8);
    heroLayout->setSpacing(0);

    m_gauge = new BatteryGauge(hero);
    m_gauge->setMinimumHeight(250);
    m_gauge->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    heroLayout->addWidget(m_gauge, 1);

    auto *shadow = new QGraphicsDropShadowEffect(hero);
    shadow->setBlurRadius(38);
    shadow->setOffset(0, 11);
    shadow->setColor(QColor(0, 0, 0, 105));
    hero->setGraphicsEffect(shadow);

    auto *stats = new QHBoxLayout;
    stats->setSpacing(10);
    stats->addWidget(makeStatCard(QStringLiteral("电压"), &m_voltageValue), 1);
    stats->addWidget(makeStatCard(QStringLiteral("DPI"), &m_dpiValue), 1);
    stats->addWidget(makeStatCard(QStringLiteral("更新"), &m_updatedValue), 1);

    auto *controls = new QHBoxLayout;
    controls->setSpacing(8);

    m_autoButton = new QPushButton(QStringLiteral("自动刷新 30s"), this);
    m_autoButton->setObjectName(QStringLiteral("autoButton"));
    m_autoButton->setCheckable(true);
    m_autoButton->setChecked(true);
    m_autoButton->setCursor(Qt::PointingHandCursor);
    m_autoButton->setToolTip(QStringLiteral("每 30 秒自动读取一次电量"));

    m_dpiButton = new QPushButton(QStringLiteral("更改 DPI"), this);
    m_dpiButton->setObjectName(QStringLiteral("dpiButton"));
    m_dpiButton->setCursor(Qt::PointingHandCursor);
    m_dpiButton->setEnabled(false);

    m_helpButton = new QPushButton(QStringLiteral("帮助"), this);
    m_helpButton->setObjectName(QStringLiteral("helpButton"));
    m_helpButton->setCursor(Qt::PointingHandCursor);
    m_helpButton->hide();

    m_refreshButton = new QPushButton(QStringLiteral("立即刷新"), this);
    m_refreshButton->setObjectName(QStringLiteral("primaryButton"));
    m_refreshButton->setCursor(Qt::PointingHandCursor);
    m_refreshButton->setShortcut(QKeySequence(Qt::Key_F5));
    m_refreshButton->setToolTip(QStringLiteral("刷新电量 (F5)"));

    controls->addWidget(m_autoButton);
    controls->addStretch(1);
    controls->addWidget(m_dpiButton);
    controls->addWidget(m_helpButton);
    controls->addWidget(m_refreshButton);

    m_errorLabel = new QLabel(this);
    m_errorLabel->setObjectName(QStringLiteral("errorLabel"));
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_errorLabel->hide();

    auto *footer = new QLabel(
        QStringLiteral("Compx / ATK 2.4G 接收器 · HID 报告 0x08"), this);
    footer->setObjectName(QStringLiteral("footerLabel"));
    footer->setAlignment(Qt::AlignHCenter);

    layout->addLayout(header);
    layout->addWidget(hero, 1);
    layout->addLayout(stats);
    layout->addLayout(controls);
    layout->addWidget(m_errorLabel);
    layout->addWidget(footer);

    m_autoRefreshTimer = new QTimer(this);
    m_autoRefreshTimer->setInterval(30 * 1000);
    connect(m_autoRefreshTimer, &QTimer::timeout, this, &MainWindow::refresh);
    connect(m_autoButton, &QPushButton::toggled, this,
            &MainWindow::setAutoRefreshEnabled);
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::refresh);
    connect(m_dpiButton, &QPushButton::clicked, this, &MainWindow::editDpi);
    connect(m_helpButton, &QPushButton::clicked, this, &MainWindow::showHelp);

    updateDpiButton();
    m_autoRefreshTimer->start();
}

QWidget *MainWindow::makeStatCard(const QString &caption, QLabel **valueLabel)
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("statCard"));
    card->setMinimumHeight(68);

    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(13, 9, 13, 9);
    layout->setSpacing(2);

    auto *captionLabel = new QLabel(caption, card);
    captionLabel->setObjectName(QStringLiteral("statCaption"));

    auto *value = new QLabel(QStringLiteral("—"), card);
    value->setObjectName(QStringLiteral("statValue"));
    value->setTextInteractionFlags(Qt::TextSelectableByMouse);

    layout->addWidget(captionLabel);
    layout->addWidget(value);

    *valueLabel = value;
    return card;
}

void MainWindow::refresh()
{
    if (m_demoMode) {
        showDemoReading();
        return;
    }
    if (m_busy) {
        return;
    }

    setBusy(true);
    emit refreshRequested();
}

void MainWindow::setBusy(bool busy)
{
    m_busy = busy;
    m_refreshButton->setEnabled(!busy);
    m_refreshButton->setText(busy ? QStringLiteral("读取中…")
                                  : QStringLiteral("立即刷新"));
    m_gauge->setBusy(busy);
    updateDpiButton();
    if (busy) {
        updateStatusPill(QStringLiteral("读取中"), QStringLiteral("busy"));
    }
}

void MainWindow::updateDpiButton()
{
    if (m_dpiApplying) {
        m_dpiButton->setText(QStringLiteral("写入中…"));
        m_dpiButton->setEnabled(false);
        return;
    }

    if (!m_dpiAvailable) {
        m_dpiButton->setText(QStringLiteral("DPI 不可用"));
        m_dpiButton->setEnabled(false);
        m_dpiButton->setToolTip(m_dpiError.isEmpty()
                                    ? QStringLiteral("当前设备没有提供 DPI 读取接口。")
                                    : m_dpiError);
        return;
    }

    m_dpiButton->setText(QStringLiteral("更改 DPI"));
    m_dpiButton->setEnabled(!m_busy);
    m_dpiButton->setToolTip(
        QStringLiteral("当前第 %1/%2 档：%3 DPI")
            .arg(m_dpiStageIndex + 1)
            .arg(m_dpiStages)
            .arg(m_currentDpi));
}

void MainWindow::editDpi()
{
    if (!m_dpiAvailable || m_dpiApplying || m_busy) {
        return;
    }

    bool accepted = false;
    const int requested = QInputDialog::getInt(
        this,
        QStringLiteral("设置 DPI"),
        QStringLiteral("当前第 %1/%2 档，输入新的 DPI（50–42000）：")
            .arg(m_dpiStageIndex + 1)
            .arg(m_dpiStages),
        m_currentDpi,
        50,
        42000,
        10,
        &accepted);
    if (!accepted) {
        return;
    }

    if (m_demoMode) {
        m_currentDpi = requested;
        m_dpiValue->setText(QString::number(requested));
        updateDpiButton();
        return;
    }

    m_dpiApplying = true;
    updateDpiButton();
    emit dpiWriteRequested(requested);
}

void MainWindow::handleDpiWriteFinished(bool success, int dpi, const QString &error)
{
    m_dpiApplying = false;

    if (!success) {
        updateDpiButton();
        QMessageBox::warning(this, QStringLiteral("DPI 写入失败"),
                             error.isEmpty()
                                 ? QStringLiteral("设备没有确认 DPI 写入。")
                                 : error);
        return;
    }

    m_currentDpi = dpi;
    m_dpiValue->setText(QString::number(dpi));
    updateDpiButton();

    if (!error.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("DPI 已写入"), error);
    }

    refresh();
}

void MainWindow::applyReading(const BatteryReading &reading)
{
    setBusy(false);

    if (reading.available) {
        updateStatusPill(QStringLiteral("已连接"), QStringLiteral("ok"));
        m_subtitle->setText(reading.deviceName.isEmpty()
                                ? QStringLiteral("2.4G 无线鼠标")
                                : reading.deviceName);
        m_subtitle->setToolTip(reading.devicePath);
        m_gauge->setReading(reading.percentage, reading.charging, true);

        m_voltageValue->setText(
            QStringLiteral("%1 V").arg(reading.voltageMv / 1000.0, 0, 'f', 3));
        m_dpiAvailable = reading.dpiAvailable;
        m_currentDpi = reading.dpi;
        m_dpiStages = reading.dpiStages;
        m_dpiStageIndex = reading.dpiStageIndex;
        m_dpiError = reading.dpiError;
        if (m_dpiAvailable) {
            m_dpiValue->setText(QString::number(reading.dpi));
            m_dpiValue->setToolTip(
                QStringLiteral("第 %1/%2 档")
                    .arg(reading.dpiStageIndex + 1)
                    .arg(reading.dpiStages));
        } else {
            m_dpiValue->setText(QStringLiteral("—"));
            m_dpiValue->setToolTip(reading.dpiError);
        }
        m_updatedValue->setText(reading.updatedAt.toString(QStringLiteral("HH:mm:ss")));
        m_errorLabel->hide();
        m_helpButton->hide();
        updateDpiButton();
        return;
    }

    updateReadingUi(reading);
}

void MainWindow::updateReadingUi(const BatteryReading &reading)
{
    updateStatusPill(QStringLiteral("未连接"), QStringLiteral("error"));
    m_subtitle->setText(reading.deviceName.isEmpty()
                            ? QStringLiteral("等待兼容的 2.4G 接收器")
                            : reading.deviceName);
    m_subtitle->setToolTip(reading.devicePath);

    m_gauge->setReading(-1, false, false);
    m_voltageValue->setText(QStringLiteral("—"));
    m_dpiValue->setText(QStringLiteral("—"));
    m_updatedValue->setText(QStringLiteral("—"));
    m_dpiAvailable = false;
    m_currentDpi = 0;
    m_dpiStages = 0;
    m_dpiStageIndex = 0;
    m_dpiError = reading.dpiError;
    updateDpiButton();

    updateErrorUi(reading);
}

void MainWindow::updateErrorUi(const BatteryReading &reading)
{
    m_errorLabel->setText(reading.error.isEmpty()
                              ? QStringLiteral("暂时无法读取鼠标电量。")
                              : reading.error);
    m_errorLabel->show();
    m_helpButton->show();
}

void MainWindow::updateStatusPill(const QString &text, const QString &state)
{
    m_statusPill->setText(text);
    m_statusPill->setProperty("state", state);
    m_statusPill->style()->unpolish(m_statusPill);
    m_statusPill->style()->polish(m_statusPill);
    m_statusPill->update();
}

void MainWindow::setAutoRefreshEnabled(bool enabled)
{
    if (enabled) {
        m_autoButton->setText(QStringLiteral("自动刷新 30s"));
        m_autoRefreshTimer->start();
    } else {
        m_autoButton->setText(QStringLiteral("自动刷新关闭"));
        m_autoRefreshTimer->stop();
    }
}

void MainWindow::showHelp()
{
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("读取帮助"));
    box.setIcon(QMessageBox::Information);
    box.setTextFormat(Qt::RichText);

#ifdef Q_OS_MACOS
    box.setText(QStringLiteral(
        "<b>如果一直显示“未连接”或“没有权限”</b><br><br>"
        "1. 在“系统设置 → 隐私与安全性 → 输入监控”中添加并勾选 "
        "MouseBattery.app。<br>"
        "2. 完全退出程序，再重新打开。<br>"
        "3. 确认 2.4G 接收器已经插好，并关闭可能独占设备的厂商驱动。<br><br>"
        "修改权限后必须重启应用，macOS 才会重新读取授权状态。"));
    box.setDetailedText(QStringLiteral(
        "If the app is not listed in Input Monitoring:\n"
        "1. Quit MouseBattery completely.\n"
        "2. Open System Settings > Privacy & Security > Input Monitoring.\n"
        "3. Click +, select MouseBattery.app and enable it.\n"
        "4. Reopen the app.\n\n"
        "An unsigned local build may need: right-click MouseBattery.app > Open."));
#elif defined(Q_OS_WIN)
    box.setText(QStringLiteral(
        "<b>如果一直显示“未连接”</b><br><br>"
        "1. 在“设备管理器 → 人体学输入设备”中确认接收器存在。<br>"
        "2. 关闭厂商驱动、网页版驱动或可能独占 HID 的软件。<br>"
        "3. 用普通用户运行即可，不需要管理员权限。<br>"
        "4. 确认 2.4G 接收器已经插好，然后点击“立即刷新”。"));
    box.setDetailedText(QStringLiteral(
        "Check the USB device:\n"
        "Get-PnpDevice -PresentOnly | Where-Object InstanceId -match 'VID_373B'\n\n"
        "If Qt DLLs are missing, deploy them with windeployqt."));
#else
    box.setText(QStringLiteral(
        "<b>如果一直显示“未连接”或“没有权限”</b><br><br>"
        "1. 确认 2.4G 接收器已经插好，可用 <code>lsusb | grep -i 373b</code> 检查。<br>"
        "2. 在项目目录运行 <code>sudo ./scripts/install-udev-rule.sh</code> 安装权限规则。<br>"
        "3. 重新插拔接收器，然后点击“立即刷新”。<br><br>"
        "接收器正常时，设备路径通常形如 <code>/dev/hidraw2</code>。"));
    box.setDetailedText(QStringLiteral(
        "udev rule:\n"
        "SUBSYSTEM==\"hidraw\", ATTRS{idVendor}==\"373b\", TAG+=\"uaccess\"\n\n"
        "Manual commands:\n"
        "sudo install -m 644 udev/70-compx-mouse-battery.rules "
        "/etc/udev/rules.d/70-compx-mouse-battery.rules\n"
        "sudo udevadm control --reload-rules\n"
        "sudo udevadm trigger --subsystem-match=hidraw\n"
        "Then unplug and reconnect the receiver."));
#endif

    box.exec();
}

void MainWindow::showDemoReading()
{
    BatteryReading reading;
    reading.available = true;
    reading.percentage = 78;
    reading.charging = true;
    reading.voltageMv = 4052;
    reading.mode = QStringLiteral("2.4G 无线");
    reading.deviceName = QStringLiteral("Compx Wireless mouse 8k NANO dongle-L");
    reading.devicePath = QStringLiteral("/dev/hidraw2");
    reading.dpiAvailable = true;
    reading.dpi = 800;
    reading.dpiStages = 4;
    reading.dpiStageIndex = 1;
    reading.updatedAt = QDateTime::currentDateTime();
    applyReading(reading);
}
