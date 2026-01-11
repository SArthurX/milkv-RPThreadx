#ifndef __COMM_HEADER__
#define __COMM_HEADER__

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif

/**
 * comm_mailbox_send_rpmsg_kick - Send RPMsg VirtIO kick to Linux via mailbox
 * @vq_id: VirtQueue ID (0 or 1)
 * 
 * Returns: 0 on success, -1 on failure
 */
int comm_mailbox_send_rpmsg_kick(uint32_t vq_id);


#endif // end of __COMM_HEADER__
