#include "lora.h"
#include "config.h"
#include "tracker.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "LORA";

namespace {
constexpr gpio_num_t PIN_SCK  = static_cast<gpio_num_t>(LORA_PIN_SCK);
constexpr gpio_num_t PIN_MISO = static_cast<gpio_num_t>(LORA_PIN_MISO);
constexpr gpio_num_t PIN_MOSI = static_cast<gpio_num_t>(LORA_PIN_MOSI);
constexpr gpio_num_t PIN_NSS  = static_cast<gpio_num_t>(LORA_PIN_NSS);
constexpr gpio_num_t PIN_RST  = static_cast<gpio_num_t>(LORA_PIN_RST);

constexpr spi_host_device_t LORA_SPI_HOST = SPI2_HOST;

constexpr uint8_t REG_FIFO                 = 0x00;
constexpr uint8_t REG_OP_MODE              = 0x01;
constexpr uint8_t REG_FRF_MSB              = 0x06;
constexpr uint8_t REG_FRF_MID              = 0x07;
constexpr uint8_t REG_FRF_LSB              = 0x08;
constexpr uint8_t REG_PA_CONFIG            = 0x09;
constexpr uint8_t REG_LNA                  = 0x0C;
constexpr uint8_t REG_FIFO_ADDR_PTR        = 0x0D;
constexpr uint8_t REG_FIFO_TX_BASE_ADDR    = 0x0E;
constexpr uint8_t REG_FIFO_RX_BASE_ADDR    = 0x0F;
constexpr uint8_t REG_IRQ_FLAGS            = 0x12;
constexpr uint8_t REG_PAYLOAD_LENGTH       = 0x22;
constexpr uint8_t REG_MODEM_CONFIG_1       = 0x1D;
constexpr uint8_t REG_MODEM_CONFIG_2       = 0x1E;
constexpr uint8_t REG_PREAMBLE_MSB         = 0x20;
constexpr uint8_t REG_PREAMBLE_LSB         = 0x21;
constexpr uint8_t REG_MODEM_CONFIG_3       = 0x26;
constexpr uint8_t REG_SYNC_WORD            = 0x39;
constexpr uint8_t REG_VERSION              = 0x42;

constexpr uint8_t MODE_LONG_RANGE_MODE     = 0x80;
constexpr uint8_t MODE_SLEEP               = 0x00;
constexpr uint8_t MODE_STDBY               = 0x01;
constexpr uint8_t MODE_TX                  = 0x03;

constexpr uint8_t IRQ_TX_DONE_MASK         = 0x08;

spi_device_handle_t g_lora_spi = nullptr;
bool                g_lora_ready = false;
uint32_t            g_boot_seq = 0;

esp_err_t lora_spi_transfer(uint8_t reg, const uint8_t *tx, uint8_t *rx, size_t len, bool write)
{
    if (!g_lora_spi) return ESP_FAIL;

    spi_transaction_t t = {};
    t.length = (len + 1) * 8;

    uint8_t stack_buf[260] = {};
    if (len > sizeof(stack_buf) - 1) return ESP_ERR_INVALID_SIZE;

    stack_buf[0] = write ? static_cast<uint8_t>(reg | 0x80) : static_cast<uint8_t>(reg & 0x7F);
    if (write && tx && len > 0) memcpy(&stack_buf[1], tx, len);

    t.tx_buffer = stack_buf;
    t.rx_buffer = stack_buf;
    esp_err_t err = spi_device_transmit(g_lora_spi, &t);
    if (err != ESP_OK) return err;

    if (!write && rx && len > 0) memcpy(rx, &stack_buf[1], len);
    return ESP_OK;
}

esp_err_t lora_write_reg(uint8_t reg, uint8_t value)
{
    return lora_spi_transfer(reg, &value, nullptr, 1, true);
}

esp_err_t lora_read_reg(uint8_t reg, uint8_t *value)
{
    return lora_spi_transfer(reg, nullptr, value, 1, false);
}

esp_err_t lora_write_burst(uint8_t reg, const uint8_t *data, size_t len)
{
    return lora_spi_transfer(reg, data, nullptr, len, true);
}

esp_err_t lora_set_mode(uint8_t mode)
{
    return lora_write_reg(REG_OP_MODE, MODE_LONG_RANGE_MODE | mode);
}

void lora_reset_chip()
{
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

esp_err_t lora_send_packet(const char *payload)
{
    if (!g_lora_ready || !payload) return ESP_FAIL;

    const size_t len = strnlen(payload, 220);
    if (len == 0) return ESP_OK;

    ESP_ERROR_CHECK_WITHOUT_ABORT(lora_set_mode(MODE_STDBY));
    ESP_ERROR_CHECK_WITHOUT_ABORT(lora_write_reg(REG_FIFO_ADDR_PTR, 0x00));
    ESP_ERROR_CHECK_WITHOUT_ABORT(lora_write_burst(REG_FIFO, reinterpret_cast<const uint8_t *>(payload), len));
    ESP_ERROR_CHECK_WITHOUT_ABORT(lora_write_reg(REG_PAYLOAD_LENGTH, static_cast<uint8_t>(len)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(lora_write_reg(REG_IRQ_FLAGS, 0xFF));
    ESP_ERROR_CHECK_WITHOUT_ABORT(lora_set_mode(MODE_TX));

    const int64_t start_us = esp_timer_get_time();
    while (true) {
        uint8_t irq = 0;
        if (lora_read_reg(REG_IRQ_FLAGS, &irq) == ESP_OK && (irq & IRQ_TX_DONE_MASK)) {
            break;
        }
        if ((esp_timer_get_time() - start_us) > 1000000LL) {
            ESP_LOGW(TAG, "TX timeout");
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(lora_write_reg(REG_IRQ_FLAGS, 0xFF));
    ESP_ERROR_CHECK_WITHOUT_ABORT(lora_set_mode(MODE_STDBY));
    return ESP_OK;
}
}

void lora_init(void)
{
    gpio_config_t rst_cfg = {};
    rst_cfg.pin_bit_mask = (1ULL << PIN_RST);
    rst_cfg.mode = GPIO_MODE_OUTPUT;
    rst_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    rst_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rst_cfg.intr_type = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&rst_cfg));
    gpio_set_level(PIN_RST, 1);

    spi_bus_config_t bus_cfg = {};
    bus_cfg.sclk_io_num = PIN_SCK;
    bus_cfg.mosi_io_num = PIN_MOSI;
    bus_cfg.miso_io_num = PIN_MISO;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 256;
    ESP_ERROR_CHECK(spi_bus_initialize(LORA_SPI_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev_cfg = {};
    dev_cfg.clock_speed_hz = 2 * 1000 * 1000;
    dev_cfg.mode = 0;
    dev_cfg.spics_io_num = PIN_NSS;
    dev_cfg.queue_size = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(LORA_SPI_HOST, &dev_cfg, &g_lora_spi));

    lora_reset_chip();

    uint8_t version = 0;
    ESP_ERROR_CHECK(lora_read_reg(REG_VERSION, &version));
    if (version != 0x12) {
        ESP_LOGE(TAG, "SX127x not found, REG_VERSION=0x%02X", version);
        return;
    }

    ESP_ERROR_CHECK(lora_set_mode(MODE_SLEEP));

    const uint64_t frf = (static_cast<uint64_t>(LORA_FREQ_HZ) << 19) / 32000000ULL;
    ESP_ERROR_CHECK(lora_write_reg(REG_FRF_MSB, static_cast<uint8_t>((frf >> 16) & 0xFF)));
    ESP_ERROR_CHECK(lora_write_reg(REG_FRF_MID, static_cast<uint8_t>((frf >> 8) & 0xFF)));
    ESP_ERROR_CHECK(lora_write_reg(REG_FRF_LSB, static_cast<uint8_t>(frf & 0xFF)));

    const uint8_t modem_cfg_1 = static_cast<uint8_t>(((LORA_BW_INDEX & 0x0F) << 4) | (((LORA_CR_DENOM - 4) & 0x07) << 1));
    const uint8_t modem_cfg_2 = static_cast<uint8_t>(((LORA_SF & 0x0F) << 4) | 0x04);
    ESP_ERROR_CHECK(lora_write_reg(REG_MODEM_CONFIG_1, modem_cfg_1));
    ESP_ERROR_CHECK(lora_write_reg(REG_MODEM_CONFIG_2, modem_cfg_2));
    ESP_ERROR_CHECK(lora_write_reg(REG_MODEM_CONFIG_3, 0x04));

    ESP_ERROR_CHECK(lora_write_reg(REG_PREAMBLE_MSB, 0x00));
    ESP_ERROR_CHECK(lora_write_reg(REG_PREAMBLE_LSB, 0x08));
    ESP_ERROR_CHECK(lora_write_reg(REG_SYNC_WORD, LORA_SYNC_WORD));

    ESP_ERROR_CHECK(lora_write_reg(REG_PA_CONFIG, 0x8F));
    ESP_ERROR_CHECK(lora_write_reg(REG_LNA, 0x23));

    ESP_ERROR_CHECK(lora_write_reg(REG_FIFO_TX_BASE_ADDR, 0x00));
    ESP_ERROR_CHECK(lora_write_reg(REG_FIFO_RX_BASE_ADDR, 0x00));
    ESP_ERROR_CHECK(lora_set_mode(MODE_STDBY));

    g_lora_ready = true;
    ESP_LOGW(TAG, "LoRa ready on %lu Hz (v=0x%02X)", (unsigned long)LORA_FREQ_HZ, version);

    char boot_msg[120];
    snprintf(boot_msg, sizeof(boot_msg), "{\"node\":\"node_a\",\"type\":\"boot\",\"seq\":%lu}", (unsigned long)g_boot_seq++);
    lora_send_packet(boot_msg);
}

void lora_publish_counts(void)
{
    if (!g_lora_ready) return;
    char buf[160];
    xSemaphoreTake(counter_mutex, portMAX_DELAY);
    const unsigned long car = (unsigned long)vehicle_counts[CLASS_CAR];
    const unsigned long moto = (unsigned long)vehicle_counts[CLASS_MOTORCYCLE];
    xSemaphoreGive(counter_mutex);

    snprintf(buf, sizeof(buf),
             "{\"node\":\"node_a\",\"type\":\"counts\",\"seq\":%lu,\"car\":%lu,\"motorcycle\":%lu,\"total\":%lu}",
             (unsigned long)g_boot_seq++, car, moto, car + moto);
    lora_send_packet(buf);
}

void lora_publish_status(void)
{
    if (!g_lora_ready) return;
    char buf[192];
    snprintf(buf, sizeof(buf),
             "{\"node\":\"node_a\",\"type\":\"status\",\"seq\":%lu,\"fps\":%.1f,\"heap\":%lu,\"tracking\":%d}",
             (unsigned long)g_boot_seq++,
             current_fps,
             (unsigned long)esp_get_free_heap_size(),
             current_tracking);
    lora_send_packet(buf);
}

void lora_test_ping_loop(void)
{
    uint32_t seq = 0;
    ESP_LOGW(TAG, "=== LoRa TX test loop started ===");

    uint8_t reg_val = 0;
    lora_read_reg(REG_OP_MODE, &reg_val);
    ESP_LOGW(TAG, "  REG_OP_MODE   = 0x%02X", reg_val);
    lora_read_reg(REG_PA_CONFIG, &reg_val);
    ESP_LOGW(TAG, "  REG_PA_CONFIG = 0x%02X", reg_val);
    lora_read_reg(REG_MODEM_CONFIG_1, &reg_val);
    ESP_LOGW(TAG, "  REG_MODEM_CFG1= 0x%02X", reg_val);
    lora_read_reg(REG_MODEM_CONFIG_2, &reg_val);
    ESP_LOGW(TAG, "  REG_MODEM_CFG2= 0x%02X", reg_val);
    lora_read_reg(REG_FRF_MSB, &reg_val);
    ESP_LOGW(TAG, "  REG_FRF_MSB   = 0x%02X", reg_val);
    lora_read_reg(REG_FRF_MID, &reg_val);
    ESP_LOGW(TAG, "  REG_FRF_MID   = 0x%02X", reg_val);
    lora_read_reg(REG_FRF_LSB, &reg_val);
    ESP_LOGW(TAG, "  REG_FRF_LSB   = 0x%02X", reg_val);

    while (true) {
        char buf[128];
        snprintf(buf, sizeof(buf),
                 "{\"node\":\"node_a\",\"type\":\"ping\",\"seq\":%lu,\"heap\":%lu}",
                 (unsigned long)seq,
                 (unsigned long)esp_get_free_heap_size());

        ESP_LOGW(TAG, "TX [%lu]: %s", (unsigned long)seq, buf);
        esp_err_t err = lora_send_packet(buf);
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "TX [%lu] done", (unsigned long)seq);
        } else {
            ESP_LOGE(TAG, "TX [%lu] FAILED err=%d", (unsigned long)seq, err);
        }
        seq++;
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
