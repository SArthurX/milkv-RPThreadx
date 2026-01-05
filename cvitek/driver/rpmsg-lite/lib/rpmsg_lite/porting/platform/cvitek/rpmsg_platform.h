/*
 * Copyright (c) 2024 Milk-V
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef RPMSG_PLATFORM_H_
#define RPMSG_PLATFORM_H_

#include <stdint.h>

#define VRING_ALIGNMENT      (4096UL)

/* Mailbox */
#define MAILBOX_BASE         (0x01900000UL)
#define RPMSG_MU_CHANNEL     (0)

int32_t platform_init(void);
int32_t platform_deinit(void);

int32_t platform_init_interrupt(uint32_t vector_id, void *isr_data);
int32_t platform_deinit_interrupt(uint32_t vector_id);
void platform_notify(uint32_t vector_id);
void platform_interrupt_enable(uint32_t vector_id);
void platform_interrupt_disable(uint32_t vector_id);
int32_t platform_in_isr(void);

int32_t platform_time_delay(uint32_t num_msec);
uint32_t platform_get_time_stamp(void);

void platform_cache_all_flush_invalidate(void);
void platform_cache_disable(void);
void platform_cache_flush(void *data, uint32_t len);
void platform_cache_invalidate(void *data, uint32_t len);
void platform_map_mem_region(uint32_t vrt_addr, uint32_t phy_addr, 
                             uint32_t size, uint32_t flags);


static inline uint32_t platform_vatopa(void *addr) {
    return (uint32_t)(uintptr_t)addr;
}

static inline void *platform_patova(uint32_t addr) {
    return (void *)(uintptr_t)addr;
}

#endif /* RPMSG_PLATFORM_H_ */
