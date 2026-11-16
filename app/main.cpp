#include "app/main_window.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication application(argc, argv);
    QApplication::setApplicationName("PixelCore - N64");
    QApplication::setApplicationVersion("0.1.0");
    QApplication::setOrganizationName("PixelCore");

    MainWindow window;
    window.show();

    return application.exec();
}
