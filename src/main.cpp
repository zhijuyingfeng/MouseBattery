#include "batterydevice.h"
#include "mainwindow.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QPalette>
#include <QStyleFactory>
#include <QTimer>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("MouseBattery"));
    application.setApplicationDisplayName(QStringLiteral("鼠标电量"));
    application.setApplicationVersion(QStringLiteral("1.0.0"));
    application.setOrganizationName(QStringLiteral("MouseBattery"));
    application.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor(QStringLiteral("#0B1120")));
    palette.setColor(QPalette::WindowText, QColor(QStringLiteral("#E2E8F0")));
    palette.setColor(QPalette::Base, QColor(QStringLiteral("#0F172A")));
    palette.setColor(QPalette::AlternateBase, QColor(QStringLiteral("#1E293B")));
    palette.setColor(QPalette::Text, QColor(QStringLiteral("#E2E8F0")));
    palette.setColor(QPalette::Button, QColor(QStringLiteral("#1E293B")));
    palette.setColor(QPalette::ButtonText, QColor(QStringLiteral("#E2E8F0")));
    palette.setColor(QPalette::Highlight, QColor(QStringLiteral("#22D3EE")));
    palette.setColor(QPalette::HighlightedText, QColor(QStringLiteral("#05232C")));
    palette.setColor(QPalette::ToolTipBase, QColor(QStringLiteral("#1E293B")));
    palette.setColor(QPalette::ToolTipText, QColor(QStringLiteral("#E2E8F0")));
    palette.setColor(QPalette::PlaceholderText, QColor(QStringLiteral("#64748B")));
    application.setPalette(palette);

    const QStringList families = QFontDatabase::families();
    const QStringList preferredFonts = {
#ifdef Q_OS_WIN
        QStringLiteral("Microsoft YaHei UI"),
        QStringLiteral("Microsoft YaHei"),
        QStringLiteral("Segoe UI"),
#elif defined(Q_OS_MACOS)
        QStringLiteral("PingFang SC"),
        QStringLiteral("Hiragino Sans GB"),
        QStringLiteral("Heiti SC"),
#else
        QStringLiteral("Noto Sans CJK SC"),
        QStringLiteral("Noto Sans CJK HK"),
        QStringLiteral("Source Han Sans SC"),
        QStringLiteral("WenQuanYi Micro Hei"),
#endif
        QStringLiteral("DejaVu Sans"),
    };
    for (const QString &family : preferredFonts) {
        if (families.contains(family)) {
            QFont font = application.font();
            font.setFamily(family);
            font.setPointSizeF(10.0);
            application.setFont(font);
            break;
        }
    }

    qRegisterMetaType<BatteryReading>("BatteryReading");

    const QStringList arguments = QCoreApplication::arguments();
    const bool demoMode = arguments.contains(QStringLiteral("--demo"));

    QString screenshotPath;
    const int screenshotIndex = arguments.indexOf(QStringLiteral("--screenshot"));
    if (screenshotIndex >= 0 && screenshotIndex + 1 < arguments.size()) {
        screenshotPath = arguments.at(screenshotIndex + 1);
    }

    MainWindow window(demoMode);
    window.show();

    if (!screenshotPath.isEmpty()) {
        QDir().mkpath(QFileInfo(screenshotPath).absolutePath());
        QTimer::singleShot(1200, &application, [&window, screenshotPath, &application]() {
            const bool saved = window.grab().save(screenshotPath);
            if (!saved) {
                qWarning("Could not save screenshot to %s",
                         qPrintable(screenshotPath));
            }
            application.quit();
        });
    }

    return application.exec();
}
