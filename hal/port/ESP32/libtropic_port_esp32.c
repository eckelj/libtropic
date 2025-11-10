// SPDX-License-Identifier: MIT
// ESP32-P4 port for libtropic lt_port_* HAL (SPI + RNG + delays)
// Matches STM32 signatures so you can link libtropic without changes.

#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* libtropic headers (provided by your tree) */
#include "libtropic_common.h"
#include "libtropic_port.h"     // lt_handle_t, TR01_L1_LEN_MAX, lt_ret_t, LT_OK, etc.

#define TAG "libtropic_port_esp32"

/* ──────────────────── Wiring / SPI config (edit if needed) ────────────────────
   These are set to the same pins + mode as your working ESP32-P4 Tropic01 demo.
*/
#ifndef LT_ESP_SPI_HOST
#define LT_ESP_SPI_HOST   SPI2_HOST
#endif

#ifndef LT_ESP_SPI_MOSI
#define LT_ESP_SPI_MOSI   23
#endif
#ifndef LT_ESP_SPI_MISO
#define LT_ESP_SPI_MISO   22
#endif
#ifndef LT_ESP_SPI_SCLK
#define LT_ESP_SPI_SCLK   21
#endif
#ifndef LT_ESP_SPI_CS
#define LT_ESP_SPI_CS     20   // manual CS controlled via GPIO
#endif

#ifndef LT_ESP_SPI_MODE
#define LT_ESP_SPI_MODE   0    // CPOL=0, CPHA=0 (as in your demo)
#endif

#ifndef LT_ESP_SPI_CLOCK_HZ
#define LT_ESP_SPI_CLOCK_HZ  1000000   // 1 MHz to start; raise once stable
#endif

/* If you wired an INT pin and want wait-on-int semantics, set this to 1 and define LT_ESP_INT_IO */
#ifndef LT_USE_INT_PIN
#define LT_USE_INT_PIN   0
#endif
#if LT_USE_INT_PIN
#ifndef LT_ESP_INT_IO
#define LT_ESP_INT_IO     32
#endif
#endif

/* ───────────────────────────── Globals ───────────────────────────── */
static spi_device_handle_t s_dev = NULL;
static bool s_bus_owned = false;

/* tiny helpers like your demo */
static inline void udelay(uint32_t us) { esp_rom_delay_us(us); }
static inline void cs_low(void){ gpio_set_level(LT_ESP_SPI_CS, 0); udelay(2);}
static inline void cs_high(void){ gpio_set_level(LT_ESP_SPI_CS, 1); udelay(2);}

/* ───────────────────────── RNG (matches STM32 API) ─────────────────────────
 * STM32: lt_port_random_bytes(uint32_t *buff, uint16_t len) returns len 32-bit words.
 */

 lt_ret_t lt_port_random_bytes(lt_l2_state_t *s2, void *buff, size_t count)
{
    if (!buff || count == 0) return LT_OK;
    for (size_t i = 0; i < count; i++) {
        ((uint8_t *)buff)[i] = esp_random();  // HW RNG-backed
    }
    return LT_OK;
}

/* ───────────────────────── CS control ───────────────────────── */
lt_ret_t lt_port_spi_csn_low(lt_l2_state_t *s2)
{
    LT_UNUSED(s2);
    cs_low();
    return LT_OK;
}

lt_ret_t lt_port_spi_csn_high(lt_l2_state_t *s2)
{
    LT_UNUSED(s2);
    cs_high();
    return LT_OK;
}

