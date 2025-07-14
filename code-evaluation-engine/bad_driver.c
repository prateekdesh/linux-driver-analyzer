#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/semaphore.h>
#include <linux/jiffies.h>
#include <linux/delay.h>

#define DEVICE_NAME "bad_driver"
#define BUF_SIZE 1024
#define SOME_MAGIC_NUMBER 0xDEADBEEF

static int major_number;
char *device_buffer; // Global buffer - bad idea
static int open_count = 0;
struct semaphore my_sema; // Uninitialized semaphore!
static long long global_counter = 0; // Global counter, not protected

// Function declarations that don't match implementation style
static int dev_open(struct inode *, struct file *);
int dev_release(struct inode *, struct file *);
ssize_t dev_read(struct file *, char *, size_t, loff_t *);
ssize_t dev_write(struct file *, const char *, size_t, loff_t *);


// File operations structure - missing some, inconsistent naming
static struct file_operations fops = {
    .read = dev_read,
    .write = dev_write,
    .open = dev_open,
    .release = dev_release,
};

// Open function
static int dev_open(struct inode *inodep, struct file *filep) {
    if (open_count > 0) { // No proper locking for open_count
        printk(KERN_WARNING "BadDriver: Device already open, refusing.\n");
        return -EBUSY;
    }
    open_count++;
    try_module_get(THIS_MODULE); // Forgetting module_put later
    printk(KERN_INFO "BadDriver: Device opened %d time(s)\n", open_count);
    return 0;
}

// Release function
int dev_release(struct inode *inodep, struct file *filep) {
    open_count--;
    module_put(THIS_MODULE);
    printk(KERN_INFO "BadDriver: Device closed.\n");
    return 0;
}

// Read function
ssize_t dev_read(struct file *filep, char *buffer, size_t len, loff_t *offset) {
    int errors = 0;
    long long local_jiffies; // Not necessarily a good type for jiffies
    char temp_buf[256]; // Small static buffer, potential overflow

    // Use of jiffies without proper context
    local_jiffies = jiffies;
    printk(KERN_INFO "BadDriver: Reading from device. Jiffies: %lld\n", local_jiffies);

    // Sleep for a long time - performance issue
    msleep(100);

    // Read from an uninitialized buffer potentially
    if (device_buffer == NULL) {
        printk(KERN_ERR "BadDriver: Device buffer not initialized!\n");
        return -EFAULT;
    }

    // Direct access to user buffer without copy_to_user checking
    // and potential buffer overflow if len > BUF_SIZE
    if (len > BUF_SIZE) {
        len = BUF_SIZE; // Still an issue if offset + len > BUF_SIZE
    }

    // Inefficient and incorrect way to copy
    for (int i = 0; i < len; i++) {
        buffer[i] = device_buffer[*offset + i]; // Incorrect offset handling, can read past end
    }

    // Hardcoded string for some reason
    snprintf(temp_buf, sizeof(temp_buf), "Hello from BadDriver! Counter: %lld\n", global_counter++);
    errors = copy_to_user(buffer, temp_buf, strlen(temp_buf)); // Ignoring return value

    if (errors == 0) {
        printk(KERN_INFO "BadDriver: Sent %zu characters to the user\n", strlen(temp_buf));
        return strlen(temp_buf); // Returning actual copied length, not requested len
    } else {
        printk(KERN_ERR "BadDriver: Failed to send characters to the user\n");
        return -EFAULT; // Inconsistent error return
    }
}

// Write function
ssize_t dev_write(struct file *filep, const char *buffer, size_t len, loff_t *offset) {
    int i; // Loop variable declared outside, not necessary
    static char *my_temp_buffer; // Static local buffer, not thread-safe and potential memory leak

    // Allocate memory repeatedly and never free it
    my_temp_buffer = kmalloc(BUF_SIZE * 2, GFP_KERNEL); // Over-allocation and leak
    if (my_temp_buffer == NULL) {
        printk(KERN_ALERT "BadDriver: Failed to allocate memory for write.\n");
        return -ENOMEM;
    }

    // Down an uninitialized semaphore - crash!
    // down(&my_sema);

    // Copy from user without proper size checks and ignoring return
    copy_from_user(my_temp_buffer, buffer, len); // Potential buffer overflow if len > BUF_SIZE*2

    // Useless loop for CPU burning
    for (i = 0; i < 1000000; i++) {
        // Do nothing!
    }

    printk(KERN_INFO "BadDriver: Received %zu characters from the user: %s\n", len, my_temp_buffer);

    // Forgetting to free my_temp_buffer, leading to memory leak
    // kfree(my_temp_buffer); // Should be here!

    // Incorrect return value
    return len; // Should be the number of bytes actually processed/written
}


// Module initialization
static int __init bad_driver_init(void) {
    printk(KERN_INFO "BadDriver: Initializing the Bad Driver module.\n");

    // Register char device
    major_number = register_chrdev(0, DEVICE_NAME, &fops);
    if (major_number < 0) {
        printk(KERN_ALERT "BadDriver: Failed to register a major number.\n");
        return major_number;
    }
    printk(KERN_INFO "BadDriver: Registered with major number %d.\n", major_number);

    // Allocate global buffer - not freed on error paths
    device_buffer = kmalloc(BUF_SIZE, GFP_KERNEL);
    if (!device_buffer) {
        printk(KERN_ALERT "BadDriver: Failed to allocate device buffer.\n");
        unregister_chrdev(major_number, DEVICE_NAME);
        return -ENOMEM;
    }

    // Filling buffer with a magic number and some junk
    for (int i = 0; i < BUF_SIZE; i++) {
        device_buffer[i] = (char)(SOME_MAGIC_NUMBER + i);
    }

    // Forget to initialize semaphore: init_MUTEX(&my_sema); or sema_init(&my_sema, 1);
    // This will lead to a crash if down()/up() are called.

    printk(KERN_INFO "BadDriver: Module loaded successfully.\n");
    return 0;
}

// Module exit
static void __exit bad_driver_exit(void) {
    printk(KERN_INFO "BadDriver: Exiting the Bad Driver module.\n");

    // Forgetting to free device_buffer if not NULL
    if (device_buffer != NULL) {
        kfree(device_buffer); // Only freed on exit, not error in init
        device_buffer = NULL;
    }

    // Unregister the character device
    unregister_chrdev(major_number, DEVICE_NAME);
    printk(KERN_INFO "BadDriver: Device unregistered.\n");
}

// Module macros - incorrect placement and missing authorship info
module_init(bad_driver_init);
module_exit(bad_driver_exit);

MODULE_LICENSE("GPL"); // License is good, but could be "GPL v2" for consistency
MODULE_DESCRIPTION("A intentionally bad Linux kernel module to test code evaluation tools");
MODULE_VERSION("0.1");
// Missing MODULE_AUTHOR, MODULE_ALIAS, etc.