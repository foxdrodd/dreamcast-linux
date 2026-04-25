/* lan_adapter.h: A SEGA LAN adapter (HIT-0300) device driver for Linux. */

/*  A Linux device driver for HIT-0300 LAN adapter (SEGA Dreamcast).

    This software may be used and distributed according to the
    terms of the GNU General Public License (GPL), incorporated
    herein by reference.  Drivers based on or derived from this
    code fall under the GPL and must retain the authorship,
    copyright and license notice.  This file is not a complete
    program and may only be used when the entire operating
    system is licensed under the GPL. See the file "COPYING" in 
    the main directory of this archive for more details.

    This driver is for the SEGA LAN adapter, based on the MB86967, a
    multi-purpose ethernet chip.

    Contributors and Contacts:

    	The author may be reached as c.berger@tu-braunschweig.de .

    	Support and updates available at 
       http://linuxdc.sourceforge.net

     	Thanks for many good documentation and sources from Dan 
        Potter (dcload-ip, Kallistios), Christian Groessler 
        (if_mbe_g2.c patch for NetBSD) and many other OpenSource 
        authors for their sources that gave me a good overview
        for writing a device driver.

    Acknowledgments:
        
        Dreamcast is a registered trademark of Sega, Inc.
    
   ChangeLog:

	11-03-2003  Christian Berger <c.berger@tu-braunschweig.de>
		    Device driver ported to 2.6.0-test9.
    
	02-28-2003  Christian Berger <c.berger@tu-braunschweig.de>
    	            First public release.
*/

#include <asm/io.h>				/* Use generic io functions. */
#include <linux/ioport.h>			/* Stuff for requesting and releasing I/O address space. */

#include <linux/delay.h>			/* For waiting. */
#include <linux/errno.h>			/* Some useful errno's. */
#include <linux/init.h>				/* __init definitions. */
#include <linux/module.h>			/* General module handling routines. */

#include <linux/netdevice.h>			/* A linux nic driver without netdevice? Show me how... :-) */
#include <linux/etherdevice.h>			/* Functions for handling ethernet packets. */
#include <linux/if_ether.h>         		/* IEEE 802.3 definitions. */
#include <linux/skbuff.h>			/* Sending buffer handling. */

#include <linux/string.h>			/* For handling strcpy. */

#define HIT_NO_DEBUG		0		/* No debug - messages. */
#define HIT_INFO     		2	    	/* For info purposes set HIT_DEBUG to > 1. */
#define HIT_FULL_DEBUG		5	   	 /* For testing purposes set HIT_DEBUG to > 4. */

#define DRV_NAME		    "lan_adapter"
#define DRV_VERSION		    "0.0.8"
#define DRV_RELDATE		    "11/03/2003"

/* This driver actually supports only the HIT-0300 lan adapter.
 * So we just hardcode the base I/O port.
 * This is the base I/O - port found in if_mbe_g2.c (netbsd),
 * modified to fit into the abstraction layer of the dreamcast port
 * "dreamcast_isa_port2addr(unsigned long offset)" in arch/sh/kernel/io_dc.c. */
#define HIT_BASE		0x00600400	

/* Basic routines for handling the I/O - registers. The register decoding
 * according to Christian Groessler's source follows: 
 *
 * regnum      -      address
 *   0                   0
 *   1                   4
 *   2                   8
 *
 * So, we've got this special macro. */
#define HIT_REG(reg)    (HIT_BASE + (reg)*4)

/* I'm assuming the following interrupt scheme:
 * HW_EVENT_IRQ_BASE equals OFF_CHIP_IRQ_BASE equals IRQ 48.
 * According to Christian Groesslers patch the lan_adapter reacts to IRQ 34, so
 * I guess the correct IRQ is 48 + 34 = 82. */
#define HIT_IRQ			82

/* Data Link Control Registers, always accessible*/
#define HIT_DLCR0      		0           /* Transmit Status Register. */
#define HIT_DLCR1       	1           /* Receive Status Register. */
#define HIT_DLCR2       	2           /* Receive Mode Register. */
#define HIT_DLCR3       	3           /* Receive Mode Register. */
#define HIT_DLCR4       	4           /* Receive Mode Register. */
#define HIT_DLCR5       	5           /* Receive Mode Register. */
#define HIT_DLCR6       	6           /* Receive Mode Register. */
#define HIT_DLCR7		7           /* Control Register 2. */

