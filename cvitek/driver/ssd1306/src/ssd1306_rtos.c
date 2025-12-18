#include "ssd1306_rtos.h"
#include "i2c.h"
#include "printf.h"
#include <stdint.h>
#include <string.h>
#include "font.h"

/* Global state */
static uint8_t g_i2c_bus = 0;
static uint8_t g_max_lines = 0;
static uint8_t g_max_columns = 0;
static uint8_t g_current_x = 0;
static uint8_t g_current_y = 0;
static uint8_t g_initialized = 0;

/* Internal buffer for I2C transactions */
static uint8_t tx_buffer[256];

/* Helper function to send command */
static uint8_t send_command(const uint8_t* cmd, uint16_t len)
{
    if (!g_initialized) return 1;
    
    tx_buffer[0] = SSD1306_COMM_CONTROL_BYTE;
    memcpy(tx_buffer + 1, cmd, len);
    
    return i2c_write(g_i2c_bus, SSD1306_I2C_ADDR, 0, 0, tx_buffer, len + 1);
}

/* Helper function to send data */
static uint8_t send_data(const uint8_t* data, uint16_t len)
{
    if (!g_initialized) return 1;
    
    tx_buffer[0] = SSD1306_DATA_CONTROL_BYTE;
    memcpy(tx_buffer + 1, data, len);
    
    return i2c_write(g_i2c_bus, SSD1306_I2C_ADDR, 0, 0, tx_buffer, len + 1);
}

uint8_t ssd1306_rtos_init(uint8_t i2c_bus, uint8_t lines, uint8_t columns)
{
    g_i2c_bus = i2c_bus;
    g_max_lines = lines;
    g_max_columns = columns;
    g_current_x = 0;
    g_current_y = 0;
    
    /* Initialize I2C */
    i2c_init(g_i2c_bus);
    
    /* SSD1306 initialization sequence */
    uint8_t init_seq[] = {
        SSD1306_COMM_DISPLAY_OFF,       // Display off
        SSD1306_COMM_DISP_NORM,         // Normal display
        SSD1306_COMM_CLK_SET, 0x80,     // Clock divide ratio
        SSD1306_COMM_MULTIPLEX, lines - 1,  // Multiplex ratio
        SSD1306_COMM_VERT_OFFSET, 0x00, // Display offset
        SSD1306_COMM_START_LINE,        // Start line
        SSD1306_COMM_CHARGE_PUMP, 0x14, // Charge pump on
        SSD1306_COMM_MEMORY_MODE, SSD1306_PAGE_MODE,  // Page mode
        SSD1306_COMM_HORIZ_NORM,        // Segment remap
        SSD1306_COMM_SCAN_NORM,         // COM scan direction
        SSD1306_COMM_COM_PIN, (lines == 32) ? 0x02 : 0x12,  // COM pins
        SSD1306_COMM_CONTRAST, 0x7F,    // Contrast
        SSD1306_COMM_PRECHARGE, 0xF1,   // Pre-charge period
        SSD1306_COMM_DESELECT_LV, 0x40, // VCOMH deselect
        SSD1306_COMM_RESUME_RAM,        // Resume from RAM
        SSD1306_COMM_DISP_NORM,         // Normal display
        SSD1306_COMM_DISPLAY_ON,        // Display on
        SSD1306_COMM_DISABLE_SCROLL     // Disable scroll
    };
    
    uint8_t result = send_command(init_seq, sizeof(init_seq));
    if (result == 0) {
        g_initialized = 1;
        printf("[SSD1306] Initialized: I2C%d, %dx%d\n", i2c_bus, columns, lines);
    } else {
        printf("[SSD1306] Init failed\n");
    }
    
    return result;
}

uint8_t ssd1306_rtos_deinit(void)
{
    if (!g_initialized) return 0;
    
    uint8_t cmd = SSD1306_COMM_DISPLAY_OFF;
    send_command(&cmd, 1);
    
    g_initialized = 0;
    return 0;
}

uint8_t ssd1306_rtos_clear_screen(void)
{
    if (!g_initialized) return 1;
    
    uint8_t result = 0;
    uint8_t zero_data[128];
    memset(zero_data, 0, sizeof(zero_data));
    
    for (uint8_t page = 0; page < (g_max_lines / 8); page++) {
        ssd1306_rtos_set_cursor(0, page);
        result |= send_data(zero_data, g_max_columns);
    }
    
    g_current_x = 0;
    g_current_y = 0;
    
    return result;
}

