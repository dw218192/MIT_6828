#include <kern/e1000.h>
#include <kern/pmap.h>
#include <inc/string.h>
#include <inc/error.h>

static void e1000_test_mmio(void);
static void e1000_test_transmit(void);

struct e1000_tx_desc tx_desc_queue[E1000_NUM_TXDESC] __attribute__ ((aligned (16)));
struct e1000_rx_desc rx_desc_queue[E1000_NUM_RXDESC] __attribute__ ((aligned (16)));

/* transmission buffers */
uint8_t tx_bufs[E1000_PBS][E1000_NUM_TXDESC];

/* reception buffers */
uint8_t rx_bufs[E1000_RBS][E1000_NUM_RXDESC];

volatile uint32_t *e1000_mmiobase;

// LAB 6: Your driver code here
int e1000_attach(struct pci_func *pcif)
{
    int i;

    pci_func_enable(pcif);
    e1000_mmiobase = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
    e1000_test_mmio();

    /*********************************************
     * Transmission Initialization
     * 
     * *******************************************/

    // Set the Transmit Descriptor Base Address (TDBAL) register
    e1000_mmiobase[E1000_TDBAL] = PADDR(tx_desc_queue);

    // Set the Transmit Descriptor Length (TDLEN) register
    e1000_mmiobase[E1000_TDLEN] = E1000_NUM_TXDESC * sizeof(struct e1000_tx_desc);

    // The Transmit Descriptor Head and Tail (TDH/TDT) registers are initialized to 0b
	e1000_mmiobase[E1000_TDH] = 0;
	e1000_mmiobase[E1000_TDT] = 0;

    // Initialize the Transmit Control (TCTL) register
    e1000_mmiobase[E1000_TCTL] |= E1000_TCTL_EN;
    e1000_mmiobase[E1000_TCTL] |= E1000_TCTL_PSP;
    e1000_mmiobase[E1000_TCTL] &= ~E1000_TCTL_CT;
    e1000_mmiobase[E1000_TCTL] |= 0x10 << 4;
    e1000_mmiobase[E1000_TCTL] &= ~E1000_TCTL_COLD;
    e1000_mmiobase[E1000_TCTL] |= 0x40 << 12;

    // Initialize the Transmit IPG (TIPG) register
	e1000_mmiobase[E1000_TIPG] = 0x0;
	e1000_mmiobase[E1000_TIPG] |= (0x6) << 20; // IPGR2 
	e1000_mmiobase[E1000_TIPG] |= (0x4) << 10; // IPGR1
	e1000_mmiobase[E1000_TIPG] |= 0xA; // IPGR

    memset(tx_desc_queue, 0, E1000_NUM_TXDESC * sizeof(struct e1000_tx_desc));
    // Initialize TX Descriptor queue
    for(i=0; i<E1000_NUM_TXDESC; ++i)
    {
        //set the descriptor as done, so when the transmit function is called for the first time, it can fill in stuff
        tx_desc_queue[i].status |= E1000_TXD_STAT_DD;
        tx_desc_queue[i].buffer_addr = PADDR(&tx_bufs[i]);
    }

    // e1000_test_transmit();

    /*********************************************
     * Reception Initialization
     * 
     * *******************************************/

    // Initialize receive address registers
    // hardcoded MAC address 52.54.00.12.34.56
    e1000_mmiobase[E1000_RAL] = 0x12005452;
    e1000_mmiobase[E1000_RAH] = 0x00005634;

    e1000_mmiobase[E1000_RAH] |= E1000_RAH_AV; // when set, the address is valid and is compared against the incoming packet

    // Set the Receive Descriptor Base Address (RDBAL) register
    e1000_mmiobase[E1000_RDBAL] = PADDR(rx_desc_queue);

    // Set the Receive Descriptor Length (RDLEN) register
    e1000_mmiobase[E1000_RDLEN] = E1000_NUM_RXDESC * sizeof(struct e1000_rx_desc);
	
    e1000_mmiobase[E1000_RDH] = 0;
	e1000_mmiobase[E1000_RDT] = E1000_NUM_RXDESC - 1;

    // Initialize the Receive Control (RCTL) register
    e1000_mmiobase[E1000_RCTL] |= E1000_RCTL_EN;
    e1000_mmiobase[E1000_RCTL] &= ~E1000_RCTL_LPE; // do not receive long packets
    e1000_mmiobase[E1000_RCTL] |= E1000_RCTL_LBM_NO;
    e1000_mmiobase[E1000_RCTL] &= ~E1000_RCTL_RDMTS;
    e1000_mmiobase[E1000_RCTL] &= ~E1000_RCTL_MO;
    e1000_mmiobase[E1000_RCTL] |= E1000_RCTL_SZ_2048;
    e1000_mmiobase[E1000_RCTL] |= E1000_RCTL_SECRC;

    memset(rx_desc_queue, 0, E1000_NUM_RXDESC * sizeof(struct e1000_rx_desc));
    // Initialize RX Descriptor queue
    for(i=0; i<E1000_NUM_RXDESC; ++i)
    {
        rx_desc_queue[i].buffer_addr = PADDR(&rx_bufs[i]);
    }

    return 0;
}

