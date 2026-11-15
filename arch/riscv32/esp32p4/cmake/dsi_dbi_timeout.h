#pragma once

#include "soc/mipi_dsi_host_reg.h"

static esp_err_t a3_dbi_wait(dsi_host_dev_t *host, uint32_t flags, TickType_t started)
{
    const TickType_t timeout = pdMS_TO_TICKS(100) + 1;
    while ((TickType_t)(xTaskGetTickCount() - started) < timeout) {
        if (!(host->cmd_pkt_status.val & flags)) {
            return ESP_OK;
        }
        vTaskDelay(1);
    }
    return ESP_ERR_TIMEOUT;
}

static esp_err_t a3_dbi_header(dsi_host_dev_t *host, uint8_t vc, mipi_dsi_data_type_t type,
                             uint16_t data, TickType_t started)
{
    ESP_RETURN_ON_ERROR(a3_dbi_wait(host, DSI_HOST_GEN_CMD_FULL, started), TAG, "DSI command FIFO timed out");
    mipi_dsi_host_ll_gen_set_packet_header(host, vc, type, data >> 8, (uint8_t)data);
    return ESP_OK;
}

static esp_err_t a3_dbi_write(dsi_host_dev_t *host, uint8_t vc, uint32_t command, unsigned command_bytes,
                            const void *param, size_t param_size)
{
    if (!command_bytes || command_bytes > sizeof(command) || param_size > UINT16_MAX - command_bytes
            || (param_size && !param)) {
        return ESP_ERR_INVALID_ARG;
    }
    const uint8_t *bytes = param;
    size_t size = command_bytes + param_size;
    TickType_t started = xTaskGetTickCount();
    for (size_t offset = 0; offset < size;) {
        uint32_t word = 0;
        for (unsigned i = 0; i < sizeof(word) && offset < size; ++i, ++offset) {
            uint8_t byte = offset < command_bytes ? (uint8_t)(command >> (8 * offset))
                                                : bytes[offset - command_bytes];
            word |= (uint32_t)byte << (8 * i);
        }
        if (size <= 2) {
            mipi_dsi_data_type_t type = size == 1 ? MIPI_DSI_DT_DCS_SHORT_WRITE_0 : MIPI_DSI_DT_DCS_SHORT_WRITE_1;
            return a3_dbi_header(host, vc, type, (uint16_t)word, started);
        }
        ESP_RETURN_ON_ERROR(a3_dbi_wait(host, DSI_HOST_GEN_PLD_W_FULL, started), TAG, "DSI write payload FIFO timed out");
        mipi_dsi_host_ll_gen_write_payload_fifo(host, word);
    }
    return a3_dbi_header(host, vc, MIPI_DSI_DT_DCS_LONG_WRITE, (uint16_t)size, started);
}

static esp_err_t a3_dbi_read(dsi_host_dev_t *host, uint8_t vc, uint32_t command, unsigned command_bytes,
                           void *param, size_t param_size)
{
    if (!command_bytes || command_bytes > 2 || !param || !param_size || param_size > UINT16_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_FALSE(mipi_dsi_host_ll_gen_is_read_fifo_empty(host), ESP_ERR_INVALID_RESPONSE, TAG,
                        "DSI read FIFO contains a previous response");
    TickType_t started = xTaskGetTickCount();
    bool video_mode = !host->mode_cfg.cmd_video_mode;
    mipi_dsi_host_ll_enable_video_mode(host, false);
    mipi_dsi_host_ll_enable_bta(host, true);
    mipi_dsi_host_ll_gen_set_rx_vcid(host, vc);
    ESP_RETURN_ON_ERROR(a3_dbi_header(host, vc, MIPI_DSI_DT_SET_MAXIMUM_RETURN_PKT,
                                     (uint16_t)param_size, started), TAG, "DSI maximum return packet failed");
    uint16_t data = command_bytes == 1 ? (uint8_t)command : (uint16_t)command;
    ESP_RETURN_ON_ERROR(a3_dbi_header(host, vc, MIPI_DSI_DT_DCS_READ_0, data, started), TAG, "DSI read request failed");

    uint8_t *bytes = param;
    for (size_t remaining = param_size; remaining;) {
        ESP_RETURN_ON_ERROR(a3_dbi_wait(host, DSI_HOST_GEN_RD_CMD_BUSY | DSI_HOST_GEN_PLD_R_EMPTY, started),
                            TAG, "DSI read response timed out for command 0x%x", data);
        uint32_t word = mipi_dsi_host_ll_gen_read_payload_fifo(host);
        for (unsigned i = 0; i < sizeof(word) && remaining; ++i, --remaining) {
            *bytes++ = (uint8_t)(word >> (8 * i));
        }
    }
    ESP_RETURN_ON_FALSE(mipi_dsi_host_ll_gen_is_read_fifo_empty(host), ESP_ERR_INVALID_RESPONSE, TAG,
                        "DSI read FIFO contains excess response data");
    // Keep BTA enabled for command ACKs. The A3 caller destroys the bus after a failed read.
    mipi_dsi_host_ll_enable_video_mode(host, video_mode);
    return ESP_OK;
}
