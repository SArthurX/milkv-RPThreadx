#ifndef SSD1306_DRAW_H
#define SSD1306_DRAW_H

#include <stdint.h>

/**
 * @brief Draw a single character on the frame buffer
 * @param frame_buffer Pointer to 1024-byte frame buffer
 * @param x X position (0-127)
 * @param y Y position in pages (0-7), each page is 8 pixels high
 * @param c Character to draw (ASCII 0x20-0x7E)
 */
void ssd1306_draw_char(uint8_t* frame_buffer, uint8_t x, uint8_t y, char c);

/**
 * @brief Draw a string on the frame buffer
 * @param frame_buffer Pointer to 1024-byte frame buffer
 * @param x X position (0-127)
 * @param y Y position in pages (0-7)
 * @param str String to draw (null-terminated)
 */
void ssd1306_draw_string(uint8_t* frame_buffer, uint8_t x, uint8_t y, const char* str);

/**
 * @brief Clear the entire frame buffer
 * @param frame_buffer Pointer to 1024-byte frame buffer
 */
void ssd1306_clear_frame_buffer(uint8_t* frame_buffer);

/**
 * @brief Draw a single pixel
 * @param frame_buffer Pointer to 1024-byte frame buffer
 * @param x X position (0-127)
 * @param y Y position (0-63)
 */
void ssd1306_draw_pixel(uint8_t* frame_buffer, uint8_t x, uint8_t y);

/**
 * @brief Draw a horizontal line
 * @param frame_buffer Pointer to 1024-byte frame buffer
 * @param x Starting X position
 * @param y Y position
 * @param width Width of line
 */
void ssd1306_draw_line_h(uint8_t* frame_buffer, uint8_t x, uint8_t y, uint8_t width);

/**
 * @brief Draw a vertical line
 * @param frame_buffer Pointer to 1024-byte frame buffer
 * @param x X position
 * @param y Starting Y position
 * @param height Height of line
 */
void ssd1306_draw_line_v(uint8_t* frame_buffer, uint8_t x, uint8_t y, uint8_t height);

/**
 * @brief Draw a rectangle outline
 * @param frame_buffer Pointer to 1024-byte frame buffer
 * @param x Top-left X position
 * @param y Top-left Y position
 * @param width Width of rectangle
 * @param height Height of rectangle
 */
void ssd1306_draw_rect(uint8_t* frame_buffer, uint8_t x, uint8_t y, uint8_t width, uint8_t height);

#endif /* SSD1306_DRAW_H */
