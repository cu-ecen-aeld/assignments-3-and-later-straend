/**
 * @file aesdchar.c
 * @brief Functions and data related to the AESD char driver implementation
 *
 * Based on the implementation of the "scull" device driver, found in
 * Linux Device Drivers example code.
 *
 * @author Dan Walkes
 * @date 2019-10-22
 * @copyright Copyright (c) 2019
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/printk.h>
#include <linux/types.h>
#include <linux/cdev.h>
#include <linux/fs.h> // file_operations
#include "aesdchar.h"
#include "aesd-circular-buffer.h"
#include <linux/slab.h>
#include <linux/string.h>

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Tomas Strand"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;
const char *hello = "HELLO\n";

int aesd_open(struct inode *inode, struct file *filp)
{
    struct aesd_dev *dev;
    PDEBUG("open");
    // Add an aesd_dev to our filepointer for use in read, write and other functions
	dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
	filp->private_data = dev; 
    
	
    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");

    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    size_t asd;
    struct aesd_buffer_entry *entry;
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
    size_t to_copy;
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    //PDEBUG("searching for entry at: %lli" ,*f_pos);
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->cbuffer, *f_pos, &asd);
    
    if (NULL == entry){
        PDEBUG("No data to read, or end of");
        retval = 0;
        goto out;
    }
    to_copy = entry->size < count ? entry->size : count;
    PDEBUG("found entry: %zu read %zu", entry->size, to_copy);
    if (entry->size > 0){
        if(++dev->cbuffer.out_offs>=AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) dev->cbuffer.out_offs = 0;
        if (copy_to_user(buf, entry->buffptr, to_copy)) {
            PDEBUG("Fauled copying");
            retval = -EFAULT;   // bad address
		    goto out;
	    }
    }
    *f_pos = *f_pos + to_copy;
    retval = to_copy;
out:
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry entry;
    char *newline;
    char *overwritten;

    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);


    // Read until we get a \n then we send the linebuffer to circular buffer
    if (dev->buffer == NULL){
        // cleared memory, allocate new
        PDEBUG("Allocationg %zu bytes", count);
        dev->buffer = kmalloc(count, GFP_KERNEL);
        if (NULL == entry.buffptr) goto out;
        //dev->allocated = count;
        dev->allocated = ksize(dev->buffer);

    } else if (dev->allocated < (dev->used+count)) {
        // allocated memory, but we need more
        PDEBUG("ReAllocationg (adding) %zu bytes to %zu", count, dev->allocated);
        dev->buffer = krealloc(dev->buffer, dev->allocated+count, GFP_KERNEL);
        if (NULL == entry.buffptr) goto out;
        //dev->allocated += count;
        dev->allocated = ksize(dev->buffer);
    }
    // Else we have enough memory to copy data now
    if (0 != copy_from_user( dev->buffer+dev->used, buf, count)){
        PDEBUG("Failed copying");
        retval = -EFAULT;
        kfree(dev->buffer);
        goto out;
    }else   {
        PDEBUG("copiws");
    }
    // update used memory size, after we have copied data
    dev->buffer[count] = 0;
    dev->used += count;
    PDEBUG("\t>'%s'", dev->buffer);
   
    // search for newline  @TODO reduce datalen if \n is not the last char
    newline = strchr(dev->buffer, '\n');
    if (NULL != newline){
        PDEBUG("Founs nwqline");
        
        entry.buffptr = dev->buffer;
        entry.size = dev->used;
        overwritten = aesd_circular_buffer_add_entry(&dev->cbuffer, &entry);
        if (NULL != overwritten) {
            kfree(overwritten);
        }
        PDEBUG("Wrote (%ld) %ld: '%s'", dev->used, entry.size, dev->buffer);
        // Clear buffer since we have sent it to circular buffer
        // (dev->buffer pointer is now owned by the circular buffer, and will be freed
        //  when overwritten)
        dev->used = 0;
        dev->allocated = 0;
        dev->buffer = NULL;
        
    } else   {
        PDEBUG("no newline");

    }
    *f_pos += count;
    retval = count;


out:
    return retval;
}
struct file_operations aesd_fops = {
    .owner =    THIS_MODULE,
    .read =     aesd_read,
    .write =    aesd_write,
    .open =     aesd_open,
    .release =  aesd_release,
};

static int aesd_setup_cdev(struct aesd_dev *dev)
{
    int err, devno = MKDEV(aesd_major, aesd_minor);

    cdev_init(&dev->cdev, &aesd_fops);
    dev->cdev.owner = THIS_MODULE;
    dev->cdev.ops = &aesd_fops;
    err = cdev_add (&dev->cdev, devno, 1);
    if (err) {
        printk(KERN_ERR "Error %d adding aesd cdev", err);
    }
    aesd_circular_buffer_init(&dev->cbuffer);


    return err;
}



int aesd_init_module(void)
{
    dev_t dev = 0;
    int result;
    result = alloc_chrdev_region(&dev, aesd_minor, 1,
            "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device,0,sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */

    result = aesd_setup_cdev(&aesd_device);

    if( result ) {
        unregister_chrdev_region(dev, 1);
    }
    return result;

}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific poritions here as necessary
     */

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
