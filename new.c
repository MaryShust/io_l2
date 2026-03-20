#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/ctype.h>

#define MAX_RESULTS 1024

static dev_t first;
static struct cdev c_dev;
static struct class *cl;

// Structure to store results
struct result_entry {
    unsigned int count;
    struct result_entry *next;
};

static struct result_entry *results_head = NULL;
static struct result_entry *results_tail = NULL;
static int result_count = 0;
static DEFINE_MUTEX(results_mutex);

// Function to add a new result
static void add_result(unsigned int count)
{
    struct result_entry *new_entry;
    
    new_entry = kmalloc(sizeof(struct result_entry), GFP_KERNEL);
    if (!new_entry)
        return;
    
    new_entry->count = count;
    new_entry->next = NULL;
    
    mutex_lock(&results_mutex);
    
    if (!results_head) {
        results_head = new_entry;
        results_tail = new_entry;
    } else {
        results_tail->next = new_entry;
        results_tail = new_entry;
    }
    result_count++;
    
    mutex_unlock(&results_mutex);
}

// Function to free all results
static void free_results(void)
{
    struct result_entry *current, *next;
    
    mutex_lock(&results_mutex);
    
    current = results_head;
    while (current) {
        next = current->next;
        kfree(current);
        current = next;
    }
    results_head = NULL;
    results_tail = NULL;
    result_count = 0;
    
    mutex_unlock(&results_mutex);
}

static int my_open(struct inode *i, struct file *f)
{
    printk(KERN_INFO "Driver: open()\n");
    return 0;
}

static int my_close(struct inode *i, struct file *f)
{
    printk(KERN_INFO "Driver: close()\n");
    return 0;
}

static ssize_t my_read(struct file *f, char __user *buf, size_t len, loff_t *off)
{
    char *kernel_buf;
    int bytes_to_copy;
    int bytes_copied = 0;
    struct result_entry *current;
    int pos = 0;
    int i;
    
    // If we've already read everything, return 0
    if (*off >= result_count * (sizeof(unsigned int) + 1)) {
        return 0;
    }
    
    // Allocate kernel buffer
    kernel_buf = kmalloc(MAX_RESULTS * (sizeof(unsigned int) + 2), GFP_KERNEL);
    if (!kernel_buf)
        return -ENOMEM;
    
    mutex_lock(&results_mutex);
    
    // Format the output: each result on a new line
    current = results_head;
    i = 0;
    while (current && i < result_count) {
        pos += sprintf(kernel_buf + pos, "%u\n", current->count);
        current = current->next;
        i++;
    }
    
    mutex_unlock(&results_mutex);
    
    // Calculate bytes to copy
    if (*off >= pos) {
        kfree(kernel_buf);
        return 0;
    }
    
    bytes_to_copy = min(len, (size_t)(pos - *off));
    
    // Copy to user space
    if (copy_to_user(buf, kernel_buf + *off, bytes_to_copy)) {
        kfree(kernel_buf);
        return -EFAULT;
    }
    
    *off += bytes_to_copy;
    bytes_copied = bytes_to_copy;
    
    kfree(kernel_buf);
    
    printk(KERN_INFO "Driver: read() %d bytes\n", bytes_copied);
    return bytes_copied;
}

static ssize_t my_write(struct file *f, const char __user *buf, size_t len, loff_t *off)
{
    char *kernel_buf;
    int letter_count = 0;
    size_t i;
    
    // Allocate kernel buffer
    kernel_buf = kmalloc(len + 1, GFP_KERNEL);
    if (!kernel_buf)
        return -ENOMEM;
    
    // Copy data from user space
    if (copy_from_user(kernel_buf, buf, len)) {
        kfree(kernel_buf);
        return -EFAULT;
    }
    kernel_buf[len] = '\0';
    
    // Count letters (alphabetic characters)
    for (i = 0; i < len; i++) {
        if (isalpha(kernel_buf[i])) {
            letter_count++;
        }
    }
    
    // Store the result
    add_result(letter_count);
    
    printk(KERN_INFO "Driver: write() %zu bytes, %d letters\n", len, letter_count);
    
    kfree(kernel_buf);
    return len;
}

static struct file_operations mychdev_fops =
{
    .owner = THIS_MODULE,
    .open = my_open,
    .release = my_close,
    .read = my_read,
    .write = my_write
};

static int __init ch_drv_init(void)
{
    printk(KERN_INFO "Hello!\n");
    
    if (alloc_chrdev_region(&first, 0, 1, "ch_dev") < 0) {
        return -1;
    }
    
    if ((cl = class_create(THIS_MODULE, "chardrv")) == NULL) {
        unregister_chrdev_region(first, 1);
        return -1;
    }
    
    if (device_create(cl, NULL, first, NULL, "mychdev") == NULL) {
        class_destroy(cl);
        unregister_chrdev_region(first, 1);
        return -1;
    }
    
    cdev_init(&c_dev, &mychdev_fops);
    if (cdev_add(&c_dev, first, 1) == -1) {
        device_destroy(cl, first);
        class_destroy(cl);
        unregister_chrdev_region(first, 1);
        return -1;
    }
    
    // Initialize results list
    results_head = NULL;
    results_tail = NULL;
    result_count = 0;
    
    return 0;
}

static void __exit ch_drv_exit(void)
{
    free_results();  // Free all stored results
    cdev_del(&c_dev);
    device_destroy(cl, first);
    class_destroy(cl);
    unregister_chrdev_region(first, 1);
    printk(KERN_INFO "Bye!!!\n");
}

module_init(ch_drv_init);
module_exit(ch_drv_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Author");
MODULE_DESCRIPTION("Character device that counts letters in writes and returns sequence of results");
