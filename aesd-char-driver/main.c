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

MODULE_AUTHOR("Tomas Strand");
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
    //struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
    PDEBUG("release");
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    size_t internal_offset;
    struct aesd_buffer_entry *entry;
    struct aesd_dev *dev = (struct aesd_dev *)filp->private_data;
    size_t to_copy;
    if(mutex_is_locked(&dev->lock)){
        retval = -ERESTARTSYS;
        goto out2;
    }
    mutex_lock(&dev->lock);

    PDEBUG("read bytes with offset %lld", *f_pos);
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(&dev->cbuffer, *f_pos, &internal_offset);
    PDEBUG("internal offset %lld", internal_offset);

    if (NULL == entry){
        //dev->cbuffer.out_offs = 0;
        retval = 0;
        *f_pos = 0;
        goto out;
    }
    //PDEBUG("reading: (%zu) %s", entry->size, entry->buffptr);

    // \n is missing from long
    to_copy = entry->size < count ? entry->size : count;
    if (entry->size > 0){
        char *ptr = entry->buffptr;
        ptr += internal_offset;
        PDEBUG("copying %lld bytes to user", to_copy);
        if (copy_to_user(buf, ptr, to_copy)) {
            // Failed copying to user buffer
            retval = -EFAULT;
            goto out;
        }
    }
    *f_pos = *f_pos + to_copy;
    retval = to_copy;

out:
    mutex_unlock(&dev->lock);

out2:
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    struct aesd_dev *dev = filp->private_data;
    struct aesd_buffer_entry entry;
    char *buffer;
    char *newline;
    char *overwritten;

    // if mutex is open we need to lock it
    if(!mutex_is_locked(&dev->lock)){
        mutex_lock(&dev->lock);
        PDEBUG("Took lock");
    } else if ( dev->buffer != NULL){
        // mutex is locked, and buffer is not null
        // we can continue to write to buffer
        PDEBUG("Already locked for us");
    } else {
        PDEBUG("Not able to get lock, abort");
        retval = -ERESTARTSYS;
        goto out;
    }

    // if dev->buffer is NULL and we don't
    // Read until we get a \n then we send the linebuffer to circular buffer
    if (dev->buffer == NULL){
        // cleared memory, allocate new
        dev->buffer = kmalloc(count, GFP_KERNEL);
        dev->allocated = count;
        if (NULL == dev->buffer) goto out;
    } else if (dev->allocated < (dev->used+count)) {
        // allocated memory, but we need more
        dev->buffer = krealloc(dev->buffer, dev->allocated+count, GFP_KERNEL);
        if (NULL == dev->buffer) goto out;
        dev->allocated += count;
    }
    buffer = dev->buffer;
    buffer += dev->used;
    // Else we have enough memory to copy data now
    if (0 != copy_from_user( buffer, buf, count)){
        // We did not manage to copy all the data, abort and free allocated buffer
        retval = -EFAULT;
        kfree(dev->buffer);
        mutex_unlock(&dev->lock);

        goto out;
    }
    // search for newline  @TODO reduce datalen if \n is not the last char
    newline = strchr(buffer, '\n');

    // update used memory size, after we have copied data
    dev->used += count;
    retval = count;
    if (NULL != newline){
        // sometimes we got too much data so, lets abort at buffer-len
        dev->buffer[dev->used+1] = 0;

        // create an entry of our buffer
        entry.buffptr = dev->buffer;
        entry.size = dev->used;

        overwritten = aesd_circular_buffer_add_entry(&dev->cbuffer, &entry);
        if (NULL != overwritten) {
            printk("Freeing");
            kfree(overwritten);
        }
        // Clear buffer since we have sent it to circular buffer
        // (dev->buffer pointer is now owned by the circular buffer, and will be freed
        //  when overwritten)
        dev->used = 0;
        dev->allocated = 0;
        dev->buffer = NULL;
        //PDEBUG("GOT LINE %ld", entry.size);
        // free mutex
        mutex_unlock(&dev->lock);


    }
    //*f_pos += count;
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
    // Need to be done before we tell the kernel about cdev
    mutex_init(&aesd_device.lock);
    //struct aesd_dev *aesd_d = (struct aesd_dev*)

    result = aesd_setup_cdev(&aesd_device);
    if( result ) {
        unregister_chrdev_region(dev, 1);
    }

    return result;
}

void aesd_cleanup_module(void)
{
    int i;
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    // Should go trhough buffer and free all allocated memories
    for(i=0; i<AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; i++){
        if (aesd_device.cbuffer.entry[i].buffptr != NULL){
            kfree(&aesd_device.cbuffer.entry[i].buffptr);
        }
    }
    if (NULL != aesd_device.buffer) {
        kfree(&aesd_device.buffer);
    }
    cdev_del(&aesd_device.cdev);

    // clear allocated memories
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
