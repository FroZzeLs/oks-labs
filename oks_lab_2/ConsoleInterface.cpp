#include "ConsoleInterface.h"
#include "HammingBlock.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <conio.h>
#include <windows.h>
#include <algorithm>

static int inputInteger(int min, int max) {
    int number = 0;
    while (true) {
        if (scanf_s("%d", &number) == 1) {
            int c = getchar();
            if (c == '\n' && number >= min && number <= max) break;
        }
        printf("Неправильный ввод. Попробуйте ещё раз: ");
        rewind(stdin);
    }
    return number;
}

ConsoleInterface::ConsoleInterface()
    : availablePortPairs{ {"COM3","COM4"},{"COM10","COM11"} },
    baudRates{ 50,75,110,134,150,200,300,600,1200,2400,4800,9600,19200,38400,57600,115200 } {
}

void ConsoleInterface::run() {
    while (true) {
        system("cls");
        showMainMenu();
        int choice = inputInteger(1, 6);
        switch (choice) {
        case 1: setupPorts(); break;
        case 2: sendMessageMenu(); break;
        case 3: receiveMessageMenu(); break;
        case 4: changeBaudRate(); break;
        case 5: viewLastSentFrame(); break;
        case 6:
            portManager.closePorts();
            return;
        default:
            std::cout << "Неверный выбор. Попробуйте снова." << std::endl;
        }
    }
}

void ConsoleInterface::showMainMenu() {
    std::cout << "Текущие настройки:" << std::endl;
    std::cout << "Порт отправки: " << (portManager.getCurrentSendPort().empty() ? "не выбран" : portManager.getCurrentSendPort()) << std::endl;
    std::cout << "Порт приема: " << (portManager.getCurrentReceivePort().empty() ? "не выбран" : portManager.getCurrentReceivePort()) << std::endl;
    std::cout << "Скорость: " << portManager.getCurrentBaudRate() << " бод" << std::endl << std::endl;
    std::cout << "Меню:" << std::endl;
    std::cout << "1. Настроить порты" << std::endl;
    std::cout << "2. Отправить сообщение" << std::endl;
    std::cout << "3. Получить сообщение" << std::endl;
    std::cout << "4. Изменить скорость передачи" << std::endl;
    std::cout << "5. Просмотр структуры последнего переданного кадра" << std::endl;
    std::cout << "6. Выход" << std::endl;
    std::cout << "Выберите действие: ";
}

void ConsoleInterface::setupPorts() {
    system("cls");
    std::cout << "=== Настройка портов ===" << std::endl;
    std::cout << "Выберите пару портов:" << std::endl;
    for (size_t i = 0; i < availablePortPairs.size(); ++i) {
        std::cout << i + 1 << ". Отправка: " << availablePortPairs[i].sendPort << " / Прием: " << availablePortPairs[i].receivePort << std::endl;
    }
    std::cout << "Ваш выбор (1-" << availablePortPairs.size() << "): ";
    int choice = inputInteger(1, static_cast<int>(availablePortPairs.size()));
    auto selectedPair = availablePortPairs[choice - 1];
    bool isPortsOpen = true;
    std::cout << "\nНастройка портов..." << std::endl;
    if (portManager.setSendPort(selectedPair.sendPort)) {
        std::cout << "Порт отправки " << selectedPair.sendPort << " настроен" << std::endl;
    }
    else {
        std::cout << "Ошибка настройки порта отправки!" << std::endl;
        isPortsOpen = false;
    }
    if (portManager.setReceivePort(selectedPair.receivePort)) {
        std::cout << "Порт приема " << selectedPair.receivePort << " настроен" << std::endl;
    }
    else {
        std::cout << "Ошибка настройки порта приема!" << std::endl;
        isPortsOpen = false;
    }
    if (isPortsOpen) std::cout << "\nНастройка завершена успешно. Нажмите любую клавишу для продолжения..." << std::endl;
    else std::cout << "\nНастройка завершена с ошибками. Проверьте состояние портов и повторите попытку.\nНажмите любую клавишу для продолжения..." << std::endl;
    _getch();
    rewind(stdin);
}

void ConsoleInterface::sendMessageMenu() {
    system("cls");

    if ((portManager.getCurrentSendPort() != "COM3" && portManager.getCurrentSendPort() != "COM10") || (portManager.getCurrentReceivePort() != "COM4" && portManager.getCurrentReceivePort() != "COM11")) {
        std::cout << "Ошибка: Порты не настроены!" << std::endl;
        std::cout << "\nНажмите любую клавишу для продолжения..." << std::endl;
        _getch();
        return;
    }

    std::cout << "=== Отправка сообщения ===" << std::endl;
    std::cout << "Порт отправки: " << portManager.getCurrentSendPort() << std::endl;
    std::cout << "Порт приема: " << portManager.getCurrentReceivePort() << std::endl;
    std::cout << "Скорость: " << portManager.getCurrentBaudRate() << " бод" << std::endl;
    std::cout << "\nВведите сообщение для отправки: ";
    std::string message;
    std::getline(std::cin, message);
    if (message.empty()) { std::cout << "Сообщение не может быть пустым!" << std::endl; _getch(); rewind(stdin); return; }

    DWORD bytesWritten = 0;
    if (portManager.sendMessage(message, &bytesWritten)) {
        std::cout << "Сообщение успешно отправлено!" << std::endl;
    }
    else {
        std::cout << "Ошибка отправки сообщения!" << std::endl;
    }
    std::cout << "\nНажмите любую клавишу для продолжения..." << std::endl;
    _getch(); rewind(stdin);
}

