#pragma once


void initFirmwareDownload(void);
void processCANFirmwareData(uint8_t* rx_buff_from_can, uint16_t length);

void processCANFirmwareData_mod(uint8_t* rx_buff_from_can, uint16_t length);
void initFirmwareDownload_mod(void);