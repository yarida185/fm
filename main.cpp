// RDA5807 I2C-slave emulator for BBC micro:bit V2 (nRF52833)
//
// Цель: заставить прошивку (например, Bruce FW), которая ищет FM-чип
// RDA5807 по I2C, "увидеть" его — micro:bit отвечает как этот чип.
//
// ВАЖНО:
// - Работает ТОЛЬКО на micro:bit V2 (nRF52833 имеет аппаратный I2C-slave —
//   периферию TWIS). На V1 (nRF51822) это нереализуемо.
// - Значения битов регистра ENABLE/INTENSET взяты по документации серии
//   nRF52 (TWIS = 9 для ENABLE, WRITE=бит25, READ=бит26 в INTENSET/EVENTS).
//   Перед прошивкой имеет смысл свериться с Product Specification вашего
//   ревизии чипа — на некоторых ревизиях SDK/датащитов номера бит могут
//   отличаться на 1-2 позиции.
// - EasyDMA (TWIS использует его) требует, чтобы буферы TXD/RXD лежали в
//   обычной RAM (не во Flash) — здесь это учтено (статические массивы).
//
// Сборка: это shim-стиль MakeCode C++ extension. Экспортированная функция
// startEmulator() появится в MakeCode как блок после импорта расширения.

#include "pxt.h"
#include "nrf.h"

using namespace pxt;

namespace rda5807emulator {

// ---- Пины (можно поменять под вашу разводку) ----
#define EMU_SDA_PIN   8   // P8
#define EMU_SCL_PIN  12   // P12

// ---- Буфер, который "читает" мастер (Bruce/CYD) командой Wire.read() ----
// Раскладка условно соответствует статус-регистрам RDA5807 0x0A..0x0F,
// начиная с этого адреса при последовательном чтении.
// 0x0A: STATUSREG  (biti: RDSR, STC, SF, RDSS, BLERA, ST ...)
//   Здесь выставляем STC=1 (tune complete), SF=0 (seek fail = false),
//   ST=1 (stereo) — так библиотека не будет "висеть" в цикле ожидания.
static volatile uint8_t txBuf[12] = {
    0x40, 0x00,   // 0x0A hi,lo  -> STC=1 (бит6 старшего байта)
    0x28, 0x00,   // 0x0B RSSI~40 в старших битах (не критично, для правдоподобия)
    0x00, 0x00,   // 0x0C RDS Block A
    0x00, 0x00,   // 0x0D RDS Block B
    0x00, 0x00,   // 0x0E RDS Block C
    0x00, 0x00    // 0x0F RDS Block D
};

// Буфер, куда мастер ПИШЕТ (регистры 0x02-0x07) — содержимое нам не важно,
// просто должно быть куда принять байты, иначе TWIS не подтвердит запись.
static volatile uint8_t rxBuf[16];

static void twis0_setup() {
    // Отключаем перед конфигурацией
    NRF_TWIS0->ENABLE = 0;

    NRF_TWIS0->PSEL.SCL = EMU_SCL_PIN;
    NRF_TWIS0->PSEL.SDA = EMU_SDA_PIN;

    // Слушаем оба адреса RDA5807, чтобы не зависеть от того, какой из них
    // пробует конкретная библиотека
    NRF_TWIS0->ADDRESS[0] = 0x10; // sequential access
    NRF_TWIS0->ADDRESS[1] = 0x11; // random/indexed access
    NRF_TWIS0->CONFIG = (1 << 0) | (1 << 1); // ADDRESS0 + ADDRESS1 enabled

    NRF_TWIS0->TXD.PTR    = (uint32_t)txBuf;
    NRF_TWIS0->TXD.MAXCNT = sizeof(txBuf);

    NRF_TWIS0->RXD.PTR    = (uint32_t)rxBuf;
    NRF_TWIS0->RXD.MAXCNT = sizeof(rxBuf);

    // Разрешаем прерывания по событиям WRITE (нам пишут) и READ (у нас читают)
    NRF_TWIS0->INTENSET = (1 << 25) | (1 << 26); // WRITE, READ

    NVIC_ClearPendingIRQ(SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQn);
    NVIC_SetPriority(SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQn, 3);
    NVIC_EnableIRQ(SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQn);

    // Готовим приём ДО включения — иначе первая транзакция может быть NACK'нута
    NRF_TWIS0->TASKS_PREPARERX = 1;

    NRF_TWIS0->ENABLE = 9; // 9 = Enabled для TWIS (см. datasheet ENABLE reg)
}

extern "C" void SPIM0_SPIS0_TWIM0_TWIS0_SPI0_TWI0_IRQHandler(void) {
    if (NRF_TWIS0->EVENTS_WRITE) {
        NRF_TWIS0->EVENTS_WRITE = 0;
        // Мастер что-то записал (симулируем прием регистров 0x02-0x07).
        // Содержимое игнорируем — просто готовим буфер под следующий приём.
        NRF_TWIS0->TASKS_PREPARERX = 1;
    }

    if (NRF_TWIS0->EVENTS_READ) {
        NRF_TWIS0->EVENTS_READ = 0;
        // Мастер запрашивает чтение — отдаём наш "статус" чипа.
        NRF_TWIS0->TXD.PTR    = (uint32_t)txBuf;
        NRF_TWIS0->TXD.MAXCNT = sizeof(txBuf);
        NRF_TWIS0->TASKS_PREPARETX = 1;
    }
}

/**
 * Запустить эмуляцию RDA5807 по I2C (SDA=P8, SCL=P12).
 * Вызывать один раз в on start.
 */
//% blockId=rda5807emu_start block="start RDA5807 emulator"
void startEmulator() {
    twis0_setup();
}

}
