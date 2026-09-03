#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTextBrowser;
class QTimer;

namespace ui {

class AiReportPage : public QWidget
{
    Q_OBJECT
public:
    explicit AiReportPage(QWidget *parent = nullptr);

    void startDemo();
    void setDemoDelayMsForTesting(int ms);
    void completeForTesting();

signals:
    void backRequested();

private:
    void finishDemo();
    void updateLoadingDots();

    QLabel *m_titleLabel = nullptr;
    QLabel *m_hintLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_backButton = nullptr;
    QPushButton *m_generateButton = nullptr;
    QTextBrowser *m_reportView = nullptr;
    QTimer *m_loadingTimer = nullptr;
    QTimer *m_demoTimer = nullptr;
    int m_demoDelayMs = 10000;
    int m_dotCount = 0;
};

} // namespace ui
