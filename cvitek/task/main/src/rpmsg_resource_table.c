/*
 * CV181x ThreadX Resource Table
 * RPMsg VirtIO for Linux Kernel
 * 
 * This must match the Linux kernel's remoteproc.h structure definitions.
 * All addresses and parameters are from rpmsg_static_config.h
 */

#include <stddef.h>
#include <stdint.h>
#include "rpmsg_static_config.h"

/* VirtIO device types - must match Linux include/uapi/linux/virtio_ids.h */
/* (Already defined in rpmsg_static_config.h) */

/*
 * Resource table header - matches Linux struct resource_table
 */
struct resource_table {
    uint32_t ver;           /* version number */
    uint32_t num;           /* number of resource entries */
    uint32_t reserved[2];   /* reserved (must be zero) */
    uint32_t offset[1];     /* array of offsets pointing to resource entries */
} __attribute__((packed));

/*
 * Resource header - every resource entry starts with this
 * matches Linux struct fw_rsc_hdr
 */
struct fw_rsc_hdr {
    uint32_t type;          /* resource type */
} __attribute__((packed));

/*
 * VirtIO vring descriptor - matches Linux struct fw_rsc_vdev_vring
 */
struct fw_rsc_vdev_vring {
    uint32_t da;            /* device address */
    uint32_t align;         /* alignment */
    uint32_t num;           /* number of buffers */
    uint32_t notifyid;      /* notify id */
    uint32_t pa;            /* physical address (filled by remoteproc) */
} __attribute__((packed));

/*
 * VirtIO device resource - matches Linux struct fw_rsc_vdev
 * Note: This follows IMMEDIATELY after fw_rsc_hdr (type field)
 */
struct fw_rsc_vdev {
    uint32_t id;            /* virtio device id */
    uint32_t notifyid;      /* rproc-wide notify index */
    uint32_t dfeatures;     /* device features */
    uint32_t gfeatures;     /* guest features (negotiated) */
    uint32_t config_len;    /* config space length */
    uint8_t  status;        /* virtio status */
    uint8_t  num_of_vrings; /* number of vrings */
    uint8_t  reserved[2];   /* reserved (must be zero) */
    /* vrings follow immediately */
} __attribute__((packed));

/*
 * Complete Resource Table structure
 * Layout must match what Linux remoteproc expects:
 *   - resource_table header
 *   - offset[0] points to fw_rsc_hdr
 *   - fw_rsc_hdr.type = RSC_VDEV
 *   - fw_rsc_vdev follows immediately
 *   - vrings follow fw_rsc_vdev
 */
struct rpmsg_resource_table {
    /* Table header */
    struct resource_table table_hdr;
    
    /* Resource entry: vdev */
    struct fw_rsc_hdr rpmsg_vdev_hdr;   /* type = RSC_VDEV */
    struct fw_rsc_vdev rpmsg_vdev;       /* vdev descriptor */
    struct fw_rsc_vdev_vring vring0;     /* vring0: Host TX -> Remote RX */
    struct fw_rsc_vdev_vring vring1;     /* vring1: Remote TX -> Host RX */
} __attribute__((packed));


__attribute__((section(".resource_table"), used, aligned(16)))
struct rpmsg_resource_table resource_table = {
    /* Resource Table Header */
    .table_hdr = {
        .ver = 1,
        .num = 1,
        .reserved = {0, 0},
        /* offset points to the fw_rsc_hdr (type field) */
        .offset = {offsetof(struct rpmsg_resource_table, rpmsg_vdev_hdr)},
    },
    
    /* Resource header (type = RSC_VDEV) */
    .rpmsg_vdev_hdr = {
        .type = RSC_VDEV,
    },
    
    /* RPMsg VirtIO Device descriptor */
    .rpmsg_vdev = {
        .id = VIRTIO_ID_RPMSG,                  /* virtio device id = 7 */
        .notifyid = 0,                          /* rproc-wide notify id */
        .dfeatures = (1 << VIRTIO_RPMSG_F_NS),  /* device features: name service */
        .gfeatures = 0,                         /* guest features (filled by host) */
        .config_len = 0,                        /* no config space */
        .status = 0,                            /* status (filled by host) */
        .num_of_vrings = 2,                     /* 2 vrings: TX + RX */
        .reserved = {0, 0},
    },
    
    /* vring0: Host (Linux) TX -> Remote (RTOS) RX */
    .vring0 = {
        .da = RPMSG_VRING0_ADDR,       /* device address: 0x8FDC1000 */
        .align = RPMSG_VRING_ALIGN,    /* vring alignment: 4096 */
        .num = RPMSG_VRING_NUM,        /* number of descriptors: 64 */
        .notifyid = 0,                 /* vring0 notify id */
        .pa = 0,                       /* physical address (filled by remoteproc) */
    },
    
    /* vring1: Remote (RTOS) TX -> Host (Linux) RX */
    .vring1 = {
        .da = RPMSG_VRING1_ADDR,       /* device address: 0x8FDC3000 */
        .align = RPMSG_VRING_ALIGN,    /* vring alignment: 4096 */
        .num = RPMSG_VRING_NUM,        /* number of descriptors: 64 */
        .notifyid = 1,                 /* vring1 notify id */
        .pa = 0,                       /* physical address (filled by remoteproc) */
    },
};

