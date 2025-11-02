#include "ConsoleInterface.h"
#include <windows.h>

int main() {
    system("chcp 1251");
    system("cls");

    ConsoleInterface app;
    app.run();

    return 0;
}
