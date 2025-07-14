#include <linux/module.h>     // Required for all kernel modules
#include <linux/kernel.h>     // KERN_INFO, printk
#include <linux/fs.h>         // File operations, register_chrdev
#include <linux/uaccess.h>    // copy_to_user, copy_from_user
#include <linux/slab.h>       // kmalloc, kfree
#include <linux/cdev.h>       // cdev_init, cdev_add
#include <linux/device.h>     // class_create, device_create
#include <linux/mutex.h>      // Mutex for synchronization

// --- Defines ---
#define DEVICE_NAME "good_driver"
#define CLASS_NAME  "good_class"
#define BUFFER_SIZE 4096 // A reasonable buffer size for a simple device

// --- Module Information ---
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name/Organization");
MODULE_DESCRIPTION("A well-written Linux kernel character device driver.");
MODULE_VERSION("1.0");

// --- Global Variables (Protected) ---
static dev_t major_minor_dev_num; // Stores major and minor numbers
static struct class* good_driver_class = NULL; // Device class
static struct cdev good_driver_cdev; // Character device structure

// Device-specific data structure
struct good_driver_data {
    char *buffer;              // Device buffer
    size_t current_len;        // Current data length in buffer
    struct mutex buffer_mutex; // Mutex for protecting buffer access
};

static struct good_driver_data *g_driver_data = NULL; // Pointer to driver's global data

// --- File Operations ---

/**
 * @brief Handles device open requests.
 * @param inode Pointer to the inode structure.
 * @param file Pointer to the file structure.
 * @return 0 on success, or a negative error code on failure.
 */
static int good_dev_open(struct inode *inode, struct file *file) {
    // Associate our private data with the file pointer for later use.
    // This is good practice for managing per-device or per-open data.
    file->private_data = g_driver_data;

    // We don't need a counter for open instances if we only allow one,
    // or if the mutex handles concurrent access effectively for the buffer.
    // For simplicity, this driver implicitly allows multiple opens but
    // relies on the buffer_mutex for data integrity.

    printk(KERN_INFO "%s: Device opened.\n", DEVICE_NAME);
    return 0;
}

// -----------------------------------------------------------------------------

/**
 * @brief Handles device close requests.
 * @param inode Pointer to the inode structure.
 * @param file Pointer to the file structure.
 * @return 0 on success.
 */
static int good_dev_release(struct inode *inode, struct file *file) {
    printk(KERN_INFO "%s: Device closed.\n", DEVICE_NAME);
    return 0;
}

// -----------------------------------------------------------------------------

/**
 * @brief Handles device read requests.
 * @param file Pointer to the file structure.
 * @param user_buffer Pointer to the user-space buffer.
 * @param len Maximum number of bytes to read.
 * @param offset Pointer to the current file offset.
 * @return Number of bytes read on success, or a negative error code on failure.
 */
static ssize_t good_dev_read(struct file *file, char __user *user_buffer, size_t len, loff_t *offset) {
    struct good_driver_data *data = (struct good_driver_data *)file->private_data;
    ssize_t bytes_to_read;
    int ret;

    // Acquire mutex to protect buffer during read
    if (mutex_lock_interruptible(&data->buffer_mutex)) {
        printk(KERN_WARNING "%s: Read: Mutex lock interrupted.\n", DEVICE_NAME);
        return -ERESTARTSYS; // Signal that syscall should be restarted
    }

    // Determine how many bytes to copy
    // Ensure we don't read past the end of the data in our buffer
    bytes_to_read = min((size_t)(data->current_len - *offset), len);

    if (bytes_to_read <= 0) {
        mutex_unlock(&data->buffer_mutex);
        return 0; // No more data to read or offset is past end
    }

    // Copy data from kernel space to user space
    ret = copy_to_user(user_buffer, data->buffer + *offset, bytes_to_read);
    if (ret != 0) {
        // If ret is non-zero, it indicates the number of bytes that could NOT be copied.
        printk(KERN_ERR "%s: Read: Failed to copy %d bytes to user space.\n", DEVICE_NAME, ret);
        mutex_unlock(&data->buffer_mutex);
        return -EFAULT; // Bad address
    }

    *offset += bytes_to_read; // Update the file offset
    mutex_unlock(&data->buffer_mutex); // Release mutex

    printk(KERN_INFO "%s: Read %zd bytes from device. Offset now %lld.\n", DEVICE_NAME, bytes_to_read, *offset);
    return bytes_to_read;
}

// -----------------------------------------------------------------------------

/**
 * @brief Handles device write requests.
 * @param file Pointer to the file structure.
 * @param user_buffer Pointer to the user-space buffer.
 * @param len Number of bytes to write.
 * @param offset Pointer to the current file offset.
 * @return Number of bytes written on success, or a negative error code on failure.
 */
