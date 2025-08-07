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

#define MIN(x, y) (((x) < (y)) ? (x) : (y))

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("nazim1997"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    return 0;  // No per-file data needed with global approach
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    return 0;  // No per-file cleanup needed
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    ssize_t total_bytes_read = 0;
    PDEBUG("read %zu bytes with offset %lld", count, *f_pos);
    
    mutex_lock(&aesd_device.device_lock);
    
    while (count > 0 && total_bytes_read < count) {
        struct aesd_buffer_entry *entry;
        size_t entry_offset_byte_rtn;
        
        entry = aesd_circular_buffer_find_entry_offset_for_fpos(aesd_device.buffer,
                *f_pos, &entry_offset_byte_rtn);
        
        if (!entry) {
            // No more data to read - end of buffer
            break;
        }
        
        size_t available_bytes = entry->size - entry_offset_byte_rtn;
        size_t bytes_to_copy = MIN(count - total_bytes_read, available_bytes);
        size_t bytes_not_copied = copy_to_user(buf + total_bytes_read, 
                                             entry->buffptr + entry_offset_byte_rtn, 
                                             bytes_to_copy);
        
        if (bytes_not_copied != 0) {
            retval = -EFAULT;
            break;
        }
        
        *f_pos += bytes_to_copy;
        total_bytes_read += bytes_to_copy;
    }
    
    mutex_unlock(&aesd_device.device_lock);
    return total_bytes_read > 0 ? total_bytes_read : retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld", count, *f_pos);
    
    mutex_lock(&aesd_device.device_lock);
    char *kernel_buf = kmalloc(count, GFP_KERNEL);

    if (aesd_device.incomplete_cmd.buffer) {
        PDEBUG("CMD BUFFER FULL: previous command is : %s", aesd_device.incomplete_cmd.buffer);
    } else {
        PDEBUG("CMD BUFFER EMPTY : previous command was empty");
    }

    if (!kernel_buf) {
        PDEBUG("Failed to allocate kernel buffer");
        mutex_unlock(&aesd_device.device_lock);
        return -ENOMEM;
    }

    if (copy_from_user(kernel_buf, buf, count)) {
        PDEBUG("Failed to copy buf user space to kernel buffer");
        kfree(kernel_buf);
        mutex_unlock(&aesd_device.device_lock);
        return -EFAULT;
    }
    
    PDEBUG("writing string %s to kernel buf", kernel_buf);
    
    if (memchr(kernel_buf, '\n', count)) {
        PDEBUG("a new line detected");
        // Complete command - add to circular buffer (exclude the newline)
        struct aesd_buffer_entry entry;
        
        if (aesd_device.incomplete_cmd.buffer) {
            PDEBUG("the previous command is not empty and buffer = %s", aesd_device.incomplete_cmd.buffer);
            // Combine accumulated data with current write (keep the newline)
            entry.buffptr = kmalloc(aesd_device.incomplete_cmd.size + count, GFP_KERNEL);
            if (!entry.buffptr) {
                kfree(kernel_buf);
                mutex_unlock(&aesd_device.device_lock);
                return -ENOMEM;
            }
            memcpy((void*)entry.buffptr, aesd_device.incomplete_cmd.buffer, aesd_device.incomplete_cmd.size);
            memcpy((void*)entry.buffptr + aesd_device.incomplete_cmd.size, kernel_buf, count);  // Include newline
            entry.size = aesd_device.incomplete_cmd.size + count;  // Include newline in size
            
            kfree(aesd_device.incomplete_cmd.buffer);
            aesd_device.incomplete_cmd.buffer = NULL;
            aesd_device.incomplete_cmd.size = 0;
            kfree(kernel_buf);
        } else {
            PDEBUG("new line but previous command is empty");
            // Just current write (keep the newline)
            entry.buffptr = kmalloc(count, GFP_KERNEL);
            if (!entry.buffptr) {
                kfree(kernel_buf);
                mutex_unlock(&aesd_device.device_lock);
                return -ENOMEM;
            }
            memcpy((void*)entry.buffptr, kernel_buf, count);  // Include newline
            entry.size = count;  // Include newline in size
            kfree(kernel_buf);
        }
        
        aesd_circular_buffer_add_entry(aesd_device.buffer, &entry);
        retval = count;  // Return bytes from THIS write operation
    } else {
        PDEBUG("THIS IS A INCOMPLETE COMMAND NO NEWLINE DETECTED");
        // Incomplete command - accumulate data
        if (aesd_device.incomplete_cmd.buffer) {
            char *new_buffer = krealloc(aesd_device.incomplete_cmd.buffer, 
                                      aesd_device.incomplete_cmd.size + count, GFP_KERNEL);
            if (!new_buffer) {
                kfree(kernel_buf);
                mutex_unlock(&aesd_device.device_lock);
                return -ENOMEM;
            }
            aesd_device.incomplete_cmd.buffer = new_buffer;
            memcpy(aesd_device.incomplete_cmd.buffer + aesd_device.incomplete_cmd.size, kernel_buf, count);
            aesd_device.incomplete_cmd.size += count;
        } else {
            aesd_device.incomplete_cmd.buffer = kmalloc(count, GFP_KERNEL);
            if (!aesd_device.incomplete_cmd.buffer) {
                kfree(kernel_buf);
                mutex_unlock(&aesd_device.device_lock);
                return -ENOMEM;
            }
            PDEBUG("COMMAND INCOMPLETE : copying kernel_buff=%s to buffer", kernel_buf);
            memcpy(aesd_device.incomplete_cmd.buffer, kernel_buf, count);
            PDEBUG("COMMAND INCOMPLETE : value of buffer after copy = %s", aesd_device.incomplete_cmd.buffer);
            aesd_device.incomplete_cmd.size = count;
        }
        kfree(kernel_buf);
        retval = count;  // Return bytes from THIS write operation
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
    result = alloc_chrdev_region(&dev, aesd_minor, 1, "aesdchar");
    aesd_major = MAJOR(dev);
    if (result < 0) {
        printk(KERN_WARNING "Can't get major %d\n", aesd_major);
        return result;
    }
    memset(&aesd_device, 0, sizeof(struct aesd_dev));

    /**
     * TODO: initialize the AESD specific portion of the device
     */
    aesd_device.incomplete_cmd.buffer = NULL;
    aesd_device.incomplete_cmd.size = 0;
    aesd_device.buffer = kmalloc(sizeof(struct aesd_circular_buffer), GFP_KERNEL);
    if (!aesd_device.buffer) {
        unregister_chrdev_region(dev, 1);
        return -ENOMEM;
    }
    aesd_circular_buffer_init(aesd_device.buffer);
    mutex_init(&aesd_device.device_lock);

    result = aesd_setup_cdev(&aesd_device);

    if (result) {
        kfree(aesd_device.buffer);
        unregister_chrdev_region(dev, 1);
    }
    return result;
}

void aesd_cleanup_module(void)
{
    dev_t devno = MKDEV(aesd_major, aesd_minor);

    cdev_del(&aesd_device.cdev);

    /**
     * TODO: cleanup AESD specific portions here as necessary
     */
    // Clean up incomplete command if any
    if (aesd_device.incomplete_cmd.buffer) {
        kfree(aesd_device.incomplete_cmd.buffer);
    }
    
    // Freeing entries from circular buffer
    if (aesd_device.buffer) {
        char index = 0;
        struct aesd_buffer_entry *entry;
        AESD_CIRCULAR_BUFFER_FOREACH(entry, aesd_device.buffer, index) {
            if (entry->buffptr) {
                kfree(entry->buffptr);
            }
        }
        kfree(aesd_device.buffer);
    }
    
    mutex_destroy(&aesd_device.device_lock);
    unregister_chrdev_region(devno, 1);
}

module_init(aesd_init_module);
module_exit(aesd_cleanup_module);