#pragma once

#include <QMainWindow>
#include <QString>
#include <QtGlobal>

class QLabel;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    void openRom();
    void updateRomDisplay();

    QLabel *statusLabel_ = nullptr;
    QLabel *romInfoLabel_ = nullptr;
    QString loadedRomPath_;
    QString loadedRomTitle_;
    QString loadedRomFormat_;
    qint64 loadedRomSize_ = 0;
};
