#include <linux/atomic.h> 
#include <linux/cdev.h> 
#include <linux/delay.h> 
#include <linux/device.h> 
#include <linux/fs.h> 
#include <linux/init.h> 
#include <linux/module.h> /* Specifically, a module */ 
#include <linux/printk.h> 
#include <linux/types.h> 
#include <linux/uaccess.h> /* for get_user and put_user */ 
#include <linux/version.h> 
#include <linux/ioctl.h> 
#include <linux/string.h>
#include <linux/slab.h>

/* define the major number */
#define BUF_LEN 80 
#define SUCCESS 0
#define MAJOR_NUM 100 
#define MESSAGE_MAX_LEN 256

#define DEBUG_PRINT(fmt, args...) \
    printk(KERN_DEBUG "heartydev [%lld]: " fmt, ktime_get_real_ns(), ##args)

/* define the IOCTL's message of the device driver*/
#define HEARTYDEV_WRITE_CNT _IOW(MAJOR_NUM, 0, char *) 
#define HEARTYDEV_READ_CNT _IOR(MAJOR_NUM, 1, char *) 
#define HEARTYDEV_BUF_LEN _IOWR(MAJOR_NUM, 2, int) 

enum { 
    CDEV_NOT_USED = 0, 
    CDEV_EXCLUSIVE_OPEN = 1, 
}; 

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("A simple character device driver");
MODULE_AUTHOR("... <...@cmkl.ac.th>");

static int heartydev_open(struct inode *inode, struct file *file);
static int heartydev_release(struct inode *inode, struct file *file);
static long heartydev_ioctl(struct file *file, unsigned int cmd,
                            unsigned long arg);
static ssize_t heartydev_read(struct file *file, char __user *buf, size_t count,
                              loff_t *offset);
static ssize_t heartydev_write(struct file *file, const char __user *buf,
                               size_t count, loff_t *offset);

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

static char *message = NULL;
static size_t message_capacity = 0;
static int message_len = 0;
static atomic_t already_open = ATOMIC_INIT(CDEV_NOT_USED); 

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
        return -ENOMEM;
    }
    message_capacity = MESSAGE_MAX_LEN;

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
    int ret = 0;

    switch (cmd) {
    case HEARTYDEV_WRITE_CNT:
        if (!access_ok((void __user *)arg, message_capacity)) {
            pr_err("heartydev: Invalid user space pointer\n");
            return -EFAULT;
        }
        ret = heartydev_write(file, (const char __user *)arg, message_capacity - 1, NULL);
        if (ret < 0) {
            pr_err("heartydev: Write failed with error %d\n", ret);
            return ret;
        }
        return ret;
    case HEARTYDEV_READ_CNT:
        if (!access_ok((void __user *)arg, message_capacity)) {
            pr_err("heartydev: Invalid user space pointer\n");
            return -EFAULT;
        }
        ret = heartydev_read(file, (char __user *)arg, message_capacity, NULL);
        return ret;
    case HEARTYDEV_BUF_LEN:
        if (copy_to_user((int __user *)arg, &message_len, sizeof(int))) {
            pr_err("heartydev: Failed to copy buffer length to user space\n");
            return -EFAULT;
        }
        // printk("heartydev: Buffer length %zu\n", message_len);
        return message_len;
    default:
        return -ENOTTY;
    }
    atomic_set(&already_open, CDEV_NOT_USED);
    return 0;
}


static ssize_t heartydev_read(struct file *file, char __user *buf, size_t count,
                              loff_t *offset) {
    ssize_t bytes_to_read;
    
    if (offset != NULL && *offset >= message_len) {
        return 0;
    }
    
    bytes_to_read = min(message_len - (offset ? *offset : 0), count);

    if (bytes_to_read <= 0) {
        return 0;
    }

    if (copy_to_user(buf, message + (offset ? *offset : 0), bytes_to_read)) {
        pr_err("heartydev: Failed to copy data to user space\n");
        return -EFAULT;
    }

    if (offset) {
        *offset += bytes_to_read;
    }
    // printk("offset=%lld\n", offset ? *offset : 0);
    read_count++;
    // printk("heartydev: message_len=%zu, offset=%lld, bytes_to_read=%zd\n", message_len, offset ? *offset : 0, bytes_to_read);
    // printk("heartydev: Read %zd bytes\n", bytes_to_read);
    printk("heartydev: Read count %d\n", read_count);

    return bytes_to_read;
}

static ssize_t heartydev_write(struct file *file, const char __user *buf,
                               size_t count, loff_t *offset) {

    char *new_message;
    size_t new_capacity;

    printk("heartydev_write called\n");
    // printk("device_write(%p,%p,%zu)", file, buf, count);

    // Check if we need to increase the buffer size
    if (count > message_capacity) {
        new_capacity = count + 1;  // +1 for null terminator
        new_message = krealloc(message, new_capacity, GFP_KERNEL);
        if (!new_message) {
            pr_err("heartydev: Failed to allocate memory\n");
            return -ENOMEM;
        }
        message = new_message;
        message_capacity = new_capacity;
    }

    if (copy_from_user(message, buf, count)) {
        pr_err("heartydev: Failed to copy data from user space\n");
        return -EFAULT;
    }

    message[message_capacity] = '\0';
    message_len = strlen(message);
    write_count++;

    // printk("heartydev: Wrote %zu bytes\n", message_len);
    printk("heartydev: Write count %d\n", write_count);

    return message_len;
}

module_init(heartydev_init);
module_exit(heartydev_exit);