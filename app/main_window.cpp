#include "app/main_window.h"

#include "core/rom/rom_file.h"

#include <QAction>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QKeySequence>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QPainter>
#include <QTimer>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
constexpr std::uint32_t DisplayHeight = 240;
}

class FramebufferView final : public QWidget
{
public:
    explicit FramebufferView(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumHeight(260);
        setStyleSheet("background: #080d17; border: 1px solid #263650; border-radius: 12px;");
    }

    void setFrame(const QImage &frame)
    {
        frame_ = frame;
        message_.clear();
        update();
    }

    void setMessage(const QString &message)
    {
        frame_ = QImage();
        message_ = message;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#080d17"));
        if (!frame_.isNull()) {
            const QSize fitted = frame_.size().scaled(size(), Qt::KeepAspectRatio);
            const QRect target((width() - fitted.width()) / 2,
                               (height() - fitted.height()) / 2,
                               fitted.width(), fitted.height());
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.drawImage(target, frame_);
            return;
        }

        painter.setPen(QColor("#b8c6dc"));
        painter.drawText(rect().adjusted(24, 24, -24, -24),
                         Qt::AlignCenter | Qt::TextWordWrap, message_);
    }

private:
    QImage frame_;
    QString message_ = "No game loaded\n\nChoose File → Open ROM to select an N64 game.";
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("PixelCore - N64");
    resize(1000, 650);
    setMinimumSize(720, 480);
    setFocusPolicy(Qt::StrongFocus);

    auto *fileMenu = menuBar()->addMenu("&File");
    auto *openAction = fileMenu->addAction("&Open ROM...");
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::openRom);
    auto *openIplAction = fileMenu->addAction("Open &IPL ROM...");
    connect(openIplAction, &QAction::triggered, this, &MainWindow::openIplRom);

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

    framebufferView_ = new FramebufferView(central);
    framebufferView_->setObjectName("gameViewport");
    framebufferView_->setFocusPolicy(Qt::StrongFocus);

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
    layout->addWidget(framebufferView_, 1);

    auto *inputInfo = new QLabel(
        "Keyboard controller: WASD = analog stick  |  Arrows = D-pad  |  J/K/U = A/B/Z  |  Enter = Start",
        central
    );
    inputInfo->setAlignment(Qt::AlignCenter);
    inputInfo->setWordWrap(true);
    inputInfo->setStyleSheet("color: #8197b5; font-size: 12px;");
    layout->addWidget(inputInfo);
    layout->addWidget(openButton, 0, Qt::AlignHCenter);
    layout->addStretch();

    setCentralWidget(central);
    framebufferView_->setFocus();
    frameTimer_ = new QTimer(this);
    frameTimer_->setTimerType(Qt::PreciseTimer);
    connect(frameTimer_, &QTimer::timeout, this, &MainWindow::runEmulationFrame);
    frameTimer_->start(16);
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

namespace
{
std::uint16_t controllerButtonForKey(int key)
{
    switch (key)
    {
    case Qt::Key_J: return 0x8000; // A
    case Qt::Key_K: return 0x4000; // B
    case Qt::Key_U: return 0x2000; // Z
    case Qt::Key_Return:
    case Qt::Key_Enter: return 0x1000; // Start
    case Qt::Key_Up: return 0x0800;
    case Qt::Key_Down: return 0x0400;
    case Qt::Key_Left: return 0x0200;
    case Qt::Key_Right: return 0x0100;
    case Qt::Key_Q: return 0x0020; // L
    case Qt::Key_E: return 0x0010; // R
    case Qt::Key_T: return 0x0008; // C-up
    case Qt::Key_G: return 0x0004; // C-down
    case Qt::Key_F: return 0x0002; // C-left
    case Qt::Key_H: return 0x0001; // C-right
    default: return 0;
    }
}

bool isAnalogStickKey(int key)
{
    return key == Qt::Key_W || key == Qt::Key_A ||
           key == Qt::Key_S || key == Qt::Key_D;
}
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat())
    {
        event->accept();
        return;
    }

    const int key = event->key();
    if (controllerButtonForKey(key) == 0 && !isAnalogStickKey(key))
    {
        QMainWindow::keyPressEvent(event);
        return;
    }

    pressedKeys_.insert(key);
    updateControllerInput();
    event->accept();
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat())
    {
        event->accept();
        return;
    }

    const int key = event->key();
    if (controllerButtonForKey(key) == 0 && !isAnalogStickKey(key))
    {
        QMainWindow::keyReleaseEvent(event);
        return;
    }

    pressedKeys_.remove(key);
    updateControllerInput();
    event->accept();
}

