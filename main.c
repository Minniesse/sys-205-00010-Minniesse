/**
 * @file main.c
 * @brief A simple character device driver for the Linux kernel.
 *
 * This file contains the implementation of a character device driver for the Linux kernel.
 * The driver provides read and write operations, as well as IOCTL commands for setting the mode and retrieving buffer length.
 * It also keeps track of the number of reads and writes performed.
 * Wrtiien by Paksaran Kongkaew
 * Date: 2024-02-09
 */

/* Include the necessary headers */
#include <linux/atomic.h>   // for atomic_t
#include <linux/cdev.h>     // for cdev_* functions
#include <linux/delay.h>    // for msleep
#include <linux/device.h>   // for device_create
#include <linux/fs.h>       // for file_operations
#include <linux/init.h>     // for __init and __exit
#include <linux/module.h>   // Specifically, a module
#include <linux/printk.h>   // for printk and pr_*
#include <linux/types.h>    // for dev_t
#include <linux/uaccess.h>  // for get_user and put_user 
#include <linux/version.h>  // for KERNEL_VERSION
#include <linux/ioctl.h>    // for _IOW, _IOR, _IOWR
#include <linux/string.h>   // for strnlen_user
#include <linux/slab.h>     // for kmalloc and kfree

/* Define the necessary constants */
#define MAJOR_NUM 100 
#define MESSAGE_MAX_LEN 256
#define DEBUG_PRINT(fmt, args...) \
    printk(KERN_DEBUG "heartydev [%lld]: " fmt, ktime_get_real_ns(), ##args)

/* define the IOCTL's message of the device driver */
#define HEARTYDEV_WRITE_CNT _IOW(MAJOR_NUM, 0, char *) 
#define HEARTYDEV_READ_CNT _IOR(MAJOR_NUM, 1, char *) 
#define HEARTYDEV_BUF_LEN _IOWR(MAJOR_NUM, 2, int) 

/* define the IOCTL's mode of the device driver */
#define HEARTYDEV_SET_MODE _IOW(MAJOR_NUM, 3, int)
#define HEARTYDEV_NORMAL 0
#define HEARTYDEV_UPPER 1
#define HEARTYDEV_LOWER 2

enum { 
    CDEV_NOT_USED = 0, 
    CDEV_EXCLUSIVE_OPEN = 1, 
}; 

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A simple character device driver");
MODULE_AUTHOR("pkongkae@cmkl.ac.th>");

static int heartydev_open(struct inode *inode, struct file *file);
static int heartydev_release(struct inode *inode, struct file *file);
static long heartydev_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static ssize_t heartydev_read(struct file *file, char __user *buf, size_t count, loff_t *offset);
static ssize_t heartydev_write(struct file *file, const char __user *buf, size_t count, loff_t *offset);

static const struct file_operations heartydev_fops = {
    .open = heartydev_open,
    .release = heartydev_release,
    .unlocked_ioctl = heartydev_ioctl,
    .read = heartydev_read,
    .write = heartydev_write};

dev_t dev = 0;
static struct class *heartydev_class = NULL;
static struct cdev heartydev_cdev;

/* functions called */
static int read_count = 0;
static int write_count = 0;
static int current_mode = HEARTYDEV_UPPER;

static char *message = NULL;
static size_t message_capacity = 0;
static int message_len = 0;
static atomic_t already_open = ATOMIC_INIT(CDEV_NOT_USED); 

/**
 * @brief uevent function for the device class
 * 
 * @param dev the device
 * @param env the environment
 * @return int 0
 */

static int heartydev_uevent(struct device *dev, struct kobj_uevent_env *env) {
    add_uevent_var(env, "DEVMODE=%#o", 0666);
    return 0;
}

static int __init heartydev_init(void) {
    printk(KERN_INFO "----heartydev INIT START----\n");
    if (alloc_chrdev_region(&dev, 0, 1, "heartydev") < 0) {
        printk(KERN_ALERT "heartydev registration failed\n");
        return -1;
    }

    /* allocate memory for the buffer */
    message = kmalloc(MESSAGE_MAX_LEN, GFP_KERNEL);
    if (!message) {
        printk(KERN_ALERT "Failed to allocate initial message buffer\n");
        unregister_chrdev_region(dev, 1);
        return -ENOMEM;
    }
    memset(message, 0, MESSAGE_MAX_LEN);
    message_capacity = MESSAGE_MAX_LEN;
    message_len = 0;

    /* ********* Kernel Version 5.8.0-63-generic ********* */
    heartydev_class = class_create(THIS_MODULE, "heartydev");
    heartydev_class->dev_uevent = heartydev_uevent;
    cdev_init(&heartydev_cdev, &heartydev_fops);
    cdev_add(&heartydev_cdev, MKDEV(MAJOR(dev), 0), 1);
    device_create(heartydev_class, NULL, MKDEV(MAJOR(dev), 0), NULL,
                  "heartydev");

    // printk(KERN_ALERT "heartydev registered\n");
    printk(KERN_INFO "----heartydev INIT END----\n");

    return 0;
}

static void __exit heartydev_exit(void) {
    device_destroy(heartydev_class, MKDEV(MAJOR(dev), 0));
    class_unregister(heartydev_class);
    class_destroy(heartydev_class);
    unregister_chrdev_region(MKDEV(MAJOR(dev), 0), MINORMASK);
    printk(KERN_DEBUG "----heartydev memory free----\n");
    kfree(message);
}

static int heartydev_open(struct inode *inode, struct file *file) {
    printk("heartydev_open\n");
    return 0;
}

static int heartydev_release(struct inode *inode, struct file *file) {
    printk("heartydev_release\n");
    printk(KERN_INFO "heartydev: Total writes: %d, Total reads: %d\n", write_count, read_count);
    return 0;
}

static long heartydev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    // int ret = 0;
    // size_t len;
    char __user *user_buf;
    int mode;

    switch (cmd) {
    case HEARTYDEV_WRITE_CNT:
        // user_buf = (char __user *)arg;
        // len = strnlen_user(user_buf, MESSAGE_MAX_LEN);
        // if (len == 0 || len > MESSAGE_MAX_LEN) {
        //     pr_err("heartydev: Invalid input length\n");
        //     return -EINVAL;
        // }
        // if (!access_ok(user_buf, len)) {
        //     pr_err("heartydev: Invalid user space pointer\n");
        //     return -EFAULT;
        // }
        // ret = heartydev_write(file, user_buf, len - 1, NULL);  // -1 to remove null terminator
        // if (ret < 0) {
        //     pr_err("heartydev: Write failed with error %d\n", ret);
        //     return ret;
        // }
        // get_user(write_count, (int __user *)arg);
        return write_count;

    case HEARTYDEV_READ_CNT:
        // user_buf = (char __user *)arg;
        // if (!access_ok(user_buf, message_len)) {
        //     pr_err("heartydev: Invalid user space pointer for read\n");
        //     return -EFAULT;
        // }
        // ret = heartydev_read(file, user_buf, message_len, NULL);
        // put_user(read_count, (int __user *)arg);
        // printk("heartydev: Read count %d\n", read_count);
        return read_count;

    case HEARTYDEV_BUF_LEN:
        if (!access_ok((int __user *)arg, sizeof(int))) {
            pr_err("heartydev: Invalid user space pointer for buffer length\n");
            return -EFAULT;
        }
        if (put_user(message_len, (int __user *)arg)) {
            pr_err("heartydev: Failed to copy buffer length to user space\n");
            return -EFAULT;
        }
        // printk("heartydev: Buffer length is %d\n", message_len);
        // put_user(message_len, (int __user *)arg);
        return message_len;
    
    case HEARTYDEV_SET_MODE:
        if (get_user(mode, (int __user *)arg)) {
            pr_err("heartydev: Failed to get mode from user space\n");
            return -EFAULT;
        }

        if (mode < HEARTYDEV_NORMAL || mode > HEARTYDEV_LOWER)
            return -EINVAL;

        current_mode = mode;
        pr_info("heartydev: mode set to %d\n", current_mode);
        return 0;

    default:
        return -ENOTTY;
    }
}


