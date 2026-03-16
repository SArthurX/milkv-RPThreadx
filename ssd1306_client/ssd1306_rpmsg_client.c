/*
 * SSD1306 RPMsg Client - Linux Userspace Implementation
 * 
 * Uses /dev/rpmsg_ctrl0 and /dev/rpmsg0 for communication with RTOS.
 * Frame buffer is sent in 256-byte chunks since RPMsg max payload is ~496 bytes.
 */

#include "ssd1306_rpmsg_client.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <poll.h>

/* RPMsg device paths */
#define RPMSG_CTRL_DEV "/dev/rpmsg_ctrl0"
#define RPMSG_EPT_DEV  "/dev/rpmsg0"
#define RTOS_EPT_ADDR  40  /* RTOS endpoint address from rpmsg_static_config.h */

/* RPMsg ioctl definitions */
struct rpmsg_endpoint_info {
    char name[32];
    unsigned int src;
    unsigned int dst;
};

#define RPMSG_CREATE_EPT_IOCTL  _IOW(0xb5, 0x1, struct rpmsg_endpoint_info)
#define RPMSG_DESTROY_EPT_IOCTL _IO(0xb5, 0x2)

/* Global state */
static int g_ctrl_fd = -1;
static int g_ept_fd = -1;
static ssd1306_display_data_t g_display_buffer;

/* Response timeout in milliseconds */
#define RESPONSE_TIMEOUT_MS 500  /* Reduced from 2000ms - fast response expected */
#define RESPONSE_TIMEOUT_SLOW_MS 3000  /* For slow operations like CLEAR and FLUSH */

/* Delay between chunk sends to avoid overwhelming RTOS */
#define CHUNK_SEND_DELAY_US 50000  /* 50ms between chunks */

/* Send message and wait for response */
static int send_cmd(const void *msg, size_t msg_len)
{
    if (g_ept_fd < 0) {
        fprintf(stderr, "[SSD1306] RPMsg endpoint not open\n");
        return -1;
    }

    printf("[SSD1306] Attempting to write %zu bytes to fd=%d...\n", msg_len, g_ept_fd);
    
    /* Send command */
    ssize_t ret = write(g_ept_fd, msg, msg_len);
    
    printf("[SSD1306] write() returned: %zd (expected %zu)\n", ret, msg_len);
    
    if (ret < 0) {
        perror("[SSD1306] write() failed with error");
        return -1;
    }
    
    if (ret != (ssize_t)msg_len) {
        fprintf(stderr, "[SSD1306] Partial write: sent %zd/%zu bytes\n", ret, msg_len);
        return -1;
    }

    printf("[SSD1306] TX: %zu bytes ✅\n", msg_len);
    return 0;
}

/* Wait for response with timeout */
static int wait_response(char *resp_buf, size_t buf_size, int timeout_ms)
{
    struct pollfd pfd;
    int ret;

    if (g_ept_fd < 0) return -1;

    pfd.fd = g_ept_fd;
    pfd.events = POLLIN;

    ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) {
        perror("[SSD1306] Poll failed");
        return -1;
    }
    if (ret == 0) {
        fprintf(stderr, "[SSD1306] Response timeout (%dms)\n", timeout_ms);
        return -1;
    }

    memset(resp_buf, 0, buf_size);
    ret = read(g_ept_fd, resp_buf, buf_size - 1);
    if (ret < 0) {
        if (errno != EAGAIN)
            perror("[SSD1306] Read failed");
        return -1;
    }

    if (ret > 0) {
        resp_buf[ret] = '\0';  /* Null-terminate for safety */
        printf("[SSD1306] RX: %d bytes, first byte=0x%02x\n", ret, (uint8_t)resp_buf[0]);
    }
    return ret;
}

