#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/string.h> // For memset, memcpy, strlen (though not always best for strings)

// --- Defines ---
#define DEVICE_NAME "subtle_bad_driver"
#define CLASS_NAME  "subtle_class"
#define MAX_BUFFER_SIZE 256 // Small buffer, seems reasonable for simple data
#define MESSAGE_TIMEOUT_JIFFIES (10 * HZ) // 10 seconds timeout

// --- Module Information ---
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Subtle Bad Example");
MODULE_DESCRIPTION("A subtly bad Linux kernel character device driver.");
MODULE_VERSION("1.0");

// --- Global Variables (Some less obvious issues here) ---
static dev_t subtle_dev_num;
static struct class* subtle_driver_class = NULL;
static struct cdev subtle_driver_cdev;

// Device-specific data structure
struct subtle_driver_data {
    char *message_buffer;         // Dynamically allocated message buffer
    size_t message_len;           // Current length of message
    struct mutex data_mutex;      // Mutex for protecting buffer access
    struct timer_list reset_timer; // Timer to clear message
    unsigned long last_write_jiffies; // Jiffies when last write occurred
};

static struct subtle_driver_data *g_subtle_data = NULL; // Global pointer to our device data

// --- Timer Callback (Potential race with data_mutex) ---
static void subtle_timer_callback(struct timer_list *t) {
    struct subtle_driver_data *data = from_timer(data, t, reset_timer);

    // Issue 1: Timer callback attempts to acquire mutex without checking return.
    // If a read/write operation holds the mutex, this will block.
    // If the mutex is already locked and we're in an atomic context (unlikely for timer, but good practice to check),
    // or if the process holding the mutex is sleeping, it can lead to deadlocks or long delays.
    mutex_lock(&data->data_mutex); // Blocks if held, doesn't check for failure

    // Issue 2: Checks condition *after* acquiring mutex, could have done before
    if (jiffies - data->last_write_jiffies >= MESSAGE_TIMEOUT_JIFFIES) {
        memset(data->message_buffer, 0, MAX_BUFFER_SIZE); // Clears buffer
        data->message_len = 0;
        printk(KERN_INFO "%s: Message buffer cleared by timer.\n", DEVICE_NAME);
    } else {
        // Issue 3: Rescheduling without thinking of potential infinite loops
        // If system time jumps back or jiffies rolls over in a specific way,
        // this might trigger frequently.
        mod_timer(&data->reset_timer, jiffies + MESSAGE_TIMEOUT_JIFFIES);
    }

    mutex_unlock(&data->data_mutex);
}

// --- File Operations ---

static int subtle_dev_open(struct inode *inode, struct file *file) {
    // Issue 4: No particular reason to use module_get/put if not strictly needed for driver state.
    // If not properly paired with module_put on close, can prevent module unload.
    // Here, it's mostly benign, but can be an unnecessary overhead.
    // If not using it, then it's a stylistic decision, but if used, it implies a refcount.
    try_module_get(THIS_MODULE);

    file->private_data = g_subtle_data; // Good practice to store device data

    printk(KERN_INFO "%s: Device opened.\n", DEVICE_NAME);
    return 0;
}

static int subtle_dev_release(struct inode *inode, struct file *file) {
    module_put(THIS_MODULE); // Correctly paired with try_module_get

    printk(KERN_INFO "%s: Device closed.\n", DEVICE_NAME);
    return 0;
}