static ssize_t heartydev_read(struct file *file, char __user *buf, size_t count,
                              loff_t *offset) {
    ssize_t bytes_to_read;
    char *temp_buf;
    size_t i = 0;
    
    if (!message) {
        pr_err("heartydev: message buffer is NULL\n");
        return -ENOMEM;
    }
    
    if (offset != NULL && *offset >= message_len) {
        return 0;
    }
    
    bytes_to_read = min(message_len - (offset ? *offset : 0), count);
    if (bytes_to_read <= 0) {
        return 0;
    }
    
    temp_buf = kmalloc(bytes_to_read, GFP_KERNEL);
    if (!temp_buf) {
        pr_err("heartydev: Failed to allocate temporary buffer\n");
        return -ENOMEM;
    }

    memcpy(temp_buf, message + (offset ? *offset : 0), bytes_to_read);

    switch (current_mode) {
    case HEARTYDEV_UPPER:
        for (i = 0; i < bytes_to_read; i++)
            if (temp_buf[i] >= 'a' && temp_buf[i] <= 'z')
                temp_buf[i] -= 32;
        break;
    case HEARTYDEV_LOWER:
        for (i = 0; i < bytes_to_read; i++)
            if (temp_buf[i] >= 'A' && temp_buf[i] <= 'Z')
                temp_buf[i] += 32;
        break;
    case HEARTYDEV_NORMAL:
    default:
        break;
    }

    if (copy_to_user(buf, temp_buf, bytes_to_read)) {
        pr_err("heartydev: Failed to copy data to user space\n");
        kfree(temp_buf);
        return -EFAULT;
    }

    if (offset) {
        *offset += bytes_to_read;
    }
    kfree(temp_buf);
    read_count++;
    pr_debug("heartydev: Read %zd bytes, read_count=%d\n", bytes_to_read, read_count);
    printk("heartydev: Read count %d\n", read_count);

    return bytes_to_read;
}

static ssize_t heartydev_write(struct file *file, const char __user *buf,
                               size_t count, loff_t *offset) {

    printk("heartydev_write called\n");
    // printk("device_write(%p,%p,%zu)", file, buf, count);

    if (count > MESSAGE_MAX_LEN - 1)
        count = MESSAGE_MAX_LEN - 1;

    if (copy_from_user(message, buf, count)) {
        pr_err("heartydev: Failed to copy data from user space\n");
        return -EFAULT;
    }

    message[count] = '\0';
    message_len = count;
    write_count++;
    // printk("heartydev: Wrote %zu bytes\n", message_len);
    printk("heartydev: Write count %d\n", write_count);

    return message_len;
}

module_init(heartydev_init);
module_exit(heartydev_exit);