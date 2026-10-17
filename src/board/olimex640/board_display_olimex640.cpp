/*
 * board_display_olimex640.cpp - Olimex MIPI-LCD2.8-640x480 (ST7701S) bring-up
 * for the Waveshare ESP32-P4-WIFI6 board.
 *
 * Unlike the 10.1" target (which drives an on-board JD9365 panel through the
 * vendored Waveshare BSP's bsp_display_new()), this target attaches a
 * separate Olimex 2.8" MIPI-DSI module with an ST7701S controller. The
 * panel is brought up directly with the espressif/esp_lcd_st7701 managed
 * component, mirroring the ESP-IDF ST7701 example and the standalone
 * esp32st7701s reference:
 *
 *   1. Acquire the on-chip LDO that powers the MIPI D-PHY (channel 3, 2.5 V)
 *   2. Create a single-lane MIPI-DSI bus @ 500 Mbps
 *   3. Attach the ST7701S panel (DBI command IO + DPI video path) and send
 *      its vendor initialization sequence
 *   4. Grab the DPI controller's PSRAM framebuffer and hand it to the shared
 *      RGB565 surface / tile pipeline
 *
 * The module is physically portrait 480x640. The shared surface layer
 * presents it as a 640x480 landscape frame via the same 90-degree rotation
 * model used on the Waveshare 10.1" panel, so MiniGfx and the video tile
 * writers need no board-specific changes.
 *
 * The module has no separately controllable reset or backlight pin, so
 * BoardDisplay_SetBacklight() and BoardDisplay_SetFlip180() are harmless
 * no-ops here.
 */

#include "board_display.h"
#include "board_config.h"

#include "mini_gfx/mini_gfx.h"
#include "board_display_surface.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_ldo_regulator.h"
#include "esp_lcd_mipi_dsi.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st7701.h"

static const char *TAG = "board_display";

/* Panel is 480x640 portrait natively - physical module dimensions. */
static constexpr int PANEL_W = 480;
static constexpr int PANEL_H = 640;

/* Video timing for the 480x640 @ 60 Hz mode (matches the Olimex module's
 * datasheet and the esp32st7701s reference). */
static constexpr int LCD_DPI_CLK_MHZ = 16;
static constexpr int LCD_LANE_MBPS   = 500;
static constexpr int LCD_HSYNC       = 4;
static constexpr int LCD_HBP         = 20;
static constexpr int LCD_HFP         = 10;
static constexpr int LCD_VSYNC       = 4;
static constexpr int LCD_VBP         = 14;
static constexpr int LCD_VFP         = 8;

/* MIPI D-PHY power: on-chip LDO channel 3 at 2.5 V. */
static constexpr int MIPI_PHY_LDO_CH   = 3;
static constexpr int MIPI_PHY_LDO_MV   = 2500;

/* ST7701S vendor initialization sequence for the Olimex 2.8" module.
 * Sourced from the esp32st7701s reference (Olimex WLK2802MIPI profile).
 * The BK3/BK0/BK1 Command2 blocks configure gamma, power, and MIPI
 * parameters; the trailing BK0-select returns the controller to normal
 * command mode. */
