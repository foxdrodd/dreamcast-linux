/* mlcd.c: A VMU LCD display device driver for Linux. */

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

#include "mlcd.h"

/* Debugging switch: 0=no debug messages ... 5=mess your logs. */
static unsigned int mlcd_debug = MLCD_NO_DEBUG;

/* Data structure describing callback functions for (dis-)connecting a device attached to the maple bus. */
static struct maple_driver mlcd_driver = {
    function:MAPLE_FUNC_LCD,
    name:"VMU LCD display",
    connect:mlcd_connect,
    disconnect:mlcd_disconnect,
};

/* Data structure describing callback functions for the character device corresponding to the display. */
static struct file_operations mlcd_ops = {
    owner: THIS_MODULE,
    write: mlcd_write,
    open: mlcd_open,
    release: mlcd_release,
};

static int mlcd_major = MLCD_MAJOR_DEFAULT;
static int mlcd_minor = 0;

static char *bitmap = NULL;

static struct dc_mlcd *vmu_display;

/*******************************************************************************************************************
 **
 ** Below this line, you'll find all functions related to drawing on the SEGA VMU LCD display.
 **
 *******************************************************************************************************************/
void mlcd_draw(struct dc_mlcd *display)
{
    if (mlcd_debug >= MLCD_FULL_DEBUG) printk(KERN_WARNING "MLCD: Call to draw: Init.\n");

    /* Initialize a new packet for the maple bus. */
    maple_init_mq(&(display->data->mq));

    /* Set up the new packet. */
    unsigned long *sendbuf = (unsigned long *) display->data->mq.recvbuf;
    sendbuf[0] = cpu_to_be32(MAPLE_FUNC_LCD);
    sendbuf[1] = 0; /* Block, phase and partition is zero. */

    /* If connected for the first time, we simply show the Tux image. :-) */
    if (!bitmap)
        memcpy(sendbuf + 2, &tuximg48x32, 48 * 4); /* 48 * 4 = 192 bytes */
    else
        memcpy(sendbuf + 2, bitmap, 48 * 4); /* 48 * 4 = 192 bytes */

    /* Adjust the parameter. */
    display->data->mq.port = display->data->dev->port;
    display->data->mq.unit = display->data->dev->unit;
    display->data->mq.command = MAPLE_COMMAND_BWRITE;
    display->data->mq.length = 2 + 48;
    display->data->mq.callback = NULL;
    display->data->mq.sendbuf = sendbuf;

    if ( maple_add_packet((struct mapleq *) &(display->data->mq)) )
        if (mlcd_debug >= MLCD_FULL_DEBUG) printk(KERN_WARNING "MLCD: Error transferring image.\n");
}

/*******************************************************************************************************************
 **
 ** Below this line, you'll find all functions related to the maple_bus_driver.
 **
 *******************************************************************************************************************/
static int mlcd_connect(struct maple_driver_data *d)
{
    MOD_INC_USE_COUNT;

    if (mlcd_debug >= MLCD_FULL_DEBUG) printk(KERN_WARNING "MLCD: Call to connect.\n");

    struct dc_mlcd *mlcd;

    if (!(mlcd = kmalloc(sizeof(struct dc_mlcd), GFP_KERNEL))) {
        if (mlcd_debug >= MLCD_INFO) printk(KERN_WARNING "MLCD: Error allocation memory.\n");
        return -ENOMEM;
    }

    memset(mlcd, 0, sizeof(struct dc_mlcd));

    /* Set up the data structure. */
    mlcd->minor = mlcd_minor;
    mlcd->data = d;
    d->private_data = mlcd;

    /* FIXME: Insert the new display into the linked list. */
    vmu_display = mlcd;

    /* Draw the initial Tux image. */
    mlcd_draw(vmu_display);
    
    return 0;
}

static void mlcd_disconnect(struct maple_driver_data *d)
{
    if (mlcd_debug >= MLCD_FULL_DEBUG) printk(KERN_WARNING "MLCD: Call to disconnect.\n");

    /* Release previously allocated memory. */
    if (bitmap != NULL)
        kfree(bitmap);
    bitmap = NULL;

    /* FIXME: Remove this driver from the linked list. */

    MOD_DEC_USE_COUNT;
}

/*******************************************************************************************************************
 **
 ** Below this line, you'll find all functions related to the cross space data transfer between user- and 
 ** kernelspace through a character device.
 **
 *******************************************************************************************************************/

/* This function is invoked each time the device /dev/mlcd0 is used by a userspace tool (e.g. dmesg > /dev/mlcd0).
   It's used for preparing the driver to use the proper device (if several devices are structured by a linked
   list). */