static std::string to_hex(uint8_t v) {
    std::ostringstream oss;
    oss << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << int(v);
    return oss.str();
}

std::string ConsoleInterface::prettyPrintRawFrame(const std::vector<uint8_t>& stuffed) const {
    const uint8_t ESC = 0x1B;
    std::ostringstream oss;

    if (stuffed.size() < 2) return "Некорректный кадр.";

    size_t i = 0;
    oss << "Флаг начала кадра: " << to_hex(stuffed[i++]) << "\n";

    // --- Поля заголовка ---
    if (stuffed[i] == ESC) i++;
    oss << "Отправитель: " << to_hex(stuffed[i++]) << "\n";

    if (stuffed[i] == ESC) i++;
    oss << "Получатель: " << to_hex(stuffed[i++]) << "\n";

    uint64_t time = 0;
    for (int idx = 0; idx < 8; idx++) {
        if (stuffed[i] == ESC) i++;
        time |= (uint64_t(stuffed[i++]) << (8 * idx));
    }

    oss << "Время отправления: ";
    for (int b = 0; b < 8; b++) {
        uint8_t byte = (time >> (8 * b)) & 0xFF;
        oss << to_hex(byte) << " ";
    }
    oss << "\n";

    if (stuffed[i] == ESC) i++;
    oss << "Порядковый номер кадра: " << to_hex(stuffed[i++]) << "\n";

    // --- Длина данных (2 байта, Big Endian) ---
    uint16_t dataLen = 0;
    if (stuffed[i] == ESC) i++;
    dataLen |= (uint16_t)stuffed[i++] << 8;
    if (stuffed[i] == ESC) i++;
    dataLen |= (uint16_t)stuffed[i++];

    // --- Данные ---
    oss << "Данные: ";
    int logical_bytes_read = 0;
    while (logical_bytes_read < dataLen && i < stuffed.size() - 1) {
        uint8_t current_byte = stuffed[i];
        oss << to_hex(current_byte) << " ";
        if (current_byte == ESC) {
            i++; // Переходим к следующему байту, который тоже часть данных
            oss << to_hex(stuffed[i]) << " ";
        }
        logical_bytes_read++;
        i++;
    }
    oss << "\n";

    // --- FCS ---
    oss << "FCS: ";
    while (i < stuffed.size() - 1) {
        oss << to_hex(stuffed[i++]) << " ";
    }
    oss << "\n";

    // --- Флаг конца ---
    oss << "Флаг конца кадра: " << to_hex(stuffed.back());
    return oss.str();
}


void ConsoleInterface::receiveMessageMenu() {
    system("cls");

    // Проверка корректности настроек портов
    if ((portManager.getCurrentSendPort() != "COM3" && portManager.getCurrentSendPort() != "COM10") ||
        (portManager.getCurrentReceivePort() != "COM4" && portManager.getCurrentReceivePort() != "COM11")) {
        std::cout << "Ошибка: Порты не настроены!" << std::endl;
        std::cout << "\nНажмите любую клавишу для продолжения..." << std::endl;
        _getch();
        return;
    }

    // Получение всех кадров
    auto frames = portManager.receiveAllFrames();

    if (frames.empty()) {
        _getch();
        return;
    }

    std::string fullMessage;
    bool had_uncorrectable_error = false;

    // Декодирование всех кадров
    for (auto& f : frames) {
        HammingBlockResult res = HammingBlock::decode_and_correct(f.data, f.fcs);

        if (res.double_error_detected) {
            had_uncorrectable_error = true;
        }

        fullMessage.append(
            reinterpret_cast<const char*>(res.corrected_data.data()),
            res.corrected_data.size()
        );
    }

    system("cls");

    // Если была ошибка, вывести предупреждение, но всё равно показать сообщение
    if (had_uncorrectable_error) {
        std::cout << "Данные повреждены (обнаружена двойная или множественная ошибка)\n\n";
        std::cout << "Принятое сообщение:\n";
    }

    std::cout << fullMessage;

    _getch();
}


void ConsoleInterface::changeBaudRate() {
    system("cls");
    std::cout << "=== Изменение скорости передачи ===" << std::endl;
    std::cout << "Текущая скорость: " << portManager.getCurrentBaudRate() << " бод" << std::endl << std::endl;
    std::cout << "Доступные скорости:" << std::endl;
    for (size_t i = 0; i < baudRates.size(); ++i) std::cout << i + 1 << ". " << baudRates[i] << " бод" << std::endl;
    std::cout << "Выберите скорость (1-" << baudRates.size() << "): ";
    int choice = inputInteger(1, static_cast<int>(baudRates.size()));
    if (choice >= 1 && choice <= static_cast<int>(baudRates.size())) {
        DWORD newBaudRate = baudRates[choice - 1];
        if (portManager.setBaudRate(newBaudRate)) std::cout << "Скорость изменена на " << newBaudRate << " бод" << std::endl;
        else std::cout << "Ошибка изменения скорости!" << std::endl;
    }
    else std::cout << "Неверный выбор скорости." << std::endl;
    std::cout << "\nНажмите любую клавишу для продолжения..." << std::endl;
    _getch(); rewind(stdin);
}

void ConsoleInterface::viewLastSentFrame() {
    system("cls");
    const auto& raw = portManager.getLastSentRawFrame();
    if (raw.empty()) { std::cout << "Нет отправленных кадров для просмотра." << std::endl; _getch(); return; }
    std::cout << prettyPrintRawFrame(raw) << std::endl;
    std::cout << "\nНажмите любую клавишу для продолжения..." << std::endl;
    _getch(); rewind(stdin);
}