static void e1000_test_mmio(void)
{
    if(e1000_mmiobase[E1000_STATUS] != 0x80080783)
    {
        cprintf("e1000_test_mmio: e1000 device status = %08x, expected %08x\n", e1000_mmiobase[E1000_STATUS], 0x80080783);
        assert(false);
    }

    cprintf("e1000 mmio is good\n");
}


int e1000_transmit(const void* data, uint16_t len)
{
    // transmit a packet by checking that the next descriptor is free, copying the packet data into the next descriptor, and updating TDT
    unsigned int idx = e1000_mmiobase[E1000_TDT];
    struct e1000_tx_desc *next = tx_desc_queue + idx;

    if(len > E1000_PBS)
        return -E_PKT_TOO_LONG;

    if(next->status & E1000_TXD_STAT_DD)
    {
        // a free descriptor is available
        memcpy((void*) KADDR(next->buffer_addr), data, len);

        next->length = len;

        next->cmd |= E1000_TXD_CMD_RS;
        next->cmd |= E1000_TXD_CMD_EOP;

        next->status &= ~E1000_TXD_STAT_DD;

        // wrap
        idx = (idx + 1) % E1000_NUM_TXDESC;
        e1000_mmiobase[E1000_TDT] = idx;

        return 0;
    }
    else
    {
        // transmission ring is full
        // drop
        return - E_TX_FULL;
    }
}

static void e1000_test_transmit(void)
{
    char buf[] = "this is the new message of the day!\t\n";
    int i;
    int r;

    for(i=0; i<E1000_NUM_TXDESC*2; ++i)
    {
        if((r = e1000_transmit(buf, MAX((int)sizeof(buf)-i, 1)))<0)
            cprintf("e1000_test_transmit(%08x, %d): %e\n", buf, MAX(sizeof(buf)-i, 1), r);
    }
}

/* 
* attempts to receive a packet through the e1000
* returns the length of the data written to buf
* when an error occured, returns the error code
*/
int e1000_receive(void* buf)
{
    unsigned int idx = e1000_mmiobase[E1000_RDT];
    // wrap
    idx = (idx + 1) % E1000_NUM_RXDESC;

    struct e1000_rx_desc *next = rx_desc_queue + idx;
    int len;

    if(next->status & E1000_RXD_STAT_DD)
    {
        // a packet is received
        len = next->length;
        memcpy(buf, (void*) KADDR(next->buffer_addr), len);

        next->status &= ~E1000_RXD_STAT_DD;
        next->status &= ~E1000_RXD_STAT_EOP;

        e1000_mmiobase[E1000_RDT] = idx;

        return len;
    }
    else
    {
        // no packet was received, rx ring is empty
        // notify the caller
        return - E_RX_EMPTY;
    }
}