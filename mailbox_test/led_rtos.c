#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define RTOS_CMDQU_DEV_NAME "/dev/cvi-rtos-cmdqu"

typedef struct {
  unsigned char ip_id;
  unsigned char cmd_id;
  unsigned char block;
  unsigned char resv;
  unsigned int param_ptr;
} rtos_cmdqu_t;

#define RTOS_CMDQU_SEND _IOW('r', 1, rtos_cmdqu_t)
#define RTOS_CMDQU_SEND_WAIT _IOW('r', 4, rtos_cmdqu_t)

#define CMD_DUO_LED 0x10

int main(int argc, char *argv[]) {
  int fd;
  int ret;
  rtos_cmdqu_t cmd;

  if (argc != 2) {
    printf("Usage: %s <0|1>\n", argv[0]);
    printf("  1: LED ON\n");
    printf("  0: LED OFF\n");
    return -1;
  }

  int state = atoi(argv[1]);

  fd = open(RTOS_CMDQU_DEV_NAME, O_RDWR);
  if (fd < 0) {
    perror("Open device failed");
    return -1;
  }

  memset(&cmd, 0, sizeof(cmd));
  cmd.ip_id = 0;
  cmd.cmd_id = CMD_DUO_LED;
  cmd.block = 1;
  cmd.param_ptr = state;

    printf("Sending CMD(wait): ID=%d, Param=%d to RTOS...\n", cmd.cmd_id,
      cmd.param_ptr);

    ret = ioctl(fd, RTOS_CMDQU_SEND_WAIT, &cmd);
  if (ret < 0) {
    perror("IOCTL failed");
    close(fd);
    return -1;
  }

    printf("RTOS replied: cmd_id=%d, param_ptr=%d\n", cmd.cmd_id, cmd.param_ptr);

  close(fd);
  return 0;
}
