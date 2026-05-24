#include <iostream>
#include <fstream>
#include <vector>
#include <sstream>

void XLNX_CAN_sendMessage( int32_t , uint16_t*  , int32_t ) {
    //заглушка
    return;
}

// Глобальные переменные для хранения состояния приёма прошивки
// (размещаются в Filter1_RegsFile, который в ОЗУ)
//#pragma DATA_SECTION(g_progState, ".Filter1_RegsFile");
static struct {
    uint32_t currentAddr;       // Текущий адрес для записи
    uint16_t blockSizeWords;    // Размер текущего блока (в словах)
    uint16_t wordCount;         // Счётчик принятых слов в текущем блоке
    uint8_t buffer[256];        // Буфер для накопления данных блока
    uint8_t bufferIndex;        // Индекс в буфере
    uint8_t state;              // 0=ожидание заголовка, 1=приём данных
    uint8_t sectorBuffer[0x4000]; // Буфер для целого сектора (16KB)
    uint32_t sectorAddr;        // Адрес текущего сектора
    uint16_t sectorOffset;      // Смещение в текущем секторе
    uint16_t remainingBytes;    // Остаток байт в текущем блоке
    uint16_t skipBytes;         // Количество байт для пропуска (заголовок)
    uint32_t entriPoint;        //точка входа в программу
} g_progState = { 0 };

// Константы для состояния
#define STATE_WAIT_BLOCK_HEADER  0
#define STATE_RECEIVE_DATA       1

// Адреса секторов флеш-памяти
static const uint32_t sectorAddresses[] = {
    0x080000, 0x082000, 0x084000, 0x086000,
    0x088000, 0x090000, 0x098000, 0x0A0000,
    0x0A8000, 0x0B0000, 0x0B8000, 0x0BA000,
    0x0BC000, 0x0BE000, 0x0C0000
};

// Функция получения следующего слова (2 байта) из CAN-буфера
static uint16_t getNextWord(uint8_t* rx_buff, uint16_t* offset)
{
    uint16_t word;
    word = rx_buff[*offset] | (rx_buff[*offset + 1] << 8);
    //word = (rx_buff[*offset]<<8) | rx_buff[*offset + 1];
    *offset += 2;
    return word;
}

// Функция записи накопленных данных в текущий сектор
static uint8_t flushSector(void)
{
    uint8_t sectorNum;

    // Определяем номер сектора по адресу
    for (sectorNum = 0; sectorNum < 15; sectorNum++) {
        if (g_progState.sectorAddr >= sectorAddresses[sectorNum]) {
            break;
        }
    }

    if (sectorNum >= 15) {
        return 1; // Ошибка: неверный адрес сектора
    }

    // Здесь вызывается Flash API для программирования сектора
    // ...
    printf("Write addr sector 0x%08X", g_progState.sectorAddr);
    for (int i = 0; i < 4; ++i)
        printf(" 0x%08X ", g_progState.sectorBuffer[i]);
    printf("\n");

    // Отправка сообщения о готовности принять следующий сектор
    uint16_t msgData[2];
    msgData[0] = 0x0001;  // Код готовности
    msgData[1] = sectorNum;
    XLNX_CAN_sendMessage(0x01, msgData, 2);

    // Сброс состояния для следующего сектора
    g_progState.sectorOffset = 0;
    g_progState.currentAddr = g_progState.sectorAddr + 0x4000;

    return 0;
}

// Инициализация процесса загрузки (вызывается один раз перед началом)
void initFirmwareDownload(void)
{
    g_progState.state = STATE_WAIT_BLOCK_HEADER;
    g_progState.wordCount = 0;
    g_progState.skipBytes = 0;
    g_progState.remainingBytes = 0;
    g_progState.bufferIndex = 0;
    g_progState.sectorOffset = 0;
}