static const st7701_lcd_init_cmd_t lcd_init_cmds[] = {
    // Command2 BK3 Selection: Enable the BK function of Command2
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x13}, 5, 0},
    // Unknown
    {0xEF, (uint8_t []){0x08}, 1, 0},

    // Command2 BK0 Selection: Enable the BK function of Command2
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x10}, 5, 0},
    // Display Line Setting
    {0xC0, (uint8_t []){0x4f, 0x00}, 2, 0},
    // Porch Control
    {0xC1, (uint8_t []){0x10, 0x0c}, 2, 0},
    // Inversion selection & Frame Rate Control
    {0xC2, (uint8_t []){0x07, 0x14}, 2, 0},
    // Unknown
    {0xCC, (uint8_t []){0x10}, 1, 0},
    // Positive Voltage Gamma Control
    {0xB0, (uint8_t []){0x0a, 0x18, 0x1e, 0x12, 0x16, 0x0c, 0x0e, 0x0d, 0x0c, 0x29, 0x06, 0x14, 0x13, 0x29, 0x33, 0x1c}, 16, 0},
    // Negative Voltage Gamma Control
    {0xB1, (uint8_t []){0x0a, 0x19, 0x21, 0x0a, 0x0c, 0x00, 0x0c, 0x03, 0x03, 0x23, 0x01, 0x0e, 0x0c, 0x27, 0x2b, 0x1c}, 16, 0},

    // Command2 BK1 Selection: Enable the BK function of Command2
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x11}, 5, 0},
    // Vop Amplitude setting
    {0xB0, (uint8_t []){0x5d}, 1, 0},
    // VCOM amplitude setting
    {0xB1, (uint8_t []){0x61}, 1, 0},
    // VGH Voltage setting
    {0xB2, (uint8_t []){0x84}, 1, 0},
    // TEST Command Setting
    {0xB3, (uint8_t []){0x80}, 1, 0},
    // VGL Voltage setting
    {0xB5, (uint8_t []){0x4d}, 1, 0},
    // Power Control 1
    {0xB7, (uint8_t []){0x85}, 1, 0},
    // Power Control 2
    {0xB8, (uint8_t []){0x20}, 1, 0},
    // Source pre_drive timing set1
    {0xC1, (uint8_t []){0x78}, 1, 0},
    // Source EQ2 Setting
    {0xC2, (uint8_t []){0x78}, 1, 0},
    // MIPI Setting 1
    {0xD0, (uint8_t []){0x88}, 1, 0},
    // GIP Code
    {0xE0, (uint8_t []){0x00, 0x00, 0x02}, 3, 0},
    {0xE1, (uint8_t []){0x06, 0xa0, 0x08, 0xa0, 0x05, 0xa0, 0x07, 0xa0, 0x00, 0x44, 0x44}, 11, 0},
    {0xE2, (uint8_t []){0x20, 0x20, 0x44, 0x44, 0x96, 0xa0, 0x00, 0x00, 0x96, 0xa0, 0x00, 0x00}, 12, 0},
    {0xE3, (uint8_t []){0x00, 0x00, 0x22, 0x22}, 4, 0},
    {0xE4, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE5, (uint8_t []){0x0d, 0x91, 0xa0, 0xa0, 0x0f, 0x93, 0xa0, 0xa0, 0x09, 0x8d, 0xa0, 0xa0, 0x0b, 0x8f, 0xa0, 0xa0}, 16, 0},
    {0xE6, (uint8_t []){0x00, 0x00, 0x22, 0x22}, 4, 0},
    {0xE7, (uint8_t []){0x44, 0x44}, 2, 0},
    {0xE8, (uint8_t []){0x0c, 0x90, 0xa0, 0xa0, 0x0e, 0x92, 0xa0, 0xa0, 0x08, 0x8c, 0xa0, 0xa0, 0x0a, 0x8e, 0xa0, 0xa0}, 16, 0},
    {0xE9, (uint8_t []){0x36, 0x00}, 2, 0},
    {0xEB, (uint8_t []){0x00, 0x01, 0xe4, 0xe4, 0x44, 0x88, 0x40}, 7, 0},
    {0xED, (uint8_t []){0xff, 0x45, 0x67, 0xfa, 0x01, 0x2b, 0xcf, 0xff, 0xff, 0xfc, 0xb2, 0x10, 0xaf, 0x76, 0x54, 0xff}, 16, 0},
    {0xEF, (uint8_t []){0x10, 0x0d, 0x04, 0x08, 0x3f, 0x1f}, 6, 0},

    // Command2 BK0 Selection: Disable the BK function of Command2
    {0xFF, (uint8_t []){0x77, 0x01, 0x00, 0x00, 0x00}, 5, 0},
};

static esp_lcd_panel_handle_t    s_panel    = nullptr;
static esp_lcd_panel_io_handle_t s_panel_io = nullptr;
static bool                      s_inited   = false;