static int mlcd_open(struct inode *inode, struct file *file)
{
    if (mlcd_debug >= MLCD_FULL_DEBUG) printk(KERN_WARNING "MLCD: Call to mlcd_open.\n");

    /* Identify the proper linked list entry through our minor number. */
    file->private_data = (int*) MINOR(inode->i_rdev);
    
    return 0;
}

/* This function is invoked each time after the device /dev/mlcd0 is used by a userspace tool (e.g. dmesg > /dev/mlcd0).
   It's used for freeing used resources. */
static int mlcd_release(struct inode *inode, struct file *file)
{
    if (mlcd_debug >= MLCD_FULL_DEBUG) printk(KERN_WARNING "MLCD: Call to mlcd_release.\n");

    return 0;
}

static ssize_t mlcd_write(struct file *file, const char *buffer, size_t count, loff_t * pos)
{
    /* Only allow the transfer of exact 192 bytes because the display size is 48 * 32.
       So, we have to fill up 1536 bit = 192 bytes. */
    if ( (mlcd_debug == MLCD_NO_DEBUG) && (count != 192) )
        return -EIO; /* Just return I/O error. */

    /* Get the minor address of the used character device. */
    if (mlcd_debug >= MLCD_FULL_DEBUG) printk(KERN_WARNING "MLCD: Call to mlcd_write from minor: %d.\n", mlcd_minor);
    mlcd_minor = (int)file->private_data;

    /* Allocate memory for private data. */
    bitmap = kmalloc(192 * sizeof(char), GFP_KERNEL);
    if (bitmap == NULL) 
    {
        if (mlcd_debug >= MLCD_INFO) printk(KERN_WARNING "MLCD: Allocation of memory failed.\n");
        return -ENOMEM;
    }
    memset(bitmap, 0, 192 * sizeof(char));

    /* Get the data from user space. */
    copy_from_user(bitmap, buffer, count);
    if (mlcd_debug >= MLCD_FULL_DEBUG) printk(KERN_WARNING "MLCD: User wrote %d bytes: %s\n", count, bitmap);

    /* FIXME: Now, send the read data to the proper LCD display:
              1.) Traverse the linked list and find the proper "display" using the character device minor number.
              2.) Use the mlcd_draw(...) function for painting ;)
     */

    /* "Draw" the buffer. */
    mlcd_draw(vmu_display);

    /* Indicate the faultless transfer by return the count of read bytes. */
    return count;
}

/*******************************************************************************************************************
 **
 ** Below this line, you'll find all functions related to the (de-)initialization of the driver.
 **
 *******************************************************************************************************************/
int __init init_module_MLCD(void)
{
    printk(KERN_WARNING "SEGA VMU LCD display device driver (" DRV_NAME ".c, v" DRV_VERSION " " DRV_RELDATE" C. Berger).\n");

    /* Register driver at maple_bus_driver. */
    maple_register_driver(&mlcd_driver);

    /* Register or request a major number for the character device driver. */
    int result = register_chrdev(mlcd_major, "mlcd", &mlcd_ops);
    if(result < 0){
        printk(KERN_WARNING "MLCD: Error retrieving major %d.\n", mlcd_major);
        return result;
    }

    /* Dynamic assignment of our major device number. */
    if (mlcd_major == 0) mlcd_major = result;

    return 0;
}

void __exit cleanup_module_MLCD(void)
{
    /* Unregister the character device driver. */
    unregister_chrdev(mlcd_major, "mlcd");
    
    /* Unregister this driver at the maple_bus_driver. */
    maple_unregister_driver(&mlcd_driver);

    printk(KERN_WARNING "Unload SEGA VMU LCD display device driver.\n");
}

/* Module-depended stuff. */
#ifdef MODULE
MODULE_PARM(mlcd_major, "i");
MODULE_PARM_DESC(mlcd_major, "MLCD desired character device free major number, 0 for automatic assignment. If no number is given, MLCD_MAJOR_DEFAULT is used (see source for further information).");

MODULE_PARM(mlcd_debug, "i");
MODULE_PARM_DESC(mlcd_debug, "MLCD debug level: 0, 2, 5.");

MODULE_AUTHOR("Christian Berger <c.berger@tu-braunschweig.de>");
MODULE_DESCRIPTION("Device driver for the SEGA VMU LCD display.");
MODULE_LICENSE("GPL");
#endif

module_init(init_module_MLCD);
module_exit(cleanup_module_MLCD);
