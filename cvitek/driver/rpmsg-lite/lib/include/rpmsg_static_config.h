#ifndef RPMSG_STATIC_CONFIG_H_
#define RPMSG_STATIC_CONFIG_H_

#include <stdint.h>

/* ============================================
 * addr mapp
 * ============================================
 * 
 * (RPMSG) 256KB = 0x40000:
 * 
 *   0x8FDC0000 +------------------+
 *              | Resource Table   | 4KB (0x1000)
 *   0x8FDC1000 +------------------+
 *              | VRing0           | 8KB (0x2000)
 *              | Linux RX <- RTOS TX (Linux pre-fills)
 *   0x8FDC3000 +------------------+
 *              | VRing1           | 8KB (0x2000)
 *              | Linux TX -> RTOS RX
 *   0x8FDC5000 +------------------+
 *              | Buffer Pool      | 236KB
 *              |                  |
 *   0x8FDFFFFF +------------------+
 *
 * Per Linux virtio_rpmsg_bus.c:
 *   vrp->rvq = vqs[0] = vring0 = Linux RX (pre-filled with buffers)
 *   vrp->svq = vqs[1] = vring1 = Linux TX
 *
 * RTOS must use opposite:
 *   tvq = vqs[0] = vring0 = RTOS TX (writes to Linux's pre-filled buffers)
 *   rvq = vqs[1] = vring1 = RTOS RX (reads from Linux's TX)
 */


#define RPMSG_MEM_BASE          0x8FDC0000UL
#define RPMSG_MEM_SIZE          (256 * 1024)        /* 256KB */
#define RPMSG_RSC_TABLE_ADDR    RPMSG_MEM_BASE      /* 0x8FDC0000 */
#define RPMSG_RSC_TABLE_SIZE    (4 * 1024)          /* 4KB */

#define RPMSG_VRING0_ADDR       0x8FDC1000UL        /* Linux RX (pre-filled) <- RTOS TX */
#define RPMSG_VRING1_ADDR       0x8FDC3000UL        /* Linux TX -> RTOS RX */
#define RPMSG_VRING_SIZE        0x2000UL            /* 8KB per vring */

#define RPMSG_VRING_ALIGN       4096                /* 4KB alignment for vring */
#define RPMSG_VRING_NUM         64                  /* Number of descriptors */

/* Buffer Pool  */
#define RPMSG_BUFFER_POOL_ADDR  0x8FDC5000UL
#define RPMSG_BUFFER_POOL_SIZE  (RPMSG_MEM_SIZE - (RPMSG_BUFFER_POOL_ADDR - RPMSG_MEM_BASE))

#define RPMSG_BUFFER_PAYLOAD    496
#define RPMSG_BUFFER_SIZE       (RPMSG_BUFFER_PAYLOAD + 16) /* 512 bytes with header */
#define RPMSG_BUFFER_COUNT      64

#define RPMSG_SHMEM_BASE        RPMSG_VRING0_ADDR   /* 0x8FDC1000 */

/* Link ID (CV181x RPMsg ID) */
#define RPMSG_LITE_LINK_ID      0

/* ============================================
 * VirtIO 
 * ============================================ */

/* VirtIO Device ID */
#define VIRTIO_ID_RPMSG         7
#define VIRTIO_RPMSG_F_NS       0   /* Name service notifications */

/* Resource */
#define RSC_CARVEOUT            0
#define RSC_DEVMEM              1
#define RSC_TRACE               2
#define RSC_VDEV                3

/* ============================================
 * Endpoint 
 * ============================================ */
 
#define RPMSG_LOCAL_EPT_ADDR    40

/* Name Service  (Linux rpmsg_char driver define)  */
#define RPMSG_NS_SERVICE_NAME   "rpmsg_chrdev"

/*
 * Resource Table struct
 * ELF .resource_table section
 */
struct rpmsg_resource_table_full {
    /* Table header */
    struct {
        uint32_t ver;
        uint32_t num;
        uint32_t reserved[2];
        uint32_t offset[1];
    } __attribute__((packed)) table_hdr;
    
    /* Resource header */
    struct {
        uint32_t type;
    } __attribute__((packed)) rpmsg_vdev_hdr;
    
    /* VDev descriptor */
    struct {
        uint32_t id;
        uint32_t notifyid;
        uint32_t dfeatures;
        uint32_t gfeatures;
        uint32_t config_len;
        uint8_t  status;
        uint8_t  num_of_vrings;
        uint8_t  reserved[2];
    } __attribute__((packed)) rpmsg_vdev;
    
    /* VRing descriptors */
    struct {
        uint32_t da;
        uint32_t align;
        uint32_t num;
        uint32_t notifyid;
        uint32_t pa;
    } __attribute__((packed)) vring0, vring1;
} __attribute__((packed));


#if (RPMSG_VRING0_ADDR & (RPMSG_VRING_ALIGN - 1)) != 0
#error "RPMSG_VRING0_ADDR is not properly aligned!"
#endif

#if (RPMSG_VRING1_ADDR & (RPMSG_VRING_ALIGN - 1)) != 0
#error "RPMSG_VRING1_ADDR is not properly aligned!"
#endif

#if (RPMSG_BUFFER_COUNT & (RPMSG_BUFFER_COUNT - 1)) != 0
#error "RPMSG_BUFFER_COUNT must be power of two!"
#endif

#endif /* RPMSG_STATIC_CONFIG_H_ */