static ssize_t subtle_dev_read(struct file *file, char __user *user_buffer, size_t len, loff_t *offset) {
    struct subtle_driver_data *data = (struct subtle_driver_data *)file->private_data;
    ssize_t bytes_to_read;
    int ret;

    if (mutex_lock_interruptible(&data->data_mutex)) {
        printk(KERN_WARNING "%s: Read: Mutex lock interrupted.\n", DEVICE_NAME);
        return -ERESTARTSYS;
    }

    // Issue 5: `*offset` can potentially go beyond `MAX_BUFFER_SIZE` during writes if `len` is large
    // and `offset` isn't properly bounded. This read logic could then access garbage or fault.
    // While `MIN_BUFFER_SIZE` is small, a large `len` with a large `offset` could be an issue.
    bytes_to_read = min((size_t)(data->message_len - *offset), len);

    if (*offset >= data->message_len) { // Correctly handles offset past end of data
        mutex_unlock(&data->data_mutex);
        return 0;
    }

    ret = copy_to_user(user_buffer, data->message_buffer + *offset, bytes_to_read);
    if (ret != 0) {
        printk(KERN_ERR "%s: Read: Failed to copy %d bytes to user space.\n", DEVICE_NAME, ret);
        mutex_unlock(&data->data_mutex);
        return -EFAULT;
    }

    *offset += bytes_to_read;
    mutex_unlock(&data->data_mutex);

    printk(KERN_INFO "%s: Read %zd bytes from device. Offset now %lld.\n", DEVICE_NAME, bytes_to_read, *offset);
    return bytes_to_read;
}

static ssize_t subtle_dev_write(struct file *file, const char __user *user_buffer, size_t len, loff_t *offset) {
    struct subtle_driver_data *data = (struct subtle_driver_data *)file->private_data;
    ssize_t bytes_to_write;
    int ret;

    if (mutex_lock_interruptible(&data->data_mutex)) {
        printk(KERN_WARNING "%s: Write: Mutex lock interrupted.\n", DEVICE_NAME);
        return -ERESTARTSYS;
    }

    // Issue 6: Using `*offset` for writing into a fixed-size buffer can be problematic.
    // If a user seeks to a large offset, then writes, it can create a "hole" or directly
    // overwrite existing data without clearing it. For a simple message buffer,
    // this usually means you want to overwrite from the beginning, or append.
    // Here, it allows appending, but `message_len` only updates to *offset.
    bytes_to_write = min((size_t)(MAX_BUFFER_SIZE - *offset), len);

    if (bytes_to_write <= 0) {
        mutex_unlock(&data->data_mutex);
        printk(KERN_WARNING "%s: Write: Buffer full or offset too large.\n", DEVICE_NAME);
        return -ENOSPC;
    }

    ret = copy_from_user(data->message_buffer + *offset, user_buffer, bytes_to_write);
    if (ret != 0) {
        printk(KERN_ERR "%s: Write: Failed to copy %d bytes from user space.\n", DEVICE_NAME, ret);
        mutex_unlock(&data->data_mutex);
        return -EFAULT;
    }

    *offset += bytes_to_write;
    // Issue 7: `message_len` only grows, it doesn't shrink if an overwrite happens.
    // E.g., write "hello", message_len = 5. Seek to 0, write "hi", message_len is still 5.
    // Reads will still see "hillo" not "hi".
    if (*offset > data->message_len) {
        data->message_len = *offset;
    }

    data->last_write_jiffies = jiffies; // Update last write time
    // Issue 8: Reschedule timer on every write. This is usually fine, but if writes are very frequent,
    // it could cause excessive timer modifications. Might be better to let it expire and reschedule only if needed.
    // Also, assumes timer is already initialized, which it is.
    mod_timer(&data->reset_timer, jiffies + MESSAGE_TIMEOUT_JIFFIES);

    mutex_unlock(&data->data_mutex);

    printk(KERN_INFO "%s: Written %zd bytes to device. Offset now %lld.\n", DEVICE_NAME, bytes_to_write, *offset);
    return bytes_to_write;
}