static ssize_t good_dev_write(struct file *file, const char __user *user_buffer, size_t len, loff_t *offset) {
    struct good_driver_data *data = (struct good_driver_data *)file->private_data;
    ssize_t bytes_to_write;
    int ret;

    // Acquire mutex to protect buffer during write
    if (mutex_lock_interruptible(&data->buffer_mutex)) {
        printk(KERN_WARNING "%s: Write: Mutex lock interrupted.\n", DEVICE_NAME);
        return -ERESTARTSYS; // Signal that syscall should be restarted
    }

    // Determine how many bytes to copy
    // Ensure we don't write past the end of our buffer
    bytes_to_write = min((size_t)(BUFFER_SIZE - *offset), len);

    if (bytes_to_write <= 0) {
        mutex_unlock(&data->buffer_mutex);
        printk(KERN_WARNING "%s: Write: Buffer full or offset too large. No bytes written.\n", DEVICE_NAME);
        return -ENOSPC; // No space left on device
    }

    // Copy data from user space to kernel space
    ret = copy_from_user(data->buffer + *offset, user_buffer, bytes_to_write);
    if (ret != 0) {
        printk(KERN_ERR "%s: Write: Failed to copy %d bytes from user space.\n", DEVICE_NAME, ret);
        mutex_unlock(&data->buffer_mutex);
        return -EFAULT; // Bad address
    }

    *offset += bytes_to_write; // Update the file offset
    data->current_len = *offset; // Update the current length of data in the buffer

    mutex_unlock(&data->buffer_mutex); // Release mutex

    printk(KERN_INFO "%s: Written %zd bytes to device. Offset now %lld.\n", DEVICE_NAME, bytes_to_write, *offset);
    return bytes_to_write;
}

// -----------------------------------------------------------------------------

/**
 * @brief Handles seek requests for the device.
 * @param file Pointer to the file structure.
 * @param offset New offset value.
 * @param whence Origin for the seek operation (SEEK_SET, SEEK_CUR, SEEK_END).
 * @return New file offset on success, or a negative error code on failure.
 */
static loff_t good_dev_llseek(struct file *file, loff_t offset, int whence) {
    struct good_driver_data *data = (struct good_driver_data *)file->private_data;
    loff_t new_offset = 0;

    // Acquire mutex to protect current_len during seek operation
    if (mutex_lock_interruptible(&data->buffer_mutex)) {
        printk(KERN_WARNING "%s: Lseek: Mutex lock interrupted.\n", DEVICE_NAME);
        return -ERESTARTSYS;
    }

    switch (whence) {
        case SEEK_SET: // Relative to the beginning of the file
            new_offset = offset;
            break;
        case SEEK_CUR: // Relative to the current file position
            new_offset = *(&file->f_pos) + offset;
            break;
        case SEEK_END: // Relative to the end of the file (current data length)
            new_offset = data->current_len + offset;
            break;
        default:
            mutex_unlock(&data->buffer_mutex);
            return -EINVAL; // Invalid argument
    }

    // Validate the new offset
    if (new_offset < 0 || new_offset > BUFFER_SIZE) {
        mutex_unlock(&data->buffer_mutex);
        printk(KERN_WARNING "%s: Lseek: Invalid offset %lld.\n", DEVICE_NAME, new_offset);
        return -EINVAL;
    }

    *(&file->f_pos) = new_offset; // Update file position
    mutex_unlock(&data->buffer_mutex);

    printk(KERN_INFO "%s: Seeked to offset %lld.\n", DEVICE_NAME, new_offset);
    return new_offset;
}

// -----------------------------------------------------------------------------

// File operations structure
static const struct file_operations good_fops = {
    .owner = THIS_MODULE,
    .open = good_dev_open,
    .release = good_dev_release,
    .read = good_dev_read,
    .write = good_dev_write,
    .llseek = good_dev_llseek, // Implement seek for better device behavior
};

// --- Module Initialization ---

/**
 * @brief Initializes the driver module.
 * @return 0 on success, or a negative error code on failure.
 */