/* Create RPMsg endpoint if not exists */
static int create_endpoint(void)
{
    struct rpmsg_endpoint_info ept_info;
    int ret;

    /* Always open control device (needed even if endpoint exists) */
    if (g_ctrl_fd < 0) {
        g_ctrl_fd = open(RPMSG_CTRL_DEV, O_RDWR);
        if (g_ctrl_fd < 0) {
            perror("[SSD1306] Failed to open " RPMSG_CTRL_DEV);
            return -1;
        }
    }

    /* Check if endpoint already exists and is valid */
    if (access(RPMSG_EPT_DEV, R_OK | W_OK) == 0) {
        printf("[SSD1306] Endpoint %s exists, reusing\n", RPMSG_EPT_DEV);
        return 0;
    }

    /* Wait a bit for RTOS to announce service */
    printf("[SSD1306] Waiting for RTOS service announcement...\n");
    usleep(500000);  /* 500ms */

    /* Create endpoint */
    memset(&ept_info, 0, sizeof(ept_info));
    strncpy(ept_info.name, "rpmsg-ssd1306-demo", sizeof(ept_info.name) - 1);
    ept_info.src = 0xFFFFFFFF;  /* RPMSG_ADDR_ANY */
    ept_info.dst = RTOS_EPT_ADDR;

    printf("[SSD1306] Creating endpoint: name=%s, dst=%d\n", 
           ept_info.name, ept_info.dst);

    ret = ioctl(g_ctrl_fd, RPMSG_CREATE_EPT_IOCTL, &ept_info);
    if (ret < 0) {
        perror("[SSD1306] Failed to create endpoint");
        return -1;
    }

    printf("[SSD1306] Endpoint created, assigned src=%d\n", ept_info.src);
    
    /* Wait for device node to appear */
    for (int i = 0; i < 10; i++) {
        usleep(100000);  /* 100ms */
        if (access(RPMSG_EPT_DEV, F_OK) == 0) {
            printf("[SSD1306] Device node %s ready\n", RPMSG_EPT_DEV);
            return 0;
        }
    }
    
    fprintf(stderr, "[SSD1306] Device node %s did not appear\n", RPMSG_EPT_DEV);
    return -1;
}

/* Open RPMsg connection without sending INIT (RTOS already initialized at boot) */
int ssd1306_rpmsg_open_only(void)
{
    printf("[SSD1306] === Opening RPMsg Connection (No INIT) ===\n");

    /* Create endpoint if needed */
    if (create_endpoint() < 0) {
        fprintf(stderr, "[SSD1306] Failed to create endpoint\n");
        return -1;
    }

    /* Open endpoint device */
    if (g_ept_fd < 0) {
        g_ept_fd = open(RPMSG_EPT_DEV, O_RDWR);
        if (g_ept_fd < 0) {
            perror("[SSD1306] Failed to open " RPMSG_EPT_DEV);
            return -1;
        }
        printf("[SSD1306] Opened %s (fd=%d)\n", RPMSG_EPT_DEV, g_ept_fd);
    }

    /* Clear local buffer */
    memset(&g_display_buffer, 0, sizeof(g_display_buffer));

    printf("[SSD1306] RPMsg connection ready (skipped INIT - RTOS did it at boot)\n");
    return 0;
}

