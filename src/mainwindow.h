#pragma once

#include "batterydevice.h"

#include <QWidget>

class BatteryGauge;
class BatteryReader;
class QLabel;
class QPushButton;
class QThread;
class QTimer;

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(bool demoMode = false, QWidget *parent = nullptr);
    ~MainWindow() override;

    void showDemoReading();

signals:
    void refreshRequested();

private slots:
    void refresh();
    void applyReading(const BatteryReading &reading);
    void setAutoRefreshEnabled(bool enabled);
    void showHelp();

private:
    void buildUi();
    void setBusy(bool busy);
    void updateStatusPill(const QString &text, const QString &state);
    void updateReadingUi(const BatteryReading &reading);
    void updateErrorUi(const BatteryReading &reading);
    QWidget *makeStatCard(const QString &caption, QLabel **valueLabel);

    bool m_demoMode = false;
    bool m_busy = false;

    BatteryGauge *m_gauge = nullptr;
    QLabel *m_subtitle = nullptr;
    QLabel *m_statusPill = nullptr;
    QLabel *m_voltageValue = nullptr;
    QLabel *m_modeValue = nullptr;
    QLabel *m_updatedValue = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_autoButton = nullptr;
    QPushButton *m_helpButton = nullptr;
    QTimer *m_autoRefreshTimer = nullptr;

    QThread *m_workerThread = nullptr;
    BatteryReader *m_reader = nullptr;
};