// Основная функция обработки принятых данных
// Вызывается каждый раз при приходе очередного CAN-сообщения (8 байт)
void processCANFirmwareData(uint8_t* rx_buff_from_can, uint16_t length)
{
    static uint32_t offset_global = 0;
    uint16_t offset = 0;
    uint16_t bytesToCopy;

    if (length == 0) return;

    while (offset < length) {

        // ========== ОБРАБОТКА ЗАГОЛОВКА (первое сообщение) ==========
        if (g_progState.state == STATE_WAIT_BLOCK_HEADER && g_progState.wordCount == 0) {

            // Пропускаем ключ (0xAA 0x08) и зарезервированные байты (всего 18 байт) --------
            if (g_progState.skipBytes == 0 && offset_global == 0) {
                g_progState.skipBytes = 18;
            }

            // Пропускаем байты заголовка
            while (offset < length && g_progState.skipBytes > 0) {
                offset++;
                offset_global++;
                g_progState.skipBytes--;
            }

            if (g_progState.skipBytes > 0) continue; // Ждём следующий пакет
            //-----------------------------------------------------------------------------------

            //  точк входа (4 байта)
            if (offset + 4 <= length && offset_global == 18) {
                g_progState.entriPoint = getNextWord(rx_buff_from_can, &offset);
                g_progState.entriPoint |= (getNextWord(rx_buff_from_can, &offset) << 16);
                offset += 4;
                offset_global += 4;
            }
            else {
                // Точка входа пришла не полностью
                // Для упрощения считаем, что CAN-сообщение всегда содержит целые поля
                return;
            }

            //----------------------------------------------------------------------------------

            // Чтение размера первого блока
            if (offset + 2 <= length && offset_global == 22) {
                g_progState.blockSizeWords = getNextWord(rx_buff_from_can, &offset);
            }
            else {
                return;
            }

            // Чтение адреса назначения
            if (offset + 4 <= length && offset_global == 24) {
                g_progState.currentAddr = getNextWord(rx_buff_from_can, &offset);
                g_progState.currentAddr |= (getNextWord(rx_buff_from_can, &offset) << 16);
            }
            //else {
            //    return;
            //}

            // Инициализация состояния для приёма данных
            g_progState.sectorAddr = g_progState.currentAddr & 0xFFFFC000;
            g_progState.sectorOffset = g_progState.currentAddr & 0x3FFF;
            g_progState.bufferIndex = 0;
            g_progState.remainingBytes = g_progState.blockSizeWords * 2;
            g_progState.state = STATE_RECEIVE_DATA;

            continue;
        }

        // ========== ПРИЁМ ДАННЫХ ==========
        if (g_progState.state == STATE_RECEIVE_DATA) {

            // Определяем сколько байт можно скопировать
            bytesToCopy = (length - offset);
            if (bytesToCopy > g_progState.remainingBytes) {
                bytesToCopy = g_progState.remainingBytes;
            }

            // Копируем данные в буфер сектора
            for (uint16_t i = 0; i < bytesToCopy; i++) {
                g_progState.sectorBuffer[g_progState.sectorOffset + g_progState.bufferIndex] = rx_buff_from_can[offset + i];
                g_progState.bufferIndex++;
            }

            offset += bytesToCopy;
            g_progState.remainingBytes -= bytesToCopy;

            // Если заполнили текущий сектор
            if (g_progState.sectorOffset + g_progState.bufferIndex >= 0x4000) {
                flushSector();
            }

            // Если блок полностью принят
            if (g_progState.remainingBytes == 0) {

                // Чтение следующего блока (размер и адрес) из оставшихся данных
                if (offset + 2 <= length) {
                    g_progState.blockSizeWords = getNextWord(rx_buff_from_can, &offset);
                }
                else {
                    // Размер следующего блока придёт в следующем CAN-сообщении
                    g_progState.state = STATE_WAIT_BLOCK_HEADER;
                    g_progState.wordCount = 1; // Маркер, что заголовок уже начали обрабатывать
                    continue;
                }

                // Проверка на конец загрузки (размер блока = 0)
                if (g_progState.blockSizeWords == 0) {
                    // Завершение загрузки
                    uint16_t msgData[2];
                    msgData[0] = 0x0002;  // Код завершения
                    msgData[1] = 0x0000;
                    XLNX_CAN_sendMessage(0x01, msgData, 2);
                    g_progState.state = STATE_WAIT_BLOCK_HEADER;
                    g_progState.wordCount = 0;
                    g_progState.skipBytes = 0;
                    return;
                }

                // Чтение следующего адреса
                if (offset + 4 <= length) {
                    g_progState.currentAddr = getNextWord(rx_buff_from_can, &offset);
                    g_progState.currentAddr |= (getNextWord(rx_buff_from_can, &offset) << 16);
                }
                else {
                    // Адрес придёт в следующем сообщении
                    g_progState.state = STATE_WAIT_BLOCK_HEADER;
                    g_progState.wordCount = 2; // Маркер состояния
                    continue;
                }

                // Определяем новый сектор
                if ((g_progState.currentAddr & 0xFFFFC000) != g_progState.sectorAddr) {
                    flushSector();
                    g_progState.sectorAddr = g_progState.currentAddr & 0xFFFFC000;
                    g_progState.sectorOffset = g_progState.currentAddr & 0x3FFF;
                }

                // Инициализация для следующего блока
                g_progState.bufferIndex = 0;
                g_progState.remainingBytes = g_progState.blockSizeWords * 2;
                g_progState.state = STATE_RECEIVE_DATA;
            }
        }

        // ========== ДООБРАБОТКА НАЧАЛА СЛЕДУЮЩЕГО БЛОКА ==========
        // Если находимся в состоянии WAIT с маркером, что размер уже прочитан
        if (g_progState.state == STATE_WAIT_BLOCK_HEADER && g_progState.wordCount == 2) {
            if (offset + 4 <= length) {
                g_progState.currentAddr = getNextWord(rx_buff_from_can, &offset);
                g_progState.currentAddr |= (getNextWord(rx_buff_from_can, &offset) << 16);

                // Определяем новый сектор
                if ((g_progState.currentAddr & 0xFFFFC000) != g_progState.sectorAddr) {
                    flushSector();
                    g_progState.sectorAddr = g_progState.currentAddr & 0xFFFFC000;
                    g_progState.sectorOffset = g_progState.currentAddr & 0x3FFF;
                }

                g_progState.bufferIndex = 0;
                g_progState.remainingBytes = g_progState.blockSizeWords * 2;
                g_progState.state = STATE_RECEIVE_DATA;
                g_progState.wordCount = 0;
            }
        }
    }
}


