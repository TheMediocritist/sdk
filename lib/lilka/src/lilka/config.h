#ifndef LILKA_CONFIG_H
#define LILKA_CONFIG_H

#include <stdint.h>

#if LILKA_VERSION == -1

// Lilka on a breadboard (C3)

#    define LILKA_GPIO_UP          4
#    define LILKA_GPIO_DOWN        7
#    define LILKA_GPIO_LEFT        -1
#    define LILKA_GPIO_RIGHT       -1
#    define LILKA_GPIO_SELECT      -1
#    define LILKA_GPIO_START       10
#    define LILKA_GPIO_A           -1
#    define LILKA_GPIO_B           -1
#    define LILKA_GPIO_C           -1
#    define LILKA_GPIO_D           -1

#    define LILKA_SPI_SCK          0
#    define LILKA_SPI_MOSI         1
#    define LILKA_SPI_MISO         8
#    define LILKA_DISPLAY_DC       3
#    define LILKA_DISPLAY_CS       21
#    define LILKA_DISPLAY_RST      2
#    define LILKA_DISPLAY_ROTATION 0
#    define LILKA_DISPLAY_WIDTH    240
#    define LILKA_DISPLAY_HEIGHT   280

#    define LILKA_SDCARD_CS        20

#elif LILKA_VERSION == 1

// Lilka v1 (C3), first prototype from pcb24.com.ua

#    define LILKA_GPIO_UP          4
#    define LILKA_GPIO_DOWN        7
#    define LILKA_GPIO_LEFT        5
#    define LILKA_GPIO_RIGHT       6
#    define LILKA_GPIO_SELECT      9
#    define LILKA_GPIO_START       10
#    define LILKA_GPIO_A           20
#    define LILKA_GPIO_B           21
#    define LILKA_GPIO_C           -1
#    define LILKA_GPIO_D           -1

#    define LILKA_DISPLAY_DC       2
#    define LILKA_DISPLAY_CS       3
#    define LILKA_SPI_SCK          8
#    define LILKA_SPI_MOSI         1
#    define LILKA_DISPLAY_RST      -1
#    define LILKA_DISPLAY_ROTATION 0
#    define LILKA_DISPLAY_WIDTH    240
#    define LILKA_DISPLAY_HEIGHT   280

#    define LILKA_SDCARD_CS        -1

#elif LILKA_VERSION == 2

// Lilka v2 (S3)

// Кнопки
#    define LILKA_GPIO_UP          38
#    define LILKA_GPIO_DOWN        41
#    define LILKA_GPIO_LEFT        39
#    define LILKA_GPIO_RIGHT       40
#    define LILKA_GPIO_SELECT      0 // Режим прошивання
#    define LILKA_GPIO_START       4
#    define LILKA_GPIO_A           5
#    define LILKA_GPIO_B           6
#    define LILKA_GPIO_C           10
#    define LILKA_GPIO_D           9
// Сон
#    define LILKA_SLEEP            46
// SPI
#    define LILKA_SPI_SCK          18
#    define LILKA_SPI_MOSI         17
#    define LILKA_SPI_MISO         8
// Дисплей
#    define LILKA_DISPLAY_DC       15
#    define LILKA_DISPLAY_CS       7
#    define LILKA_DISPLAY_RST      -1
#    define LILKA_DISPLAY_ROTATION 3
#    define LILKA_DISPLAY_WIDTH    240 // Display dimensions in unrotated state
#    define LILKA_DISPLAY_HEIGHT   280 // (will be adjusted by rotation inside Arduino_GFX)
// uSD-карта
#    define LILKA_SDCARD_CS        16
// uSD-картка на роз'ємі розширення
#    ifdef USE_EXT_SPI_FOR_SD
#        define SPI2_SCK     21
#        define SPI2_MISO    14
#        define SPI2_MOSI    47
#        define SPI2_DEV1_CS 48 // Chip Select для пристрою 1
#    endif
// Рівень батареї
#    define LILKA_BATTERY_ADC            3
#    define LILKA_BATTERY_ADC_FUNC(name) adc1_##name
#    define LILKA_BATTERY_ADC_CHANNEL    ADC1_CHANNEL_2
// Buzzer
#    define LILKA_BUZZER                 11
// I2S
#    define LILKA_I2S_BCLK               42
#    define LILKA_I2S_DOUT               2
#    define LILKA_I2S_LRCK               1
// Роз'єм розширення
#    define LILKA_P3                     48
#    define LILKA_P4                     47
#    define LILKA_P5                     21
#    define LILKA_P6                     14 // ADC2, CH3
#    define LILKA_P7                     13 // ADC2, CH2
#    define LILKA_P8                     12 // ADC2, CH1
const uint8_t LILKA_EXT_PINS[] = {LILKA_P3, LILKA_P4, LILKA_P5, LILKA_P6, LILKA_P7, LILKA_P8};

