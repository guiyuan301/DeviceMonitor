#include <QApplication>
#include "index.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    app.setApplicationName("控制中心");
    app.setApplicationVersion("1.0");

    Index w;
    w.show();

    return app.exec();
}