extern "C" bool BoardDisplay_Init(void)
{
    if (s_inited) return true;

    ESP_LOGI(TAG, "Initializing Olimex 640x480 (ST7701S) display...");

    /* Power the MIPI D-PPHY from the on-chip LDO before the DSI bus is
     * created, matching the esp32st7701s reference and the Waveshare BSP. */
    esp_ldo_channel_handle_t phy_ldo = nullptr;
    const esp_ldo_channel_config_t ldo_config = {
        .chan_id = MIPI_PHY_LDO_CH,
        .voltage_mv = MIPI_PHY_LDO_MV,
    };
    esp_err_t err = esp_ldo_acquire_channel(&ldo_config, &phy_ldo);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MIPI D-PHY LDO (ch %d) acquire failed: %s",
                 MIPI_PHY_LDO_CH, esp_err_to_name(err));
        return false;
    }

    /* Single-lane MIPI-DSI bus. */
    esp_lcd_dsi_bus_handle_t dsi_bus = nullptr;
    const esp_lcd_dsi_bus_config_t bus_config = {
        .bus_id = 0,
        .num_data_lanes = 1,
        .lane_bit_rate_mbps = LCD_LANE_MBPS,
    };
    err = esp_lcd_new_dsi_bus(&bus_config, &dsi_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_dsi_bus failed: %s", esp_err_to_name(err));
        return false;
    }

    /* DBI command channel over the DSI bus (8-bit command / param). */
    const esp_lcd_dbi_io_config_t dbi_config = {
        .virtual_channel = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    err = esp_lcd_new_panel_io_dbi(dsi_bus, &dbi_config, &s_panel_io);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_io_dbi failed: %s", esp_err_to_name(err));
        return false;
    }

    /* DPI video configuration: 480x640 portrait @ 60 Hz, RGB565 so the
     * shared uint16_t surface and tile writers match the panel byte order. */
    const esp_lcd_dpi_panel_config_t dpi_config = {
        .virtual_channel = 0,
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,
        .dpi_clock_freq_mhz = LCD_DPI_CLK_MHZ,
        .pixel_format = LCD_COLOR_PIXEL_FORMAT_RGB565,
        .in_color_format = LCD_COLOR_FMT_RGB565,
        .out_color_format = LCD_COLOR_FMT_RGB565,
        .video_timing = {
            .h_size = PANEL_W,
            .v_size = PANEL_H,
            .hsync_pulse_width = LCD_HSYNC,
            .hsync_back_porch = LCD_HBP,
            .hsync_front_porch = LCD_HFP,
            .vsync_pulse_width = LCD_VSYNC,
            .vsync_back_porch = LCD_VBP,
            .vsync_front_porch = LCD_VFP,
        },
    };

    const st7701_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .mipi_config = { .dsi_bus = dsi_bus, .dpi_config = &dpi_config },
        .flags = { .use_mipi_interface = 1 },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };

    err = esp_lcd_new_panel_st7701(s_panel_io, &panel_config, &s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_new_panel_st7701 failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_lcd_panel_reset(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_reset failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_lcd_panel_init(s_panel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_init failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_lcd_panel_disp_sleep(s_panel, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_disp_sleep(false) failed: %s", esp_err_to_name(err));
        return false;
    }
    err = esp_lcd_panel_disp_on_off(s_panel, true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_lcd_panel_disp_on_off(true) failed: %s", esp_err_to_name(err));
        return false;
    }

    /* Grab the DPI controller's PSRAM framebuffer and attach the shared
     * RGB565 surface. The panel is portrait (PANEL_W x PANEL_H); the
     * surface layer rotates it to a 640x480 landscape logical frame. */
    void *dpi_fb = nullptr;
    err = esp_lcd_dpi_panel_get_frame_buffer(s_panel, 1, &dpi_fb);
    if (err != ESP_OK || !dpi_fb) {
        ESP_LOGE(TAG, "esp_lcd_dpi_panel_get_frame_buffer failed: %s", esp_err_to_name(err));
        return false;
    }
    if (!BoardDisplay_AttachSurface(dpi_fb, PANEL_W, PANEL_H)) return false;
    BoardDisplay_Present();

    s_inited = true;
    ESP_LOGI(TAG, "Display up: logical %dx%d, panel %dx%d",
             BOARD_DISPLAY_WIDTH, BOARD_DISPLAY_HEIGHT, PANEL_W, PANEL_H);
    return true;
}

extern "C" void BoardDisplay_SetBacklight(int percent)
{
    (void)percent;
    /* The module has no controllable backlight pin; the LED runs at a
     * fixed level. Accept and discard so callers keep working. */
}

/* The 2.8" panel's orientation is fixed by the ribbon location; accept the
 * request but do nothing so the boot GUI's Rotate-180 toggle is a harmless
 * no-op on this board. */
extern "C" void BoardDisplay_SetFlip180(bool flip)
{
    (void)flip;
}