/* Upper Data Link Control Registers, accessible via switchbank(0); */
#define HIT_DLCR8		8           /* Node ID Register 0. */
#define HIT_DLCR9		9           /* Node ID Register 1. */
#define HIT_DLCR10		10          /* Node ID Register 2. */
#define HIT_DLCR11		11          /* Node ID Register 3. */
#define HIT_DLCR12		12          /* Node ID Register 4. */
#define HIT_DLCR13		13          /* Node ID Register 5. */

/* Multicast address registers, accessible via switchbank(1); */
#define HIT_MAR8		8           /* Offset multicast address. */

/* Buffer Memory Port Registers, accessible via switchbank(2); */
#define HIT_BMPR8		8           /* Buffer memory port. */
#define HIT_BMPR10		10          /* Transmit packet count. */
#define HIT_BMPR11		11          /* 16 collision. */
#define HIT_BMPR12		12          /* DMA enable. */
#define HIT_BMPR13		13          /* DMA burst/transceiver mode. */
#define HIT_BMPR14		14          /* Receive control/transceiver interrupt. */
#define HIT_BMPR15		15          /* Transceiver status/control. */

/* Buffer Memory Port Registers, accessible in JLI (jumperless ISA mode) mode. */
#define HIT_BMPR16		16          /* EEPROM control. */
#define HIT_BMPR17		17          /* EEPROM data. */

/* Some self-explainig bit names. */
#define HIT_D0_NET_BSY	0x40		    /* (= 64) Net busy? */

#define HIT_D3_PKT_RDY	0x80		    /* (=128) Enable interrupt for TMT OK (receive interrupt). */

#define HIT_D5_AM_OTHER	0x02		    /* (=  2) Address mode: only other node packets. */
#define HIT_D5_AM_PROM	0x03		    /* (=  3) Address mode: receive all packets (promiscuous mode). */
#define HIT_D5_BUF_EMP	0x40		    /* (= 64) Buffer empty?. */

#define HIT_D6_ENA_DLC	0x80		    /* (=128) Enable Data Link Controller. */

#define HIT_D7_STBY 	0x20		    /* (= 32) Enable/disable operation state. */
#define HIT_D7_RBMASK	0x0c		    /* (= 12) Register bank selection mask. */

#define HIT_B10_TX  	0x80		    /* (=128) Start transmission. */

#define HIT_B11_AUTO  	0x06		    /* Auto-retransmit. */

#define HIT_B15_CLR_ALL	0x4a	            /* Reset transceiver status. */

#define HIT_B16_SELECT	0x20		    /* (= 32) EEPROM chip select. */
#define HIT_B16_CLOCK	0x40		    /* (= 64) EEPROM shift clock. */
#define HIT_B17_DATA	0x80                /* (=128) EEPROM data bit. */

#define HIT_IO_EXTENT	32                  /* Upper bound for request_region(... , ...). */

/* Hardcoded G2 bus reset, as defined in dcload-ip-la.diff.
 * Unfortunately, the generic System ASIC code for Linux only supports
 * the broadband adapter (SEGA BBA). So, I've added this piece of code
 * for resetting the G2 bus. */
#define G2_8BP_RESET    0x0480              /* I/O - address for bus reset. */
#define HIT_REGC(reg)   (volatile uint8_t *)(reg)
static volatile uint8_t *hit_g2reset = HIT_REGC(0xa0600000);    

#define HIT_DETECTED	    1               /* HIT was detected. */
#define HIT_NOT_DETECTED	2           /* HIT wasn't detected. */

typedef unsigned char		uint8;

/* Prototypes of functions and extern declarations. */
extern int init_module_HIT(void) __init;
extern void cleanup_module_HIT(void) __exit;

static int init_HIT(struct net_device *dev);
static int detect_HIT(void);
static void exit_HIT(struct net_device *dev);

/* Handlers for net_device structure. */
static int net_open(struct net_device *dev);
static int net_send_packet(struct sk_buff *skb, struct net_device *dev);
static irqreturn_t net_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void net_rx(struct net_device *dev);
static void net_timeout(struct net_device *dev);
static int net_close(struct net_device *dev);
static struct net_device_stats *net_get_stats(struct net_device *dev);      
static void set_multicast_list(struct net_device *dev);                     

/* Useful internal functions. */
static void strobe_eeprom(void);
static void read_eeprom(uint8 *data);
static void switch_bank(int bank);
