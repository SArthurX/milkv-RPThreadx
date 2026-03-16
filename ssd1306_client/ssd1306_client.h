#ifndef __SSD1306_CLIENT_H__
#define __SSD1306_CLIENT_H__

#include <stdint.h>

/* Display dimensions */
#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64

/* Font sizes */
#define SSD1306_FONT_SMALL  0
#define SSD1306_FONT_NORMAL 1

/* Shared memory structure - must match RTOS side */
typedef struct {
    uint8_t frame_buffer[1024];  // 128x64/8 = 1024 bytes
    uint32_t face_count;
    float fps;
    uint8_t reserved[12];        // Align to 1040 bytes
} __attribute__((packed)) ssd1306_display_data_t;

/* API Functions */

/**
 * Initialize SSD1306 OLED display via RTOS
 * @param i2c_bus I2C bus number (0-4)
 * @param lines Display height in pixels (32 or 64)
 * @param columns Display width in pixels (usually 128)
 * @return 0 on success, -1 on error
 */
int ssd1306_client_init(uint8_t i2c_bus, uint8_t lines, uint8_t columns);

/**
 * Deinitialize and close SSD1306 connection
 * @return 0 on success, -1 on error
 */
int ssd1306_client_close(void);

/**
 * Clear the entire screen
 * @return 0 on success, -1 on error
 */
int ssd1306_client_clear_screen(void);

/**
 * Set cursor position for text writing
 * @param x Column position (0-127)
 * @param y Page position (0-7 for 64-line display)
 * @return 0 on success, -1 on error
 */
int ssd1306_client_set_cursor(uint8_t x, uint8_t y);

/**
 * Write string at current cursor position
 * @param font_size SSD1306_FONT_SMALL or SSD1306_FONT_NORMAL
 * @param x Column position
 * @param y Page position
 * @param text Null-terminated string to write
 * @return 0 on success, -1 on error
 */
int ssd1306_client_write_string(uint8_t font_size, uint8_t x, uint8_t y, const char* text);

/**
 * Turn display on or off
 * @param onoff 1 for on, 0 for off
 * @return 0 on success, -1 on error
 */
int ssd1306_client_display_onoff(uint8_t onoff);

/**
 * Update entire display with frame buffer and info
 * @param data Display data including frame buffer, face count, and FPS
 * @return 0 on success, -1 on error
 */
int ssd1306_client_update_display(const ssd1306_display_data_t* data);

/**
 * Get pointer to shared memory for direct frame buffer manipulation
 * @return Pointer to shared display data, or NULL on error
 */
ssd1306_display_data_t* ssd1306_client_get_shared_buffer(void);

#endif /* __SSD1306_CLIENT_H__ */
