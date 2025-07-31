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


int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("nazim1997"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

struct incomplete_command {
    char *buffer;
    size_t size;
};

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    struct incomplete_command *cmd = kmalloc(sizeof(struct incomplete_command), GFP_KERNEL);
    if (cmd) {
        cmd->buffer = NULL;
        cmd->size = 0;
        filp->private_data = cmd;
        return 0;
    }
    else {
        return -ENOMEM;
    }
    
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    struct incomplete_command *cmd = filp->private_data;
    if (cmd) {
        if (cmd->buffer) {
            kfree(cmd->buffer);
        }
        kfree(cmd);
    }
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle read
     */
    struct aesd_buffer_entry *entry;
    size_t entry_offset_byte_rtn;

    mutex_lock(&aesd_device.device_lock);
    entry = aesd_circular_buffer_find_entry_offset_for_fpos(aesd_device.buffer,
            *f_pos, &entry_offset_byte_rtn);

    if (entry) {
        size_t available_bytes = entry->size - entry_offset_byte_rtn;
        size_t copied_bytes = copy_to_user(buf, entry->buffptr + entry_offset_byte_rtn, MIN(count, available_bytes));
        if (copied_bytes != 0) {
            mutex_unlock(&aesd_device.device_lock);
            return -EFAULT;
        }
        else {
            *f_pos += MIN(count, available_bytes);
            retval = MIN(count, available_bytes);
        }
        
    }
    else {
        mutex_unlock(&aesd_device.device_lock);
        return -EINVAL;
    }
    mutex_unlock(&aesd_device.device_lock);
    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);
    /**
     * TODO: handle write
     */
    mutex_lock(&aesd_device.device_lock);
    char *kernel_buf = kmalloc(count, GFP_KERNEL);
    struct incomplete_command *cmd = (struct incomplete_command *) filp->private_data;

    if (!kernel_buf) {
        PDEBUG("Failed to allocate kernel buffer");
        mutex_unlock(&aesd_device.device_lock);
        return -ENOMEM;
    }

    if ( copy_from_user(kernel_buf, buf, count)) {
        PDEBUG("Failed to copy buf user space to kernel buffer");
        mutex_unlock(&aesd_device.device_lock);
        return -EFAULT;
    }
    
    if ( memchr(kernel_buf, '\n', count)) {
        struct aesd_buffer_entry entry;
        if (cmd->buffer) {
            entry.buffptr = kmalloc((cmd->size + count) * sizeof(char), GFP_KERNEL);
            memcpy(entry.buffptr, cmd->buffer, cmd->size);
            memcpy(entry.buffptr + cmd->size, kernel_buf, count);
            entry.size = cmd->size + count;
            kfree(cmd->buffer);
            cmd->buffer = NULL;
            cmd->size = 0;
            kfree(kernel_buf); 
        }
        else {
            entry.buffptr = kernel_buf;
            entry.size = count;
        }
        aesd_circular_buffer_add_entry(aesd_device.buffer, &entry);
        mutex_unlock(&aesd_device.device_lock);
        return entry.size;
    }
    else {
        if (cmd->buffer) {
            cmd->buffer = krealloc(cmd->buffer, cmd->size + count, GFP_KERNEL);
            memcpy(cmd->buffer + cmd->size, kernel_buf, count);
            cmd->size += count;
        }
        else {
            cmd->buffer = kmalloc(count, GFP_KERNEL);
            memcpy(cmd->buffer, kernel_buf, count);
            cmd->size = count;
        }
        kfree(kernel_buf);
        mutex_unlock(&aesd_device.device_lock);
        return cmd->size;
    }
        
    mutex_unlock(&aesd_device.device_lock);
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
    aesd_device.buffer = kmalloc(sizeof(struct aesd_circular_buffer), GFP_KERNEL);
    aesd_circular_buffer_init(aesd_device.buffer);
    mutex_init(&aesd_device.device_lock);

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
    // Freeing entries from circular buffer
    char index = 0;
    struct aesd_buffer_entry *entry;
    AESD_CIRCULAR_BUFFER_FOREACH(entry, aesd_device.buffer, index) {
        kfree(entry->buffptr);
    }
    mutex_destroy(&aesd_device.device_lock);
    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
