#ifndef COUNTDOWNWINDOW_H
#define COUNTDOWNWINDOW_H

#endif // COUNTDOWNWINDOW_H

#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QTime>
#include <QCursor>
#include <QScreen>

#include "SDL_events.h"

class CountdownWindow : public QWidget {
    Q_OBJECT

public:
    CountdownWindow(QWidget *parent = nullptr) : QWidget(parent) {
        auto flag =  Qt::WindowStaysOnTopHint |
                     Qt::MSWindowsFixedSizeDialogHint | Qt::CustomizeWindowHint;
#ifdef Q_OS_WIN32
        flag = flag | Qt::Tool;
#endif
        setWindowFlags(flag);
        // 设置窗口标题和大小
        setWindowTitle("Inactivity detected");
        setFixedSize(400, 150);
        //resize(300, 150);

        // // 创建主布局
        // QWidget *centralWidget = new QWidget(this);
        QVBoxLayout *layout = new QVBoxLayout(this);
        // setCentralWidget(centralWidget);

        // 创建倒计时显示标签
        countdownLabel = new QLabel(label.arg(waitSecond), this);
        countdownLabel->setAlignment(Qt::AlignCenter);
        countdownLabel->setStyleSheet("font-size: 14px; font-weight: bold;");
        countdownLabel->setWordWrap(true); // 启用自动换行
        countdownLabel->setAlignment(Qt::AlignLeft); // 显式设置左对齐
        layout->addWidget(countdownLabel);

        // 创建取消按钮
        cancelButton = new QPushButton("Cancel", this);
        layout->addWidget(cancelButton);

        // 连接按钮信号到槽函数
        connect(cancelButton, &QPushButton::clicked, this, &CountdownWindow::cancelCountDown);

        // 初始化倒计时时间（30 秒）
        remainingTime = QTime(0, 0, 30); // 00:00:30

        // 创建定时器
        timer = new QTimer(this);
        connect(timer, &QTimer::timeout, this, &CountdownWindow::updateCountdown);
    }

    void startCountdown() {
        moveToCenter();
        // 如果定时器未运行，则开始倒计时
        if (!timer->isActive()) {
            timer->start(1000); // 每 1000ms（1秒）触发一次
        }
    }

    bool isActive() {
        return timer->isActive();
    }

    void moveToCenter() {
        QPoint mousePos = QCursor::pos();

        QScreen *screen = QGuiApplication::screenAt(mousePos);
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }

        QRect screenGeometry = screen->availableGeometry(); // 可用区域（排除任务栏等）
        QPoint screenCenter = screenGeometry.center();

        move(screenCenter.x() - width() / 2, screenCenter.y() - height() / 2);
    }
protected:
    void closeEvent(QCloseEvent *event) override {
        cancelCountDown();
    }
private slots:
    void cancelCountDown() {
        if (timer->isActive()) {
            // 如果定时器正在运行，则暂停
            timer->stop();
            // 初始化倒计时时间（30 秒）
            remainingTime = QTime(0, 0, 30); // 00:00:30
            countdownLabel->setText(label.arg(waitSecond));
            close();
        }
    }

    void updateCountdown() {
        // 减少剩余时间
        remainingTime = remainingTime.addSecs(-1);

        // 更新显示
        countdownLabel->setText(label.arg(remainingTime.toString("ss")));

        // 如果倒计时结束
        if (remainingTime == QTime(0, 0, 0)) {
            timer->stop(); // 停止定时器

            SDL_Event event;
            event.type = SDL_QUIT;
            SDL_PushEvent(&event);
            close();
        }
    }

private:
    QLabel *countdownLabel;
    QPushButton *cancelButton;
    QTimer *timer;
    QTime remainingTime;
    const QString label = "We have detected a period of inactivity. The GWS client will close after %1 seconds";
    const int waitSecond = 30;
};
