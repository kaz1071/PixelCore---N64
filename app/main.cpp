#include "app/main_window.h"

#include <QApplication>

#ifndef PIXELCORE_N64_VERSION
#define PIXELCORE_N64_VERSION "0.2.0"
#endif

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("PixelCore - N64");
    QApplication::setApplicationVersion(PIXELCORE_N64_VERSION);
    QApplication::setOrganizationName("PixelCore");

    MainWindow window;
    window.show();

    return application.exec();
}
