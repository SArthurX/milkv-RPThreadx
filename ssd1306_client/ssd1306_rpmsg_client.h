/*
 * SSD1306 RPMsg Client - Linux Userspace Library
 * 
 * Communicates with RTOS SSD1306 driver via RPMsg instead of mailbox.
 * This solves the memory address translation issue between Linux and RTOS.
 */

#ifndef __SSD1306_RPMSG_CLIENT_H__
#define __SSD1306_RPMSG_CLIENT_H__

#include <stdint.h>

/* Display dimensions */
#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64
#define SSD1306_FB_SIZE (SSD1306_WIDTH * SSD1306_HEIGHT / 8)  /* 1024 bytes */

/* Font sizes */
#define SSD1306_FONT_SMALL  0
#define SSD1306_FONT_NORMAL 1

/* RPMsg SSD1306 Command IDs */
#define SSD1306_CMD_INIT           0x01
#define SSD1306_CMD_DEINIT         0x02
#define SSD1306_CMD_CLEAR          0x03
#define SSD1306_CMD_SET_CURSOR     0x04
#define SSD1306_CMD_WRITE_STRING   0x05
#define SSD1306_CMD_DISPLAY_ONOFF  0x06
#define SSD1306_CMD_UPDATE_FB      0x07  /* Frame buffer chunk */
#define SSD1306_CMD_FLUSH_FB       0x08  /* Flush accumulated frame buffer */
#define SSD1306_CMD_RESPONSE       0x80  /* Response from RTOS */

/* RPMsg Message Header */
typedef struct __attribute__((packed)) {
    uint8_t cmd;        /* Command ID */
    uint8_t reserved;
    uint16_t length;    /* Payload length */
} ssd1306_msg_hdr_t;

/* Frame buffer chunk (for UPDATE_FB command) */
#define SSD1306_FB_CHUNK_SIZE 256

typedef struct __attribute__((packed)) {
    uint16_t offset;    /* Offset in frame buffer (0, 256, 512, 768) */
    uint16_t length;    /* Bytes in this chunk */
    uint8_t data[SSD1306_FB_CHUNK_SIZE];
} ssd1306_fb_chunk_t;

/* Display data structure (mirrors RTOS side) */
typedef struct __attribute__((packed)) {
    uint8_t frame_buffer[SSD1306_FB_SIZE];  /* 1024 bytes */
    uint32_t face_count;
    float fps;
    uint8_t reserved[12];
} ssd1306_display_data_t;

/* API Functions */

/**
 * Open RPMsg connection only (no INIT - RTOS already initialized at boot)
 * @return 0 on success, -1 on error
 */
int ssd1306_rpmsg_open_only(void);

/**
 * Initialize SSD1306 via RPMsg
 * @param i2c_bus I2C bus number (0-4)
 * @param lines Display height in pixels (32 or 64)
 * @param columns Display width in pixels (usually 128)
 * @return 0 on success, -1 on error
 */
int ssd1306_rpmsg_init(uint8_t i2c_bus, uint8_t lines, uint8_t columns);

/**
 * Close RPMsg connection
 * @return 0 on success, -1 on error
 */
int ssd1306_rpmsg_close(void);

/**
 * Clear the screen
 * @return 0 on success, -1 on error
 */
int ssd1306_rpmsg_clear_screen(void);

/**
 * Set cursor position
 * @param x Column (0-127)
 * @param y Page (0-7)
 * @return 0 on success, -1 on error
 */
int ssd1306_rpmsg_set_cursor(uint8_t x, uint8_t y);

/**
 * Write string at position
 * @param font_size SSD1306_FONT_SMALL or SSD1306_FONT_NORMAL
 * @param x Column
 * @param y Page
 * @param text String to write
 * @return 0 on success, -1 on error
 */
int ssd1306_rpmsg_write_string(uint8_t font_size, uint8_t x, uint8_t y, const char *text);

/**
 * Turn display on/off
 * @param onoff 1=on, 0=off
 * @return 0 on success, -1 on error
 */
int ssd1306_rpmsg_display_onoff(uint8_t onoff);

/**
 * Update display with frame buffer and metadata
 * @param data Display data including frame buffer
 * @return 0 on success, -1 on error
 */
int ssd1306_rpmsg_update_display(const ssd1306_display_data_t *data);

/**
 * Get local display buffer for drawing
 * @return Pointer to local buffer, or NULL on error
 */
ssd1306_display_data_t *ssd1306_rpmsg_get_buffer(void);

#endif /* __SSD1306_RPMSG_CLIENT_H__ */
