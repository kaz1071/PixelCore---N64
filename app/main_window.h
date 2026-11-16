#pragma once

#include "core/n64_system.h"

#include <QMainWindow>
#include <QSet>
#include <cstdint>
#include <QString>
#include <QtGlobal>

class QLabel;
class QKeyEvent;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;

private:
    void openRom();
    void updateControllerInput();
    void updateRomDisplay();

    QLabel *statusLabel_ = nullptr;
    QLabel *romInfoLabel_ = nullptr;
    QString loadedRomPath_;
    QString loadedRomTitle_;
    QString loadedRomFormat_;
    qint64 loadedRomSize_ = 0;
    N64System system_;
    QSet<int> pressedKeys_;
};
