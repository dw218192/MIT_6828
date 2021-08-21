#ifndef JOS_KERN_E1000_H
#define JOS_KERN_E1000_H
#include <kern/pci.h>

#define E1000_VENDORID 0x8086
#define E1000_DEVICEID 0x100e

// Sizes
#define E1000_NUM_TXDESC 	64
#define E1000_NUM_RXDESC	128
#define E1000_PBS      0x01008  /* Packet Buffer Size, The maximum size of an Ethernet packet is 1518 bytes */
#define E1000_RBS      0x00800  /* Receive Buffer Size, 2048 bytes */


// MMIO E1000 registers, divided by 4 for use as uint32_t[] indices.
#define E1000_STATUS   (0x00008/4)  /* Device Status - RO */

#define E1000_TCTL     (0x00400/4)  /* TX Control - RW */
#define E1000_TCTL_EXT (0x00404/4)  /* Extended TX Control - RW */
#define E1000_TIPG     (0x00410/4)  /* TX Inter-packet gap -RW */
#define E1000_TDBAL    (0x03800/4)  /* TX Descriptor Base Address Low - RW */
#define E1000_TDBAH    (0x03804/4)  /* TX Descriptor Base Address High - RW */
#define E1000_TDLEN    (0x03808/4)  /* TX Descriptor Length - RW */
#define E1000_TDH      (0x03810/4)  /* TX Descriptor Head - RW */
#define E1000_TDT      (0x03818/4)  /* TX Descripotr Tail - RW */

#define E1000_RCTL     (0x00100/4)  /* RX Control - RW */
#define E1000_RDBAL    (0x02800/4)  /* RX Descriptor Base Address Low - RW */
#define E1000_RDBAH    (0x02804/4)  /* RX Descriptor Base Address High - RW */
#define E1000_RDLEN    (0x02808/4)  /* RX Descriptor Length - RW */
#define E1000_RDH      (0x02810/4)  /* RX Descriptor Head - RW */
#define E1000_RDT      (0x02818/4)  /* RX Descriptor Tail - RW */
#define E1000_RAL      (0x05400/4)  /* Receive Address Low - RW */
#define E1000_RAH      (0x05404/4)  /* Receive Address HIGH - RW */

#define E1000_RAH_AV  0x80000000    /* Receive descriptor valid */


/* Transmit Descriptor bit definitions */
#define E1000_TXD_CMD_RS     0x00000008 /* Report Status */
#define E1000_TXD_CMD_EOP    0x00000001 /* End of Packet */

#define E1000_TXD_STAT_DD    0x00000001 /* Descriptor Done */

/* Receive Descriptor bit definitions */
#define E1000_RXD_STAT_DD       0x01    /* Descriptor Done */
#define E1000_RXD_STAT_EOP      0x02    /* End of Packet */

/* Transmit Control */
#define E1000_TCTL_EN     0x00000002    /* enable tx */
#define E1000_TCTL_PSP    0x00000008    /* pad short packets */
#define E1000_TCTL_CT     0x00000ff0    /* collision threshold */
#define E1000_TCTL_COLD   0x003ff000    /* collision distance */

/* Receive Control */
#define E1000_RCTL_EN             0x00000002    /* enable */
#define E1000_RCTL_LPE            0x00000020    /* long packet enable */
#define E1000_RCTL_LBM_NO         0x00000000    /* no loopback mode */
#define E1000_RCTL_RDMTS          0x00000300    /* rx min threshold size */
#define E1000_RCTL_MO             0x00003000    /* multicast offset shift */
#define E1000_RCTL_BAM            0x00008000    /* broadcast enable */
#define E1000_RCTL_SZ_2048        0x00000000    /* rx buffer size 2048 */
#define E1000_RCTL_BSEX           0x02000000    /* Buffer size extension */
#define E1000_RCTL_SECRC          0x04000000    /* Strip Ethernet CRC */

/* Transmit Descriptor */
struct e1000_tx_desc {
    uint64_t buffer_addr;       /* Address of the descriptor's data buffer */
    uint16_t length;    /* Data buffer length */
    uint8_t cso;        /* Checksum offset */
    uint8_t cmd;        /* Descriptor control */
    uint8_t status;     /* Descriptor status */
    uint8_t css;        /* Checksum start */
    uint16_t special;
};

/* Receive Descriptor */
struct e1000_rx_desc {
    uint64_t buffer_addr; /* Address of the descriptor's data buffer */
    uint16_t length;     /* Length of data DMAed into data buffer */
    uint16_t csum;       /* Packet checksum */
    uint8_t status;      /* Descriptor status */
    uint8_t errors;      /* Descriptor Errors */
    uint16_t special;
};

int e1000_attach(struct pci_func *pcif);
int e1000_transmit(const void* data, uint16_t len);
int e1000_receive(void* buf);

#endif	// JOS_KERN_E1000_H
