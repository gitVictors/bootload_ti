#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>
#include <algorithm>
#include <iomanip>  

#include "prog.h"

// Функция для чтения байтов из текстового файла
std::vector<unsigned char> readFileBytes(const std::string& filename) {

    std::vector<unsigned char> buffer;
    std::ifstream file(filename);

    if (!file) {
        std::cerr << "Ошибка при открытии файла!" << std::endl;
        return buffer;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string hexValue;

        while (iss >> hexValue) {
            try {
                // Преобразуем шестнадцатеричную строку в байт
                unsigned char byte = std::stoi(hexValue, nullptr, 16);
                buffer.push_back(byte);
            }
            catch (...) {
                std::cerr << "Ошибка при парсинге значения: " << hexValue << std::endl;
            }
        }
    }

    file.close();
    return buffer;
}

int main() {


    std::cout << "Hello World!\n";

    initFirmwareDownload_mod();

    // Читаем байты из файла
    std::vector<unsigned char> fileData = readFileBytes("test1.txt");

    // Выводим результат
    if (!fileData.empty()) {
        std::cout << "Успешно считано " << fileData.size() << " байт из файла\n";
        std::cout << "Содержимое файла:\n";

        // Выводим байты в шестнадцатеричном формате
        for (size_t i = 0; i < fileData.size(); ++i) {
            std::cout << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(fileData[i]);
            if ((i + 1) % 4 == 0)
                std::cout << std::endl;
            else
                std::cout << " ";
        }
        std::cout << std::endl;
    }
    else {
        std::cout << "Не удалось считать данные из файла\n";
    }

    // тестирование функции ------------------------------------------------------

    // Создаем временный буфер для 8 байт
    uint8_t tempBuffer[8];
    size_t dataSize = fileData.size();
    size_t processedBytes = 0;

    // Обрабатываем данные порциями по 8 байт
    while (processedBytes < dataSize) {
        // Определяем количество байт для текущей порции
        size_t bytesToProcess = std::min(8ULL, dataSize - processedBytes);

        // Копируем данные в временный буфер
        for (size_t i = 0; i < bytesToProcess; ++i) {
            tempBuffer[i] = fileData[processedBytes + i];
        }

        // Вызываем функцию обработки
        processCANFirmwareData_mod(tempBuffer, static_cast<uint16_t>(bytesToProcess));

        // Обновляем счетчик обработанных байт
        processedBytes += bytesToProcess;
    }

    //-----------------------------------------------------------------------------

    return 0;
}