#elif LILKA_VERSION == 3

// Waveshare ESP32-S3-RLCD-4.2 (ST7305 400x300 mono RLCD) - personal fork
// Board facts (verified on hardware): 8MB usable flash, DIO mode, dio_opi,
// embedded 8MB octal PSRAM (AP_3v3). Factory bootloader REQUIRED
// (arduino-esp32 prebuilt bootloader boot-loops on this chip revision).

// Кнопки: phase 1 = only the two on-board keys. Wire the rest later
// (external buttons or TCA9554 I2C expander on the existing I2C bus).
#    define LILKA_GPIO_UP          -1
#    define LILKA_GPIO_DOWN        18 // on-board KEY
#    define LILKA_GPIO_LEFT        -1
#    define LILKA_GPIO_RIGHT       -1
#    define LILKA_GPIO_SELECT      -1
#    define LILKA_GPIO_START       -1
#    define LILKA_GPIO_A           0 // on-board BOOT
#    define LILKA_GPIO_B           -1
#    define LILKA_GPIO_C           -1
#    define LILKA_GPIO_D           -1
// Сон (не підключено)
#    define LILKA_SLEEP            42
// SPI (не використовується: дисплей має власну шину, SD - це SDMMC).
// spi_begin() нічого не робить для v3.
#    define LILKA_SPI_SCK          -1
#    define LILKA_SPI_MOSI         -1
#    define LILKA_SPI_MISO         -1
// Дисплей (ST7305, окрема шина SPI3 через esp_lcd)
#    define LILKA_DISPLAY_MOSI     12
#    define LILKA_DISPLAY_SCK      11
#    define LILKA_DISPLAY_DC       5
#    define LILKA_DISPLAY_CS       40
#    define LILKA_DISPLAY_RST      41
#    define LILKA_DISPLAY_TE       6
#    define LILKA_DISPLAY_ROTATION 0
#    define LILKA_DISPLAY_WIDTH    400
#    define LILKA_DISPLAY_HEIGHT   300
// SD: тут SDMMC 1-bit (CLK 38, CMD 21, D0 39), НЕ SPI.
// CS < 0 -> fileutils gracefully skips SPI SD init. SD_MMC support: TODO.
#    define LILKA_SDCARD_CS        -1
#    define LILKA_SDMMC_CLK        38
#    define LILKA_SDMMC_CMD        21
#    define LILKA_SDMMC_D0         39
// I2C (PCF85063 RTC, SHTC3, ES8311/ES7210 codecs, майбутній TCA9554)
#    define LILKA_I2C_SDA          13
#    define LILKA_I2C_SCL          14
// Рівень батареї: ADC1 CH3 = GPIO4, дільник 1:3 (з прикладу Waveshare)
#    define LILKA_BATTERY_ADC            4
#    define LILKA_BATTERY_ADC_FUNC(name) adc1_##name
#    define LILKA_BATTERY_ADC_CHANNEL    ADC1_CHANNEL_3
// Buzzer: немає. Звук піде через кодек ES8311 (TODO).
#    define LILKA_BUZZER                 -1
// I2S -> ES8311 codec (без ініціалізації кодека - тиша, але компілюється)
#    define LILKA_I2S_BCLK               9
#    define LILKA_I2S_DOUT               8
#    define LILKA_I2S_LRCK               45
// Роз'єм розширення: вільні GPIO (перевірити з розпіновкою Waveshare)
#    define LILKA_P3                     1
#    define LILKA_P4                     2
#    define LILKA_P5                     3
#    define LILKA_P6                     7
#    define LILKA_P7                     15
#    define LILKA_P8                     17
const uint8_t LILKA_EXT_PINS[] = {LILKA_P3, LILKA_P4, LILKA_P5, LILKA_P6, LILKA_P7, LILKA_P8};

#else
#    error "LILKA_VERSION is not defined - did you forget to set board to lilka_v2 in your platformio.ini?"
#endif

#endif // LILKA_CONFIG_H
