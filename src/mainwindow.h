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
    void dpiWriteRequested(int dpi);

private slots:
    void refresh();
    void applyReading(const BatteryReading &reading);
    void setAutoRefreshEnabled(bool enabled);
    void editDpi();
    void handleDpiWriteFinished(bool success, int dpi, const QString &error);
    void showHelp();

private:
    void buildUi();
    void setBusy(bool busy);
    void updateDpiButton();
    void updateStatusPill(const QString &text, const QString &state);
    void updateReadingUi(const BatteryReading &reading);
    void updateErrorUi(const BatteryReading &reading);
    QWidget *makeStatCard(const QString &caption, QLabel **valueLabel);

    bool m_demoMode = false;
    bool m_busy = false;
    bool m_dpiAvailable = false;
    bool m_dpiApplying = false;
    int m_currentDpi = 0;
    int m_dpiStages = 0;
    int m_dpiStageIndex = 0;
    QString m_dpiError;

    BatteryGauge *m_gauge = nullptr;
    QLabel *m_subtitle = nullptr;
    QLabel *m_statusPill = nullptr;
    QLabel *m_voltageValue = nullptr;
    QLabel *m_dpiValue = nullptr;
    QLabel *m_updatedValue = nullptr;
    QLabel *m_errorLabel = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_autoButton = nullptr;
    QPushButton *m_dpiButton = nullptr;
    QPushButton *m_helpButton = nullptr;
    QTimer *m_autoRefreshTimer = nullptr;

    QThread *m_workerThread = nullptr;
    BatteryReader *m_reader = nullptr;
};