static loff_t subtle_dev_llseek(struct file *file, loff_t offset, int whence) {
    struct subtle_driver_data *data = (struct subtle_driver_data *)file->private_data;
    loff_t new_offset = 0;

    if (mutex_lock_interruptible(&data->data_mutex)) {
        printk(KERN_WARNING "%s: Lseek: Mutex lock interrupted.\n", DEVICE_NAME);
        return -ERESTARTSYS;
    }

    switch (whence) {
        case SEEK_SET:
            new_offset = offset;
            break;
        case SEEK_CUR:
            new_offset = file->f_pos + offset;
            break;
        case SEEK_END:
            // Issue 9: SEEK_END should typically be relative to the *end of the valid data*, not the buffer size.
            // If the buffer has 100 bytes of data in a 256-byte buffer, SEEK_END should be offset from 100, not 256.
            // This might allow seeking far into "empty" but allocated space.
            new_offset = MAX_BUFFER_SIZE + offset; // Should probably be data->message_len + offset
            break;
        default:
            mutex_unlock(&data->data_mutex);
            return -EINVAL;
    }

    // Issue 10: Validation of new_offset vs. buffer size.
    // If new_offset is 0 and BUFFER_SIZE is also 0, it becomes 0 - which is not allowed.
    // Also, if seeking exactly to MAX_BUFFER_SIZE, a write of 1 byte would be allowed.
    // The check `new_offset > MAX_BUFFER_SIZE` (instead of `>=`) might allow off-by-one errors.
    if (new_offset < 0 || new_offset > MAX_BUFFER_SIZE) {
        mutex_unlock(&data->data_mutex);
        printk(KERN_WARNING "%s: Lseek: Invalid offset %lld (requested: %lld, whence: %d).\n", DEVICE_NAME, new_offset, offset, whence);
        return -EINVAL;
    }

    file->f_pos = new_offset;
    mutex_unlock(&data->data_mutex);

    printk(KERN_INFO "%s: Seeked to offset %lld.\n", DEVICE_NAME, new_offset);
    return new_offset;
}

// File operations structure
static const struct file_operations subtle_fops = {
    .owner = THIS_MODULE,
    .open = subtle_dev_open,
    .release = subtle_dev_release,
    .read = subtle_dev_read,
    .write = subtle_dev_write,
    .llseek = subtle_dev_llseek,
};

// --- Module Initialization ---

