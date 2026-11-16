#include "app/main_window.h"

#include "core/rom/rom_file.h"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("PixelCore - N64");
    resize(1000, 650);
    setMinimumSize(720, 480);

    auto *fileMenu = menuBar()->addMenu("&File");
    auto *openAction = fileMenu->addAction("&Open ROM...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openRom);

    fileMenu->addSeparator();
    auto *quitAction = fileMenu->addAction("&Quit");
    quitAction->setShortcut(QKeySequence::Quit);
    connect(quitAction, &QAction::triggered, this, &QWidget::close);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(16);

    auto *brand = new QLabel("PIXELCORE", central);
    brand->setAlignment(Qt::AlignCenter);
    brand->setStyleSheet(
        "color: #f4f7ff; font-size: 34px; font-weight: 800; letter-spacing: 5px;"
    );

    auto *subtitle = new QLabel("N64", central);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet(
        "color: #70caff; font-size: 20px; font-weight: 700; letter-spacing: 8px;"
    );

    auto *viewport = new QWidget(central);
    viewport->setObjectName("gameViewport");
    viewport->setMinimumHeight(260);
    viewport->setStyleSheet(
        "#gameViewport { background: #080d17; border: 1px solid #263650; border-radius: 12px; }"
    );

    auto *viewportLayout = new QVBoxLayout(viewport);
    romInfoLabel_ = new QLabel(
        "No game loaded\n\nChoose File → Open ROM to select an N64 game.",
        viewport
    );
    romInfoLabel_->setAlignment(Qt::AlignCenter);
    romInfoLabel_->setWordWrap(true);
    romInfoLabel_->setStyleSheet("color: #b8c6dc; font-size: 16px;");
    viewportLayout->addWidget(romInfoLabel_);

    auto *openButton = new QPushButton("Open ROM", central);
    openButton->setCursor(Qt::PointingHandCursor);
    openButton->setMinimumHeight(46);
    openButton->setStyleSheet(
        "QPushButton { background: #2678c9; color: white; border: 0; "
        "border-radius: 8px; padding: 10px 24px; font-size: 15px; font-weight: 700; }"
        "QPushButton:hover { background: #328de3; }"
        "QPushButton:pressed { background: #1b5f9f; }"
    );
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openRom);

    layout->addStretch();
    layout->addWidget(brand);
    layout->addWidget(subtitle);
    layout->addSpacing(12);
    layout->addWidget(viewport, 1);
    layout->addWidget(openButton, 0, Qt::AlignHCenter);
    layout->addStretch();

    setCentralWidget(central);
    statusLabel_ = new QLabel("Ready — no ROM loaded", this);
    statusBar()->addWidget(statusLabel_, 1);

    setStyleSheet(
        "QMainWindow { background: #111a2a; }"
        "QMenuBar { background: #17243a; color: #edf3ff; padding: 5px; }"
        "QMenuBar::item:selected { background: #29415f; }"
        "QMenu { background: #17243a; color: #edf3ff; border: 1px solid #344967; }"
        "QMenu::item:selected { background: #2678c9; }"
        "QStatusBar { background: #17243a; color: #aebed4; }"
    );
}

void MainWindow::openRom()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open Nintendo 64 ROM",
        QString(),
        "Nintendo 64 ROMs (*.z64 *.n64 *.v64 *.rom *.bin);;All files (*)"
    );

    if (path.isEmpty()) {
        return;
    }

    N64Rom rom;
    QString error;
    if (!rom.load(path, error)) {
        QMessageBox::warning(this, "Could not load ROM", error);
        statusLabel_->setText("ROM load failed");
        return;
    }

    loadedRomPath_ = path;
    loadedRomTitle_ = rom.title();
    loadedRomFormat_ = rom.byteOrderName();
    loadedRomSize_ = rom.fileSize();

    updateRomDisplay();
}

void MainWindow::updateRomDisplay()
{
    const QFileInfo fileInfo(loadedRomPath_);
    const double sizeMiB = static_cast<double>(loadedRomSize_) / (1024.0 * 1024.0);

    romInfoLabel_->setText(
        QString("ROM recognized!\n\n%1\n%2\n%3\n%4 MiB\n\n"
                "The ROM is loaded for inspection. Game emulation is not implemented yet.")
            .arg(fileInfo.fileName())
            .arg(loadedRomTitle_.isEmpty() ? "Title: (not set in ROM header)" :
                                           "Title: " + loadedRomTitle_)
            .arg("Byte order: " + loadedRomFormat_)
            .arg(QString::number(sizeMiB, 'f', 2))
    );

    statusLabel_->setText("ROM recognized — " + fileInfo.fileName());
}
