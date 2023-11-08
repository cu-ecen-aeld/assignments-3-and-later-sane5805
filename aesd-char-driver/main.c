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
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include "aesdchar.h"
#include "aesd_ioctl.h"

int aesd_major =   0; // use dynamic major
int aesd_minor =   0;

MODULE_AUTHOR("Saurav Negi"); /** TODO: fill in your name **/
MODULE_LICENSE("Dual BSD/GPL");

struct aesd_dev aesd_device;

int aesd_open(struct inode *inode, struct file *filp)
{
    PDEBUG("open");
    /**
     * TODO: handle open
     */
    struct aesd_dev *dev;

    dev = container_of(inode->i_cdev, struct aesd_dev, cdev);
    filp->private_data = dev;

    return 0;
}

int aesd_release(struct inode *inode, struct file *filp)
{
    PDEBUG("release");
    /**
     * TODO: handle release
     */
    return 0;
}

ssize_t aesd_read(struct file *filp, char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = 0;
    PDEBUG("read %zu bytes with offset %lld",count,*f_pos);


    // Check for valid pointers/parameters
    if (!filp || !buf || !f_pos) {
        // Invalid memory address
        return -EFAULT; 
    }

    // Get the AESD device from the file structure
    struct aesd_dev *device = (struct aesd_dev *)filp->private_data;


    // Lock the mutex (interruptible)
    if (mutex_lock_interruptible(&device->lock)) {
        PDEBUG(KERN_ERR "could not acquire mutex lock");
        return -ERESTARTSYS;
    }

    // Variables for circular buffer entry and offset
    struct aesd_buffer_entry *read_entry= NULL;
    ssize_t read_offset = 0;

    // Find the entry and offset for the given file position
    read_entry = aesd_circular_buffer_find_entry_offset_for_fpos(&(device->circle_buff), *f_pos, &read_offset);
    if (!read_entry) {
        goto error_handling;
    }

    // Check if bytes_to_read exceeds the available data
    if (count > (read_entry->size - read_offset)) {
            count = read_entry->size - read_offset;
    }

    // Read data using copy_to_user
    ssize_t unread_count = copy_to_user(buf, (read_entry->buffptr + read_offset), count);

    // Calculate the number of bytes successfully copied and update file_pos
    retval = count - unread_count;
    *f_pos += retval;

error_handling:
    mutex_unlock(&(device->lock));

    return retval;
}

ssize_t aesd_write(struct file *filp, const char __user *buf, size_t count,
                loff_t *f_pos)
{
    ssize_t retval = -ENOMEM;
    PDEBUG("write %zu bytes with offset %lld",count,*f_pos);

    struct aesd_dev *device;
    const char *write_entry = NULL;
    
    
    // Check for invalid parameters
    if (!filp || !buf || !f_pos) {
        // Invalid memory address
        return -EFAULT;
    }

    if (count == 0) {
        // Nothing to write
        return 0;
    }

    
    // Retrieve the AESD device from the file structure
    device = (struct aesd_dev *)filp->private_data;

    // lock mutex (interruptible)
    if (mutex_lock_interruptible(&(device->lock))) {
        PDEBUG(KERN_ERR "Failed to acquire the mutex lock");
        return -ERESTARTSYS;
    }

    // memory allocation with kmalloc when no prior allocations present
    if (0 == device->circle_buff_entry.size) {

        device->circle_buff_entry.buffptr = kmalloc(count * sizeof(char), GFP_KERNEL);

        if (!device->circle_buff_entry.buffptr) {
            PDEBUG("malloc failed");
            goto error_handling_write;
        }
    }
    else {
        // memory reallocation when prior allocations is present
        device->circle_buff_entry.buffptr = krealloc(device->circle_buff_entry.buffptr, (device->circle_buff_entry.size + count) * sizeof(char), GFP_KERNEL);

        if (!device->circle_buff_entry.buffptr) {
            PDEBUG("realloc failed");
            goto error_handling_write;
        }
    }

    // Copy data from the user space buffer to the current command buffer
    ssize_t  unwritten_count = copy_from_user((void *)(device->circle_buff_entry.buffptr + device->circle_buff_entry.size), buf, count);
    retval = count - unwritten_count;
    device->circle_buff_entry.size += retval;

    // Check for '\n' character in the command; if found, add the entry to the circular buffer
    if (memchr(device->circle_buff_entry.buffptr, '\n', device->circle_buff_entry.size)) {
        
        write_entry = aesd_circular_buffer_add_entry(&device->circle_buff, &device->circle_buff_entry);

        if(write_entry) {
            // freeing space
            kfree(write_entry);
        }

        // cleaning parameters
        device->circle_buff_entry.size = 0;
        device->circle_buff_entry.buffptr = NULL;
    }

error_handling_write:
    mutex_unlock(&device->lock);

    return retval;
}


