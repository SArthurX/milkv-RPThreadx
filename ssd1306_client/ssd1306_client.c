#include "ssd1306_client.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* Mailbox device */
#define RTOS_CMDQU_DEV_NAME "/dev/cvi-rtos-cmdqu"

/* Shared memory physical address - must match device tree */
#define SHARED_MEM_PHYS_ADDR  0x8fffe000  /* CVIMMAP_FREERTOS_SHARED_ADDR */
#define SHARED_MEM_SIZE       0x2000      /* 8KB */

/* Command queue structure - must match kernel driver */
typedef struct {
    unsigned char ip_id;
    unsigned char cmd_id;
    unsigned char block;
    unsigned char resv;
    unsigned int param_ptr;
} rtos_cmdqu_t;

/* IOCTL command */
#define RTOS_CMDQU_SEND _IOW('r', 1, rtos_cmdqu_t)

/* Command IDs - must match RTOS side */
#define CMD_SSD1306_INIT           0x20
#define CMD_SSD1306_DEINIT         0x21
#define CMD_SSD1306_CLEAR          0x22
#define CMD_SSD1306_SET_CURSOR     0x23
#define CMD_SSD1306_WRITE_STRING   0x24
#define CMD_SSD1306_DISPLAY_ONOFF  0x25
#define CMD_SSD1306_UPDATE_DISPLAY 0x26

/* Global state */
static int g_cmdqu_fd = -1;
static void* g_shared_mem = NULL;
static size_t g_shared_mem_size = SHARED_MEM_SIZE;
static int g_mem_fd = -1;

/* Helper function to send command to RTOS */
static int send_command(uint8_t cmd_id, uint32_t param)
{
    if (g_cmdqu_fd < 0) {
        fprintf(stderr, "SSD1306 client not initialized\n");
        return -1;
    }

    rtos_cmdqu_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.ip_id = 0;
    cmd.cmd_id = cmd_id;
    cmd.block = 0;
    cmd.param_ptr = param;

    if (ioctl(g_cmdqu_fd, RTOS_CMDQU_SEND, &cmd) < 0) {
        perror("ioctl RTOS_CMDQU_SEND failed");
        return -1;
    }

    return 0;
}

int ssd1306_client_init(uint8_t i2c_bus, uint8_t lines, uint8_t columns)
{
    /* Open mailbox device */
    g_cmdqu_fd = open(RTOS_CMDQU_DEV_NAME, O_RDWR);
    if (g_cmdqu_fd < 0) {
        perror("Failed to open mailbox device");
        return -1;
    }

    /* Open /dev/mem to access physical memory */
    g_mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (g_mem_fd < 0) {
        perror("Failed to open /dev/mem");
        close(g_cmdqu_fd);
        g_cmdqu_fd = -1;
        return -1;
    }

    /* Map shared physical memory to user space */
    g_shared_mem = mmap(NULL, g_shared_mem_size,
                        PROT_READ | PROT_WRITE,
                        MAP_SHARED,
                        g_mem_fd,
                        SHARED_MEM_PHYS_ADDR);
    
    if (g_shared_mem == MAP_FAILED) {
        perror("Failed to mmap shared memory");
        close(g_mem_fd);
        close(g_cmdqu_fd);
        g_cmdqu_fd = -1;
        g_mem_fd = -1;
        return -1;
    }

    printf("SSD1306: Mapped shared memory at phys=0x%x, virt=%p\n", 
           SHARED_MEM_PHYS_ADDR, g_shared_mem);

    /* Send init command to RTOS */
    uint32_t param = (i2c_bus << 16) | (lines << 8) | columns;
    if (send_command(CMD_SSD1306_INIT, param) < 0) {
        munmap(g_shared_mem, g_shared_mem_size);
        g_shared_mem = NULL;
        close(g_mem_fd);
        close(g_cmdqu_fd);
        g_cmdqu_fd = -1;
        g_mem_fd = -1;
        return -1;
    }

    printf("SSD1306 client initialized: I2C%d, %dx%d\n", i2c_bus, columns, lines);
    return 0;
}

int ssd1306_client_close(void)
{
    if (g_shared_mem) {
        munmap(g_shared_mem, g_shared_mem_size);
        g_shared_mem = NULL;
    }

    if (g_mem_fd >= 0) {
        close(g_mem_fd);
        g_mem_fd = -1;
    }

    if (g_cmdqu_fd >= 0) {
        send_command(CMD_SSD1306_DEINIT, 0);
        close(g_cmdqu_fd);
        g_cmdqu_fd = -1;
    }

    return 0;
}

int ssd1306_client_clear_screen(void)
{
    return send_command(CMD_SSD1306_CLEAR, 0);
}

int ssd1306_client_set_cursor(uint8_t x, uint8_t y)
{
    uint32_t param = (x << 8) | y;
    return send_command(CMD_SSD1306_SET_CURSOR, param);
}

int ssd1306_client_write_string(uint8_t font_size, uint8_t x, uint8_t y, const char* text)
{
    if (!g_shared_mem || !text) {
        return -1;
    }

    /* Prepare string data in shared memory */
    typedef struct {
        uint8_t font_size;
        uint8_t x;
        uint8_t y;
        char text[128];
    } string_data_t;

    string_data_t* str_data = (string_data_t*)g_shared_mem;
    str_data->font_size = font_size;
    str_data->x = x;
    str_data->y = y;
    strncpy(str_data->text, text, 127);
    str_data->text[127] = '\0';

    /* Ensure data is written before sending command */
    __sync_synchronize();

    /* Send command with PHYSICAL address (RTOS can access this) */
    return send_command(CMD_SSD1306_WRITE_STRING, SHARED_MEM_PHYS_ADDR);
}

int ssd1306_client_display_onoff(uint8_t onoff)
{
    return send_command(CMD_SSD1306_DISPLAY_ONOFF, onoff ? 1 : 0);
}

int ssd1306_client_update_display(const ssd1306_display_data_t* data)
{
    if (!g_shared_mem || !data) {
        return -1;
    }

    /* Copy display data to shared memory */
    memcpy(g_shared_mem, data, sizeof(ssd1306_display_data_t));

    /* Ensure data is written before sending command */
    __sync_synchronize();

    /* Send update command with PHYSICAL address */
    return send_command(CMD_SSD1306_UPDATE_DISPLAY, SHARED_MEM_PHYS_ADDR);
}

ssd1306_display_data_t* ssd1306_client_get_shared_buffer(void)
{
    if (!g_shared_mem) {
        fprintf(stderr, "Shared memory not initialized\n");
        return NULL;
    }

    return (ssd1306_display_data_t*)g_shared_mem;
}