/* ───────────────────────── Init / Deinit ───────────────────────── */
lt_ret_t lt_port_init(lt_l2_state_t *s2)
{
    LT_UNUSED(s2);

    // CS pin as OUTPUT with pull-up (manual chip select control)
    gpio_config_t io_int = {  .pin_bit_mask = (1ULL << LT_ESP_SPI_CS), .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    if (gpio_config(&io_int) != ESP_OK) return LT_FAIL;
    
    // Configure MISO with pull-up for better signal integrity
    gpio_config_t miso_conf = { .pin_bit_mask=(1ULL<<LT_ESP_SPI_MISO), .mode=GPIO_MODE_INPUT,
                                .pull_up_en=GPIO_PULLUP_ENABLE, .pull_down_en=GPIO_PULLDOWN_DISABLE,
                                .intr_type=GPIO_INTR_DISABLE };
    gpio_config(&miso_conf);


    cs_high();

    // SPI bus
    spi_bus_config_t buscfg = {
        .mosi_io_num = LT_ESP_SPI_MOSI,
        .miso_io_num = LT_ESP_SPI_MISO,
        .sclk_io_num = LT_ESP_SPI_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 512, // TODO: TR01_L1_LEN_MAX,  // matches your libtropic L1 buffer
        .flags=SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS
    };

    // Attach device with manual CS
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = LT_ESP_SPI_CLOCK_HZ,
        .mode = LT_ESP_SPI_MODE,
        .spics_io_num = -1,       // manual CS
        .queue_size = 1,
        .flags = 0,
        .duty_cycle_pos = 128,    // 50%
        .cs_ena_pretrans = 0,
        .cs_ena_posttrans = 0
    };


    spi_bus_free(LT_ESP_SPI_HOST);
    esp_err_t er = spi_bus_initialize(LT_ESP_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (er == ESP_ERR_INVALID_STATE) {
        // Bus already initialized by someone else; don't free on deinit
        s_bus_owned = false;
    } else if (er == ESP_OK) {
        s_bus_owned = true;
    } else {
        return LT_L1_SPI_ERROR;
    }

    if (spi_bus_add_device(LT_ESP_SPI_HOST, &devcfg, &s_dev) != ESP_OK) {
        if (s_bus_owned) spi_bus_free(LT_ESP_SPI_HOST);
        s_dev = NULL;
        return LT_L1_SPI_ERROR;
    }
    ESP_EARLY_LOGI(TAG, "SPI ready host=%d mode=%d clk=%dHz mosi=%d miso=%d sclk=%d cs=%d",
                   LT_ESP_SPI_HOST, LT_ESP_SPI_MODE, LT_ESP_SPI_CLOCK_HZ,
                   LT_ESP_SPI_MOSI, LT_ESP_SPI_MISO, LT_ESP_SPI_SCLK, LT_ESP_SPI_CS);

    return LT_OK;
}

lt_ret_t lt_port_deinit(lt_l2_state_t *s2)
{
    LT_UNUSED(s2);

    if (s_dev) {
        spi_bus_remove_device(s_dev);
        s_dev = NULL;
    }
    if (s_bus_owned) {
        spi_bus_free(LT_ESP_SPI_HOST);
        s_bus_owned = false;
    }
    return LT_OK;
}

/* ───────────────────────── SPI transfer ─────────────────────────
 * Matches STM32 behavior:
 * - Full-duplex in-place over s2->buff[offset .. offset+tx_len).
 * - Blocks until complete. 'timeout_ms' not used (ESP-IDF call is blocking).
 */
lt_ret_t lt_port_spi_transfer(lt_l2_state_t *s2, uint8_t offset, uint16_t tx_len, uint32_t timeout_ms)
{
    LT_UNUSED(timeout_ms);

    if (!s2 || !s_dev) {
        ESP_LOGE(TAG, "SPI transfer: invalid state (s2=%p, s_dev=%p)", s2, s_dev);
        return LT_L1_SPI_ERROR;
    }

    ESP_LOGI(TAG, "SPI transfer: offset=%u, tx_len=%u, TR01_L1_LEN_MAX=%d", 
             offset, tx_len, TR01_L1_LEN_MAX);

    if ((size_t)offset + (size_t)tx_len > TR01_L1_LEN_MAX) {
        ESP_LOGE(TAG, "SPI transfer: length error (offset=%u + tx_len=%u = %u > max=%d)", 
                 offset, tx_len, offset + tx_len, TR01_L1_LEN_MAX);
        return LT_L1_DATA_LEN_ERROR;
    }

    uint8_t *buf = s2->buff + offset;

    // Use a TX shadow to avoid overwriting TX data before RX completes
    uint8_t *tx_shadow = (uint8_t*)alloca(tx_len);
    memcpy(tx_shadow, buf, tx_len);

    // Debug: print what we're sending
    printf("SPI TX[%u]: ", tx_len);
    for (int i = 0; i < tx_len && i < 16; i++) {
        printf("%02X ", tx_shadow[i]);
    }
    printf("\r\n");

    spi_transaction_t t = {
        .length = (size_t)tx_len * 8,
        .tx_buffer = tx_shadow,
        .rx_buffer = buf
    };

    // Note: CS is controlled by L1 layer via lt_l1_spi_csn_low/high calls
    // Do NOT toggle CS here or it will break the transaction
    esp_err_t er = spi_device_transmit(s_dev, &t);

    // Debug: print what we received
    printf("SPI RX[%u]: ", tx_len);
    for (int i = 0; i < tx_len && i < 16  && buf != NULL ; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\r\n");

    if (er != ESP_OK) {
        return LT_L1_SPI_ERROR;
    }
    return LT_OK;
}

/* ───────────────────────── Delays ───────────────────────── */
lt_ret_t lt_port_delay(lt_l2_state_t *s2, uint32_t ms)
{
    LT_UNUSED(s2);
    vTaskDelay(pdMS_TO_TICKS(ms));
    return LT_OK;
}

#if LT_USE_INT_PIN
/* Optional: wait until INT is high, or timeout (ms) */
lt_ret_t lt_port_delay_on_int(lt_l2_state_t *s2, uint32_t ms)
{
    LT_UNUSED(s2);
    int64_t start = esp_timer_get_time();
    int64_t deadline = start + (int64_t)ms * 1000;
    while (esp_timer_get_time() < deadline) {
        if (gpio_get_level(LT_ESP_INT_IO)) return LT_OK;
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    return LT_L1_INT_TIMEOUT;
}
#endif
