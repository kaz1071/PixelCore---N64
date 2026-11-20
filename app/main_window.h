#pragma once

#include "core/n64_system.h"

#include <QMainWindow>
#include <QSet>
#include <cstdint>
#include <QString>
#include <QtGlobal>

class QLabel;
class QKeyEvent;
class QTimer;
class FramebufferView;

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
    void openIplRom();
    void runEmulationFrame();
    void updateControllerInput();
    void updateRomDisplay();
    void updateFramebuffer();

    QLabel *statusLabel_ = nullptr;
    FramebufferView *framebufferView_ = nullptr;
    QTimer *frameTimer_ = nullptr;
    QString loadedRomPath_;
    QString loadedRomTitle_;
    QString loadedRomFormat_;
    qint64 loadedRomSize_ = 0;
    N64System system_;
    QSet<int> pressedKeys_;
    bool emulationRunning_ = false;
    bool executionFailed_ = false;
    bool directBootActive_ = false;
    unsigned int viWaitFrames_ = 0;
};