int ssd1306_rpmsg_init(uint8_t i2c_bus, uint8_t lines, uint8_t columns)
{
    char response[128];
    uint8_t msg[8];

    printf("[SSD1306] === Initializing RPMsg SSD1306 Client ===\n");

    /* Create endpoint if needed */
    if (create_endpoint() < 0) {
        fprintf(stderr, "[SSD1306] Failed to create endpoint\n");
        return -1;
    }

    /* Open endpoint device */
    if (g_ept_fd < 0) {
        g_ept_fd = open(RPMSG_EPT_DEV, O_RDWR);
        if (g_ept_fd < 0) {
            perror("[SSD1306] Failed to open " RPMSG_EPT_DEV);
            return -1;
        }
        printf("[SSD1306] Opened %s (fd=%d)\n", RPMSG_EPT_DEV, g_ept_fd);
    }

    /* Clear local buffer */
    memset(&g_display_buffer, 0, sizeof(g_display_buffer));
    memset(msg, 0, sizeof(msg));

    /* Build INIT command: hdr(4) + payload(3) = 7 bytes */
    ssd1306_msg_hdr_t *hdr = (ssd1306_msg_hdr_t *)msg;
    hdr->cmd = SSD1306_CMD_INIT;
    hdr->reserved = 0;
    hdr->length = 3;
    msg[4] = i2c_bus;
    msg[5] = lines;
    msg[6] = columns;

    printf("[SSD1306] Sending INIT: I2C%d, %dx%d\n", i2c_bus, columns, lines);
    printf("[SSD1306] Message bytes: [0x%02x 0x%02x 0x%02x 0x%02x] [0x%02x 0x%02x 0x%02x]\n",
           msg[0], msg[1], msg[2], msg[3], msg[4], msg[5], msg[6]);

    if (send_cmd(msg, 7) < 0) {
        fprintf(stderr, "[SSD1306] Failed to send INIT command\n");
        close(g_ept_fd);
        g_ept_fd = -1;
        return -1;
    }

    /* Wait for response */
    /* DISABLED: RTOS cannot send response (rpmsg_lite_send hangs) */
    /*
    int rx_ret = wait_response(response, sizeof(response), RESPONSE_TIMEOUT_MS * 2);
    if (rx_ret < 0) {
        fprintf(stderr, "[SSD1306] ⚠️  Init warning - no response (might still work)\n");
    } else if (rx_ret > 0 && response[0] == 0) {
        printf("[SSD1306] ✅ Init confirmed by RTOS (result=0x%02x)\n", (uint8_t)response[0]);
    } else if (rx_ret > 0) {
        fprintf(stderr, "[SSD1306] ❌ Init failed: error code 0x%02x\n", (uint8_t)response[0]);
    }
    */
    printf("[SSD1306] Command sent (no response expected - RTOS cannot reply)\n");

    printf("[SSD1306] Initialized via RPMsg\n");
    return 0;
}

int ssd1306_rpmsg_close(void)
{
    uint8_t msg[4];

    if (g_ept_fd >= 0) {
        /* Send DEINIT command */
        ssd1306_msg_hdr_t *hdr = (ssd1306_msg_hdr_t *)msg;
        hdr->cmd = SSD1306_CMD_DEINIT;
        hdr->reserved = 0;
        hdr->length = 0;
        send_cmd(msg, 4);

        usleep(100000);
        close(g_ept_fd);
        g_ept_fd = -1;
    }

    if (g_ctrl_fd >= 0) {
        close(g_ctrl_fd);
        g_ctrl_fd = -1;
    }

    printf("[SSD1306] Closed\n");
    return 0;
}

int ssd1306_rpmsg_clear_screen(void)
{
    uint8_t msg[4];
    char response[128];

    ssd1306_msg_hdr_t *hdr = (ssd1306_msg_hdr_t *)msg;
    hdr->cmd = SSD1306_CMD_CLEAR;
    hdr->reserved = 0;
    hdr->length = 0;

    printf("[SSD1306] Sending CLEAR\n");
    if (send_cmd(msg, 4) < 0) return -1;
    
    /* DISABLED: No response from RTOS */
    /* wait_response(response, sizeof(response), RESPONSE_TIMEOUT_SLOW_MS); */
    usleep(150000);  /* 150ms delay for I2C operation */
    return 0;
}

int ssd1306_rpmsg_set_cursor(uint8_t x, uint8_t y)
{
    uint8_t msg[6];
    char response[128];

    ssd1306_msg_hdr_t *hdr = (ssd1306_msg_hdr_t *)msg;
    hdr->cmd = SSD1306_CMD_SET_CURSOR;
    hdr->reserved = 0;
    hdr->length = 2;
    msg[4] = x;
    msg[5] = y;

    if (send_cmd(msg, 6) < 0) return -1;
    wait_response(response, sizeof(response), RESPONSE_TIMEOUT_MS);
    return 0;
}