void initFirmwareDownload_mod(void)
{
    g_progState.state = STATE_WAIT_BLOCK_HEADER;
    g_progState.wordCount = 0;
    g_progState.skipBytes = 0;
    g_progState.remainingBytes = 0;
    g_progState.bufferIndex = 0;
    g_progState.sectorOffset = 0;

    // Сброс статических переменных в функции processCANFirmwareData
    // (лучше вынести их в глобальное состояние)
}

// Основная функция обработки принятых данных
void processCANFirmwareData_mod(uint8_t* rx_buff_from_can, uint16_t length)
{
    static uint8_t headerSkipRemaining = 18;  // Осталось пропустить байт заголовка
    static uint8_t readingEntryPoint = 1;     // Флаг чтения точки входа
    static uint8_t readingBlockSize = 0;      // Флаг чтения размера блока
    static uint8_t readingAddress = 0;        // Флаг чтения адреса

    uint16_t offset = 0;
    uint16_t bytesToCopy;

    if (length == 0) return;

    while (offset < length) {

        // ========== ПРОПУСК ЗАГОЛОВКА (только в самом начале) ==========
        if (headerSkipRemaining > 0) {
            uint8_t skipNow = (headerSkipRemaining < (length - offset)) ?
                headerSkipRemaining : (length - offset);
            offset += skipNow;
            headerSkipRemaining -= skipNow;
            if (headerSkipRemaining > 0) return; // Ждем следующий пакет
            continue;
        }

        // ========== ЧТЕНИЕ ТОЧКИ ВХОДА (4 байта) ==========
        if (readingEntryPoint) {
            if (offset + 4 <= length) {
                g_progState.entriPoint = getNextWord(rx_buff_from_can, &offset)<<16;
                g_progState.entriPoint |= getNextWord(rx_buff_from_can, &offset);
                readingEntryPoint = 0;
                readingBlockSize = 1;
                printf("Entry point: 0x%08X\n", g_progState.entriPoint);
            }
            else {
                return; // Ждем следующие пакеты
            }
        }

        // ========== ЧТЕНИЕ РАЗМЕРА БЛОКА (2 байта) ==========
        if (readingBlockSize) {
            if (offset + 2 <= length) {
                g_progState.blockSizeWords = getNextWord(rx_buff_from_can, &offset);
                readingBlockSize = 0;

                // Проверка на конец загрузки
                if (g_progState.blockSizeWords == 0) {
                    // Завершение загрузки
                    uint16_t msgData[2];
                    msgData[0] = 0x0002;
                    msgData[1] = 0x0000;
                    XLNX_CAN_sendMessage(0x01, msgData, 2);

                    // Сброс состояния
                    headerSkipRemaining = 18;
                    readingEntryPoint = 1;
                    g_progState.state = STATE_WAIT_BLOCK_HEADER;
                    return;
                }

                readingAddress = 1;
                printf("Block size: %d words (%d bytes)\n",
                    g_progState.blockSizeWords,
                    g_progState.blockSizeWords * 2);
            }
            else {
                return;
            }
        }

        // ========== ЧТЕНИЕ АДРЕСА (4 байта) ==========
        if (readingAddress) {
            if (offset + 4 <= length) {
                g_progState.currentAddr = getNextWord(rx_buff_from_can, &offset) << 16;
                g_progState.currentAddr |=  getNextWord(rx_buff_from_can, &offset) ;
                readingAddress = 0;

                // Инициализация состояния для приема данных
                g_progState.sectorAddr = g_progState.currentAddr & 0xFFFFC000;
                g_progState.sectorOffset = g_progState.currentAddr & 0x3FFF;
                g_progState.bufferIndex = 0;
                g_progState.remainingBytes = g_progState.blockSizeWords * 2;
                g_progState.state = STATE_RECEIVE_DATA;

                printf("Address: 0x%08X, Sector: 0x%08X, Offset: 0x%04X\n",
                    g_progState.currentAddr,
                    g_progState.sectorAddr,
                    g_progState.sectorOffset);
            }
            else {
                return;
            }
        }

        // ========== ПРИЕМ ДАННЫХ ==========
        if (g_progState.state == STATE_RECEIVE_DATA && g_progState.remainingBytes > 0) {

            // Определяем сколько байт можно скопировать
            bytesToCopy = (length - offset);
            if (bytesToCopy > g_progState.remainingBytes) {
                bytesToCopy = g_progState.remainingBytes;
            }

            // Копируем данные в буфер сектора
            for (uint16_t i = 0; i < bytesToCopy; i++) {
                uint32_t bufferPos = g_progState.sectorOffset + g_progState.bufferIndex;
                if (bufferPos < 0x4000) {
                    g_progState.sectorBuffer[bufferPos] = rx_buff_from_can[offset + i];
                }
                g_progState.bufferIndex++;
            }

            offset += bytesToCopy;
            g_progState.remainingBytes -= bytesToCopy;

            // Если заполнили текущий сектор
            if (g_progState.sectorOffset + g_progState.bufferIndex >= 0x4000) {
                flushSector();
            }

            // Если блок полностью принят
            if (g_progState.remainingBytes == 0) {
                
                flushSector();

                // Переход к чтению следующего блока
                g_progState.state = STATE_WAIT_BLOCK_HEADER;
                readingBlockSize = 1;  // Следующий блок начинается с размера
                g_progState.bufferIndex = 0;

                printf("Block completed. Total bytes in sector: %d\n",
                    g_progState.bufferIndex);
            }
        }
    }
}