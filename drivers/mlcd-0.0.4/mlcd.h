/* mlcd.h: A VMU LCD display device driver for Linux. */

/*  A Linux device driver for the VMU LCD display.

    Written Oct. 2003 by Christian Berger.

    This software may be used and distributed according to the
    terms of the GNU General Public License (GPL), incorporated
    herein by reference.  Drivers based on or derived from this
    code fall under the GPL and must retain the authorship,
    copyright and license notice.  This file is not a complete
    program and may only be used when the entire operating
    system is licensed under the GPL. See the file "COPYING" in 
    the main directory of this archive for more details.

    Contributors and Contacts:

        The author may be reached as c.berger@tu-braunschweig.de .

        Support and updates available at 
        http://linuxdc.sourceforge.net

    Acknowledgments:
        
        Dreamcast is a registered trademark of Sega, Inc.
    
   ChangeLog:
    10-15-2003  Christian Berger <c.berger@tu-braunschweig.de>
                Code cleanup.

    10-12-2003  Christian Berger <c.berger@tu-braunschweig.de>
                Implementation of the mlcd_draw function.
*/

/* TODO:

 * Support multiple LCD's using a simply linked list as described in the following code snippet:

    #include <linux/list.h>

    struct dc_mlcd *select_me, *mlcd = NULL;
    struct list_head *ptr;

    for (ptr = devices_list.next; ptr != &devices_list; ptr = ptr->next) {
        select_me = list_entry(ptr, struct dc_mlcd, list);

        if (select_me->minor == last_minor) {
            mlcd = select_me;
            break;
        }
    }
    
*/   

#include <asm/byteorder.h>          /* be32_to_cpu converting functions. */
#include <asm/uaccess.h>            /* Cross space copie functions. */

#include <linux/maple.h>            /* For all maple related stuff. */
#include <linux/types.h>            /* Type definitions. */
#include <linux/fs.h>               /* Character devices related stuff. */

#include <linux/errno.h>            /* Some useful errno's. */
#include <linux/init.h>             /* __init definitions. */
#include <linux/module.h>           /* General module handling routines. */

#include <linux/slab.h>

#define DRV_NAME                    "mlcd"
#define DRV_VERSION                 "0.0.4"
#define DRV_RELDATE                 "10/15/2003"

#define MLCD_MAJOR_DEFAULT  65      /* Default character device major number. */

#define MLCD_NO_DEBUG        0      /* No debug - messages. */
#define MLCD_INFO            2      /* For info purposes set MLCD_DEBUG to > 1. */
#define MLCD_FULL_DEBUG      5      /* For testing purposes set MLCD_DEBUG to > 4. */

/* Initial image "Tux Linux", created by myself :) */
static const char tuximg48x32[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x3f, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x3f, 0xc0, 0x88, 0x00, 0x00, 
    0x00, 0x3f, 0xe0, 0x80, 0x00, 0x00, 0x00, 0x6e, 0xe0, 0x8a, 0x94, 0xa0, 
    0x00, 0x5d, 0x60, 0x8b, 0x54, 0x40, 0x00, 0x5f, 0x60, 0x8a, 0x54, 0xa0, 
    0x00, 0x3f, 0xe0, 0xea, 0x4a, 0xa0, 0x00, 0x3f, 0xf0, 0x00, 0x00, 0x00, 
    0x00, 0x3f, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x3e, 0x70, 0x00, 0x00, 0x00, 
    0x00, 0x40, 0x38, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x3c, 0x00, 0x00, 0x00, 
    0x01, 0x80, 0x3c, 0x00, 0x00, 0x00, 0x01, 0x80, 0x1e, 0x00, 0x00, 0x00, 
    0x03, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x03, 0x00, 0x0f, 0x00, 0x00, 0x00, 
    0x02, 0x00, 0x0f, 0x80, 0x00, 0x00, 0x06, 0x00, 0x0f, 0x80, 0x00, 0x00, 
    0x06, 0x00, 0x0f, 0x80, 0x00, 0x00, 0x0e, 0x00, 0x0f, 0x80, 0x00, 0x00, 
    0x0e, 0x00, 0x0f, 0x80, 0x00, 0x00, 0x0f, 0x00, 0x1f, 0x80, 0x00, 0x00, 
    0x3f, 0x80, 0x3f, 0x80, 0x00, 0x00, 0x7f, 0xe0, 0x3f, 0xc0, 0x00, 0x00, 
    0x7f, 0xe0, 0x3f, 0xe0, 0x00, 0x00, 0x7f, 0xc0, 0x7f, 0xf0, 0x00, 0x00, 
    0xff, 0xe1, 0xff, 0xe0, 0x00, 0x00, 0xff, 0xff, 0xff, 0xc0, 0x00, 0x00, 
    0x3f, 0xff, 0xff, 0x00, 0x00, 0x00, 0x03, 0xe0, 0x3c, 0x00, 0x00, 0x00
};

/* Information about a SEGA VMU LCD display. */
struct dc_mlcd {
    struct maple_driver_data *data;

    /* FIXME: Later, minor is used to find the correct VMU LCD. */
    int minor;
};

/* Prototypes of functions and extern declarations. */
extern int init_module_MLCD(void) __init;
extern void cleanup_module_MLCD(void) __exit;

/* Handlers for maple_driver structure. */
static int mlcd_connect(struct maple_driver_data *d);
static void mlcd_disconnect(struct maple_driver_data *d);

/* Handlers for the character device. */
static int mlcd_open(struct inode *inode, struct file *file);
static int mlcd_release(struct inode *inode, struct file *file);
static ssize_t mlcd_write(struct file *file, const char *buffer, size_t count, loff_t * pos);

/* Routine for "painting" the display. */
void mlcd_draw(struct dc_mlcd *display);