int ssd1306_rpmsg_write_string(uint8_t font_size, uint8_t x, uint8_t y, const char *text)
{
    uint8_t msg[256];
    char response[128];
    size_t text_len;

    if (!text) return -1;
    text_len = strlen(text);
    if (text_len > 200) text_len = 200;  /* Limit to fit in RPMsg */

    ssd1306_msg_hdr_t *hdr = (ssd1306_msg_hdr_t *)msg;
    hdr->cmd = SSD1306_CMD_WRITE_STRING;
    hdr->reserved = 0;
    hdr->length = 3 + text_len + 1;  /* font + x + y + text + null */
    
    msg[4] = font_size;
    msg[5] = x;
    msg[6] = y;
    memcpy(&msg[7], text, text_len);
    msg[7 + text_len] = '\0';

    printf("[SSD1306] Writing: \"%s\" at (%d,%d)\n", text, x, y);
    if (send_cmd(msg, 4 + hdr->length) < 0) return -1;
    
    /* DISABLED: No response from RTOS */
    usleep(10000);  /* 10ms delay */
    return 0;
}

int ssd1306_rpmsg_display_onoff(uint8_t onoff)
{
    uint8_t msg[5];
    char response[128];

    ssd1306_msg_hdr_t *hdr = (ssd1306_msg_hdr_t *)msg;
    hdr->cmd = SSD1306_CMD_DISPLAY_ONOFF;
    hdr->reserved = 0;
    hdr->length = 1;
    msg[4] = onoff;

    printf("[SSD1306] Display %s\n", onoff ? "ON" : "OFF");
    if (send_cmd(msg, 5) < 0) return -1;
    
    /* DISABLED: No response from RTOS */
    usleep(10000);
    return 0;
}

int ssd1306_rpmsg_update_display(const ssd1306_display_data_t *data)
{
    uint8_t msg[4 + sizeof(ssd1306_fb_chunk_t)];
    char response[128];
    uint16_t offset;

    if (!data) return -1;

    printf("[SSD1306] Updating display (face_count=%d, fps=%.1f)\n",
           data->face_count, data->fps);

    /* Send frame buffer in 256-byte chunks */
    for (offset = 0; offset < SSD1306_FB_SIZE; offset += SSD1306_FB_CHUNK_SIZE) {
        ssd1306_msg_hdr_t *hdr = (ssd1306_msg_hdr_t *)msg;
        ssd1306_fb_chunk_t *chunk = (ssd1306_fb_chunk_t *)&msg[4];
        
        hdr->cmd = SSD1306_CMD_UPDATE_FB;
        hdr->reserved = 0;
        hdr->length = sizeof(ssd1306_fb_chunk_t);

        chunk->offset = offset;
        chunk->length = SSD1306_FB_CHUNK_SIZE;
        memcpy(chunk->data, &data->frame_buffer[offset], SSD1306_FB_CHUNK_SIZE);

        printf("[SSD1306] Sending FB chunk: offset=%d\n", offset);
        if (send_cmd(msg, sizeof(msg)) < 0) {
            return -1;
        }

        /* DISABLED: No response from RTOS */
        /* wait_response(response, sizeof(response), RESPONSE_TIMEOUT_MS); */
        
        /* Add delay between chunks to avoid overwhelming RTOS */
        usleep(CHUNK_SEND_DELAY_US);
    }

    /* Send FLUSH command to trigger display update */
    ssd1306_msg_hdr_t *hdr = (ssd1306_msg_hdr_t *)msg;
    hdr->cmd = SSD1306_CMD_FLUSH_FB;
    hdr->reserved = 0;
    hdr->length = 8;  /* face_count(4) + fps(4) */
    memcpy(&msg[4], &data->face_count, 4);
    memcpy(&msg[8], &data->fps, 4);

    printf("[SSD1306] Sending FLUSH\n");
    if (send_cmd(msg, 12) < 0) return -1;
    
    /* DISABLED: No response from RTOS */
    /* Use longer delay for FLUSH as it involves slow I2C transfer */
    usleep(200000);  /* 200ms */
    return 0;
}

ssd1306_display_data_t *ssd1306_rpmsg_get_buffer(void)
{
    return &g_display_buffer;
}