static int __init good_driver_init(void) {
    int ret;
    struct device* good_device = NULL;

    printk(KERN_INFO "%s: Initializing Good Driver module.\n", DEVICE_NAME);

    // 1. Allocate a major and minor number dynamically
    ret = alloc_chrdev_region(&major_minor_dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ALERT "%s: Failed to allocate major/minor numbers: %d\n", DEVICE_NAME, ret);
        return ret;
    }
    printk(KERN_INFO "%s: Allocated device numbers Major: %d, Minor: %d\n", DEVICE_NAME, MAJOR(major_minor_dev_num), MINOR(major_minor_dev_num));

    // 2. Create a device class (appears in /sys/class)
    good_driver_class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(good_driver_class)) {
        ret = PTR_ERR(good_driver_class);
        printk(KERN_ALERT "%s: Failed to create device class: %d\n", DEVICE_NAME, ret);
        unregister_chrdev_region(major_minor_dev_num, 1);
        return ret;
    }
    printk(KERN_INFO "%s: Device class created: /sys/class/%s\n", DEVICE_NAME, CLASS_NAME);

    // 3. Initialize the character device structure
    cdev_init(&good_driver_cdev, &good_fops);
    good_driver_cdev.owner = THIS_MODULE;

    // 4. Add the character device to the kernel
    ret = cdev_add(&good_driver_cdev, major_minor_dev_num, 1);
    if (ret < 0) {
        printk(KERN_ALERT "%s: Failed to add character device: %d\n", DEVICE_NAME, ret);
        class_destroy(good_driver_class); // Clean up class
        unregister_chrdev_region(major_minor_dev_num, 1);
        return ret;
    }
    printk(KERN_INFO "%s: Character device added.\n", DEVICE_NAME);

    // 5. Create the device file in /dev/ (udev will pick this up)
    good_device = device_create(good_driver_class, NULL, major_minor_dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(good_device)) {
        ret = PTR_ERR(good_device);
        printk(KERN_ALERT "%s: Failed to create device: %d\n", DEVICE_NAME, ret);
        cdev_del(&good_driver_cdev); // Clean up cdev
        class_destroy(good_driver_class); // Clean up class
        unregister_chrdev_region(major_minor_dev_num, 1);
        return ret;
    }
    printk(KERN_INFO "%s: Device node created: /dev/%s\n", DEVICE_NAME, DEVICE_NAME);

    // 6. Allocate and initialize driver-specific data
    g_driver_data = kmalloc(sizeof(struct good_driver_data), GFP_KERNEL);
    if (!g_driver_data) {
        ret = -ENOMEM;
        printk(KERN_ALERT "%s: Failed to allocate driver data structure.\n", DEVICE_NAME);
        device_destroy(good_driver_class, major_minor_dev_num);
        cdev_del(&good_driver_cdev);
        class_destroy(good_driver_class);
        unregister_chrdev_region(major_minor_dev_num, 1);
        return ret;
    }
    memset(g_driver_data, 0, sizeof(struct good_driver_data)); // Zero out the structure

    g_driver_data->buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
    if (!g_driver_data->buffer) {
        ret = -ENOMEM;
        printk(KERN_ALERT "%s: Failed to allocate device buffer.\n", DEVICE_NAME);
        kfree(g_driver_data); // Free the data structure itself
        device_destroy(good_driver_class, major_minor_dev_num);
        cdev_del(&good_driver_cdev);
        class_destroy(good_driver_class);
        unregister_chrdev_region(major_minor_dev_num, 1);
        return ret;
    }
    g_driver_data->current_len = 0; // Buffer is initially empty

    // Initialize the mutex
    mutex_init(&g_driver_data->buffer_mutex);
    printk(KERN_INFO "%s: Mutex initialized.\n", DEVICE_NAME);

    printk(KERN_INFO "%s: Module loaded successfully! 🎉\n", DEVICE_NAME);
    return 0;
}

// --- Module Exit ---

/**
 * @brief Cleans up and unloads the driver module.
 */
static void __exit good_driver_exit(void) {
    printk(KERN_INFO "%s: Exiting Good Driver module.\n", DEVICE_NAME);

    // Clean up in reverse order of allocation/creation

    // 1. Free driver data and buffer
    if (g_driver_data) {
        if (g_driver_data->buffer) {
            kfree(g_driver_data->buffer);
            g_driver_data->buffer = NULL;
            printk(KERN_INFO "%s: Device buffer freed.\n", DEVICE_NAME);
        }
        kfree(g_driver_data);
        g_driver_data = NULL;
        printk(KERN_INFO "%s: Driver data structure freed.\n", DEVICE_NAME);
    }

    // 2. Destroy the device file
    device_destroy(good_driver_class, major_minor_dev_num);
    printk(KERN_INFO "%s: Device node removed.\n", DEVICE_NAME);

    // 3. Delete the character device
    cdev_del(&good_driver_cdev);
    printk(KERN_INFO "%s: Character device deleted.\n", DEVICE_NAME);

    // 4. Destroy the device class
    class_destroy(good_driver_class);
    printk(KERN_INFO "%s: Device class destroyed.\n", DEVICE_NAME);

    // 5. Unregister major/minor numbers
    unregister_chrdev_region(major_minor_dev_num, 1);
    printk(KERN_INFO "%s: Device numbers unregistered.\n", DEVICE_NAME);

    printk(KERN_INFO "%s: Module unloaded. Goodbye! 👋\n", DEVICE_NAME);
}

// --- Module Entry/Exit Points ---
module_init(good_driver_init);
module_exit(good_driver_exit);