void MainWindow::updateControllerInput()
{
    std::uint16_t buttons = 0;
    for (int key : pressedKeys_)
        buttons |= controllerButtonForKey(key);

    const int horizontal = (pressedKeys_.contains(Qt::Key_D) ? 1 : 0) -
                           (pressedKeys_.contains(Qt::Key_A) ? 1 : 0);
    const int vertical = (pressedKeys_.contains(Qt::Key_W) ? 1 : 0) -
                         (pressedKeys_.contains(Qt::Key_S) ? 1 : 0);

    system_.memory().pif().setControllerConnected(0, true);
    system_.memory().pif().setControllerState(
        0, buttons, static_cast<std::int8_t>(horizontal * 80),
        static_cast<std::int8_t>(vertical * 80)
    );

    statusLabel_->setText("Keyboard controller connected — WASD / arrows / J K U / Enter");
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

    system_.loadCartridge(rom.image());
    system_.reset();
    executionFailed_ = false;
    emulationRunning_ = system_.memory().bootRomSize() != 0;

    updateRomDisplay();
}

void MainWindow::openIplRom()
{
    const QString path = QFileDialog::getOpenFileName(
        this,
        "Open Nintendo 64 IPL ROM",
        QString(),
        "IPL ROM images (*.bin *.rom *.ipl);;All files (*)"
    );
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "Could not load IPL ROM", file.errorString());
        return;
    }

    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        QMessageBox::warning(this, "Could not load IPL ROM", "The selected IPL ROM is empty.");
        return;
    }

    std::vector<std::uint8_t> image;
    image.reserve(static_cast<std::size_t>(bytes.size()));
    for (char byte : bytes)
        image.push_back(static_cast<std::uint8_t>(static_cast<unsigned char>(byte)));

    system_.loadBootRom(image);
    executionFailed_ = false;
    if (!loadedRomPath_.isEmpty()) {
        system_.reset();
        emulationRunning_ = true;
        framebufferView_->setMessage("IPL ROM loaded\n\nStarting the emulated N64 boot process...");
        statusLabel_->setText("IPL loaded — starting emulation");
    } else {
        emulationRunning_ = false;
        statusLabel_->setText("IPL ROM loaded — now open a game ROM");
    }
}

void MainWindow::updateRomDisplay()
{
    const QFileInfo fileInfo(loadedRomPath_);
    const double sizeMiB = static_cast<double>(loadedRomSize_) / (1024.0 * 1024.0);

    framebufferView_->setMessage(
        QString("ROM loaded\n\n%1\n%2\n%3\n%4 MiB\n\n"
                "%5")
            .arg(fileInfo.fileName())
            .arg(loadedRomTitle_.isEmpty() ? "Title: (not set in ROM header)" :
                                           "Title: " + loadedRomTitle_)
            .arg("Byte order: " + loadedRomFormat_)
            .arg(QString::number(sizeMiB, 'f', 2))
            .arg(emulationRunning_ ? "Running the IPL boot process..." :
                                     "Open File → Open IPL ROM to provide boot firmware and start execution.")
    );

    statusLabel_->setText(emulationRunning_ ? "ROM loaded — emulation running" :
                                              "ROM loaded — waiting for an IPL ROM");
}

void MainWindow::runEmulationFrame()
{
    if (emulationRunning_) {
        std::string error;
        // Keep each GUI tick bounded so a difficult instruction sequence cannot
        // monopolize the desktop event loop.
        if (!system_.runInstructions(50000, error)) {
            emulationRunning_ = false;
            executionFailed_ = true;
            const QString message = error.empty()
                ? "CPU or RSP execution stopped unexpectedly."
                : QString::fromStdString(error);
            framebufferView_->setMessage("Emulation stopped\n\n" + message);
            statusLabel_->setText("Emulation stopped: " + message);
            return;
        }
    }

    if (!executionFailed_)
        updateFramebuffer();
}

void MainWindow::updateFramebuffer()
{
    if (loadedRomPath_.isEmpty())
        return;

    std::uint32_t width = 0;
    if (!system_.memory().read32(N64MemoryBus::ViWidth, width) ||
        width == 0 || width > 640) {
        framebufferView_->setMessage("ROM loaded\n\nWaiting for VI video configuration...");
        return;
    }

    std::vector<std::uint32_t> pixels;
    if (!system_.memory().renderFramebuffer(width, DisplayHeight, pixels)) {
        framebufferView_->setMessage("ROM loaded\n\nWaiting for a valid VI framebuffer...");
        return;
    }

    const QImage frame(reinterpret_cast<const uchar *>(pixels.data()),
                       static_cast<int>(width), static_cast<int>(DisplayHeight),
                       static_cast<int>(width * sizeof(std::uint32_t)),
                       QImage::Format_ARGB32);
    framebufferView_->setFrame(frame.copy());
}
