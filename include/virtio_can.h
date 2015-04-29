#ifndef _LINUX_VIRTIO_CAN_H
#define _LINUX_VIRTIO_CAN_H
/* This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of OpenWide nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */
#include <linux/types.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>
#include <linux/virtio_types.h>

/* The feature bitmap for virtio can */
#define VIRTIO_CAN_F_CTRL_VQ     0	/* Control channel available */ 
#define VIRTIO_CAN_F_GUEST_CANFD 10	/* Guest can handle CAN-FD frames */
#define VIRTIO_CAN_F_HOST_CANFD  20	/* Host can handle CAN-FD frames */

/*
 * Control virtqueue data structures
 *
 * The control virtqueue expects a header in the first sg entry
 * and an ack/status response in the last entry.  Data for the
 * command goes in between.
 */
struct virtio_can_ctrl_hdr {
	__u8 class;
	__u8 cmd;
} __attribute__((packed));

typedef __u8 virtio_can_ctrl_ack;

#define VIRTIO_CAN_OK     0
#define VIRTIO_CAN_ERR    1

/*
 * Control CAN chip status
 *
 * Execute standard CAN controller management operations on the host system.
 */
#define VIRTIO_CAN_CTRL_CHIP    0
 #define VIRTIO_CAN_CTRL_CHIP_ENABLE       0
 #define VIRTIO_CAN_CTRL_CHIP_DISABLE      1
 #define VIRTIO_CAN_CTRL_CHIP_FREEZE       2
 #define VIRTIO_CAN_CTRL_CHIP_UNFREEZE     3
 #define VIRTIO_CAN_CTRL_CHIP_SOFTRESET    4

#endif /* _LINUX_VIRTIO_CAN_H */