static long aesd_adjust_file_offset(struct file *filp, unsigned int write_cmd, unsigned int write_cmd_offset)
{
    uint8_t position = 0;
    long result = 0;
    struct aesd_buffer_entry *entry = NULL;
    struct aesd_dev *device = NULL;

    device = filp->private_data;

    if (filp == NULL) { return -EFAULT; }

    if (mutex_lock_interruptible(&(device->lock))) {

        PDEBUG(KERN_ERR "Unable to acquire mutex lock");

        return -ERESTARTSYS;
    }

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &device->circle_buff, position);
    
    if (write_cmd > position || write_cmd_offset >= (device->circle_buff.entry[write_cmd].size) || write_cmd > AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        result = -EINVAL;
    }
    else {

        if (!write_cmd) {

            filp->f_pos += write_cmd_offset;
        }
        else
        {
            for (position = 0; position < write_cmd; position++) {

                filp->f_pos += device->circle_buff.entry[position].size;
            }

            filp->f_pos += write_cmd_offset;
        }
    }

    mutex_unlock(&device->lock);
  
    return result;
}


long aesd_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int error_code = 0;
    long result = 0;
    struct aesd_seekto custom_seek;

    if(filp == NULL) { return -EFAULT; }
    
    if((_IOC_TYPE(cmd) != AESD_IOC_MAGIC) || (_IOC_NR(cmd) > AESDCHAR_IOC_MAXNR)) { return -ENOTTY; }
       

    if((_IOC_DIR(cmd) & _IOC_WRITE) || (_IOC_DIR(cmd) & _IOC_READ)) {

        error_code = !access_ok((void __user *)arg, _IOC_SIZE(cmd));


    }

    if(error_code) { return -EFAULT; }

    switch (cmd)
    {
        case AESDCHAR_IOCSEEKTO:

            if (copy_from_user(&custom_seek, (const void __user *)arg, sizeof(custom_seek))) {

                result = -EFAULT;
            }       
            else {

                result = aesd_adjust_file_offset(filp, custom_seek.write_cmd, custom_seek.write_cmd_offset);
            }
            break;

        default: 
            return -ENOTTY;
            break;
    }

    return result;
}


loff_t aesd_llseek(struct file *filp, loff_t offset, int whence)
{
    loff_t total_buffer_size  = 0;
    loff_t seek_position = 0;
    uint8_t entry_index  = 0;
    
    struct aesd_dev * device = NULL;
    struct aesd_buffer_entry *seek_entry = NULL;


    device = (struct aesd_dev *)filp->private_data;

    if (filp == NULL) { return -EFAULT; }


    if (mutex_lock_interruptible(&(device->lock))) {

        PDEBUG(KERN_ERR "Mutex lock was not acquired");
        return -ERESTARTSYS;
    }

    AESD_CIRCULAR_BUFFER_FOREACH(seek_entry, &device->circle_buff, entry_index) {

        total_buffer_size  += seek_entry->size;
    }

    seek_position = fixed_size_llseek(filp, offset, whence, total_buffer_size);

    mutex_unlock(&device->lock);

    return seek_position;
}


struct file_operations aesd_fops = {
    .owner =            THIS_MODULE,
    .read =             aesd_read,
    .write =            aesd_write,
    .open =             aesd_open,
    .release =          aesd_release,
    .llseek =           aesd_llseek,
    .unlocked_ioctl =   aesd_ioctl
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


    // initializing circular buffer
    aesd_circular_buffer_init(&aesd_device.circle_buff);

    // mutex initialization
    mutex_init(&aesd_device.lock);

    
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

    // free circular buffer entries
    struct aesd_buffer_entry *entry = NULL;
    uint8_t index = 0;

    // freeing buffer
    kfree(aesd_device.circle_buff_entry.buffptr);

    AESD_CIRCULAR_BUFFER_FOREACH(entry, &aesd_device.circle_buff, index) {

        if (entry->buffptr != NULL) {
            kfree(entry->buffptr);
        }
    }

    unregister_chrdev_region(devno, 1);
}



module_init(aesd_init_module);
module_exit(aesd_cleanup_module);