uint8_t ssd1306_rtos_set_cursor(uint8_t x, uint8_t y)
{
    if (!g_initialized || x >= g_max_columns || y >= (g_max_lines / 8)) {
        return 1;
    }
    
    g_current_x = x;
    g_current_y = y;
    
    uint8_t cmd[] = {
        (uint8_t)(SSD1306_COMM_PAGE_NUMBER | (y & 0x0F)),
        (uint8_t)(SSD1306_COMM_LOW_COLUMN | (x & 0x0F)),
        (uint8_t)(SSD1306_COMM_HIGH_COLUMN | ((x >> 4) & 0x0F))
    };
    
    return send_command(cmd, sizeof(cmd));
}

uint8_t ssd1306_rtos_write_string(uint8_t font_size, const char* text)
{
    if (!g_initialized || !text) return 1;
    
    const uint8_t* font_table;
    uint8_t font_width;
    
    if (font_size == SSD1306_FONT_SMALL) {
        font_table = font5x7;
        font_width = 5;
    } else {
        font_table = font8x8;
        font_width = 8;
    }
    
    uint8_t result = 0;
    uint16_t idx = 0;
    
    while (text[idx] != '\0' && idx < 128) {
        if (text[idx] < ' ' || text[idx] > '~') {
            idx++;
            continue;
        }
        
        const uint8_t* char_data = &font_table[(text[idx] - 0x20) * font_width];
        result |= send_data(char_data, font_width);
        
        if (font_size == SSD1306_FONT_SMALL) {
            uint8_t space = 0x00;
            result |= send_data(&space, 1);
        }
        
        idx++;
    }
    
    return result;
}

uint8_t ssd1306_rtos_display_onoff(uint8_t onoff)
{
    if (!g_initialized) return 1;
    
    uint8_t cmd = onoff ? SSD1306_COMM_DISPLAY_ON : SSD1306_COMM_DISPLAY_OFF;
    return send_command(&cmd, 1);
}

uint8_t ssd1306_rtos_set_mem_mode(uint8_t mode)
{
    if (!g_initialized) return 1;
    
    uint8_t cmd[] = { SSD1306_COMM_MEMORY_MODE, mode };
    return send_command(cmd, sizeof(cmd));
}

uint8_t ssd1306_rtos_set_col(uint8_t start, uint8_t end)
{
    if (!g_initialized) return 1;
    
    uint8_t cmd[] = { SSD1306_COMM_SET_COL_ADDR, start, end };
    return send_command(cmd, sizeof(cmd));
}

uint8_t ssd1306_rtos_set_page(uint8_t start, uint8_t end)
{
    if (!g_initialized) return 1;
    
    uint8_t cmd[] = { SSD1306_COMM_SET_PAGE_ADDR, start, end };
    return send_command(cmd, sizeof(cmd));
}

uint8_t ssd1306_rtos_flush_buffer(const uint8_t* frame_buffer, uint16_t size)
{
    if (!g_initialized || !frame_buffer || size != 1024) {
        return 1;
    }
    
    /* Set to horizontal mode for efficient transfer */
    ssd1306_rtos_set_mem_mode(SSD1306_HORI_MODE);
    ssd1306_rtos_set_col(0, g_max_columns - 1);
    ssd1306_rtos_set_page(0, (g_max_lines / 8) - 1);
    
    /* Transfer frame buffer in chunks */
    uint8_t result = 0;
    const uint16_t chunk_size = 128;
    
    for (uint16_t i = 0; i < size; i += chunk_size) {
        uint16_t bytes_to_send = (i + chunk_size > size) ? (size - i) : chunk_size;
        result |= send_data(frame_buffer + i, bytes_to_send);
    }
    
    return result;
}

uint8_t ssd1306_rtos_update_display(const ssd1306_shared_data_t* data)
{
    if (!g_initialized || !data) return 1;
    
    uint8_t result = 0;
    
    /* Flush frame buffer */
    result |= ssd1306_rtos_flush_buffer(data->frame_buffer, 1024);
    
    /* Display text info */
    char info[32];
    
    ssd1306_rtos_set_cursor(0, 0);
    snprintf(info, sizeof(info), "Faces:%u", (unsigned int)data->face_count);
    result |= ssd1306_rtos_write_string(SSD1306_FONT_SMALL, info);
    
    ssd1306_rtos_set_cursor(0, 1);
    snprintf(info, sizeof(info), "FPS:%.1f", data->fps);
    result |= ssd1306_rtos_write_string(SSD1306_FONT_SMALL, info);
    
    return result;
}