static int __init subtle_driver_init(void) {
    int ret;
    struct device* subtle_device = NULL;

    printk(KERN_INFO "%s: Initializing Subtle Bad Driver module.\n", DEVICE_NAME);

    ret = alloc_chrdev_region(&subtle_dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ALERT "%s: Failed to allocate major/minor numbers: %d\n", DEVICE_NAME, ret);
        return ret;
    }
    printk(KERN_INFO "%s: Allocated device numbers Major: %d, Minor: %d\n", DEVICE_NAME, MAJOR(subtle_dev_num), MINOR(subtle_dev_num));

    subtle_driver_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(subtle_driver_class)) {
        ret = PTR_ERR(subtle_driver_class);
        printk(KERN_ALERT "%s: Failed to create device class: %d\n", DEVICE_NAME, ret);
        unregister_chrdev_region(subtle_dev_num, 1);
        return ret;
    }
    printk(KERN_INFO "%s: Device class created: /sys/class/%s\n", DEVICE_NAME, CLASS_NAME);

    cdev_init(&subtle_driver_cdev, &subtle_fops);
    subtle_driver_cdev.owner = THIS_MODULE;

    ret = cdev_add(&subtle_driver_cdev, subtle_dev_num, 1);
    if (ret < 0) {
        printk(KERN_ALERT "%s: Failed to add character device: %d\n", DEVICE_NAME, ret);
        class_destroy(subtle_driver_class);
        unregister_chrdev_region(subtle_dev_num, 1);
        return ret;
    }
    printk(KERN_INFO "%s: Character device added.\n", DEVICE_NAME);

    subtle_device = device_create(subtle_driver_class, NULL, subtle_dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(subtle_device)) {
        ret = PTR_ERR(subtle_device);
        printk(KERN_ALERT "%s: Failed to create device: %d\n", DEVICE_NAME, ret);
        cdev_del(&subtle_driver_cdev);
        class_destroy(subtle_driver_class);
        unregister_chrdev_region(subtle_dev_num, 1);
        return ret;
    }
    printk(KERN_INFO "%s: Device node created: /dev/%s\n", DEVICE_NAME, DEVICE_NAME);

    // Allocate and initialize driver-specific data
    g_subtle_data = kmalloc(sizeof(struct subtle_driver_data), GFP_KERNEL);
    if (!g_subtle_data) {
        ret = -ENOMEM;
        printk(KERN_ALERT "%s: Failed to allocate driver data structure.\n", DEVICE_NAME);
        device_destroy(subtle_driver_class, subtle_dev_num);
        cdev_del(&subtle_driver_cdev);
        class_destroy(subtle_driver_class);
        unregister_chrdev_region(subtle_dev_num, 1);
        return ret;
    }
    memset(g_subtle_data, 0, sizeof(struct subtle_driver_data));

    g_subtle_data->message_buffer = kmalloc(MAX_BUFFER_SIZE, GFP_KERNEL);
    if (!g_subtle_data->message_buffer) {
        ret = -ENOMEM;
        printk(KERN_ALERT "%s: Failed to allocate message buffer.\n", DEVICE_NAME);
        kfree(g_subtle_data);
        g_subtle_data = NULL; // Good practice to nullify after free
        device_destroy(subtle_driver_class, subtle_dev_num);
        cdev_del(&subtle_driver_cdev);
        class_destroy(subtle_driver_class);
        unregister_chrdev_region(subtle_dev_num, 1);
        return ret;
    }
    g_subtle_data->message_len = 0; // Initialize length to 0

    mutex_init(&g_subtle_data->data_mutex);

    // Initialize and arm the timer
    timer_setup(&g_subtle_data->reset_timer, subtle_timer_callback, 0); // Issue 11: Flag is 0, not TI_ONESHOT
    // Issue 12: Initializing last_write_jiffies to current jiffies, but no write has happened.
    // If the timer fires immediately, it might clear an empty buffer prematurely.
    g_subtle_data->last_write_jiffies = jiffies;
    mod_timer(&g_subtle_data->reset_timer, jiffies + MESSAGE_TIMEOUT_JIFFIES); // Start the timer

    printk(KERN_INFO "%s: Module loaded successfully.\n", DEVICE_NAME);
    return 0;
}

// --- Module Exit ---

static void __exit subtle_driver_exit(void) {
    printk(KERN_INFO "%s: Exiting Subtle Bad Driver module.\n", DEVICE_NAME);

    // Delete and cleanup timer
    // Issue 13: Timer might still be running and trying to acquire mutex during cleanup.
    // Timer should be del_timer_sync before freeing data that it accesses.
    // If timer handler is running, del_timer() won't wait for it. del_timer_sync() would.
    del_timer(&g_subtle_data->reset_timer); // This is not del_timer_sync()

    if (g_subtle_data) {
        // Issue 14: This kfree happens immediately. If the timer callback is still running
        // (even briefly after del_timer and before returning from its last execution),
        // it might try to access freed memory (`g_subtle_data->message_buffer` or `g_subtle_data->data_mutex`).
        if (g_subtle_data->message_buffer) {
            kfree(g_subtle_data->message_buffer);
            g_subtle_data->message_buffer = NULL;
        }
        kfree(g_subtle_data);
        g_subtle_data = NULL;
    }

    device_destroy(subtle_driver_class, subtle_dev_num);
    cdev_del(&subtle_driver_cdev);
    class_destroy(subtle_driver_class);
    unregister_chrdev_region(subtle_dev_num, 1);

    printk(KERN_INFO "%s: Module unloaded. Goodbye!\n", DEVICE_NAME);
}

// --- Module Entry/Exit Points ---
module_init(subtle_driver_init);
module_exit(subtle_driver_exit);