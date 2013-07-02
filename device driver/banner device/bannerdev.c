/*
**  bannerdev.c: Kernel module that acts as a driver for a new device that holds a banner
**  string of no mare than 31 characters. The string is preserved after a writer closes the
**  device so that it can be retreived later.
**      reading: return the banner string
**      writing: set the banner string characters
*/

#include <asm/uaccess.h>	// for put_user and get_user
#include <linux/fs.h>		// file structures
#include <linux/init.h>		// for macros
#include <linux/kernel.h> 	// for KERN_INFO
#include <linux/module.h> 	// required for modules
#include <linux/moduleparam.h>
#include <linux/stat.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ben Heng <benheng@bu.edu>");
MODULE_DESCRIPTION("A driver written for a homework assignment");

// PROTOTYPES - normally in a header file =====================================================
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);

#define SUCCESS 0
#define DEVICE_NAME "bannerdev"	// Dev name as it appears in /proc/devices
#define BUFF_LEN 32				// Max length of the message from device

// GLOBAL VARIABLES ===========================================================================
static int Major;				// Major number assigned to our device driver
static int Device_Open = 0;		// is the device open? prevents multiple access to devices
static char msg[BUFF_LEN];		// output message of device
static char *msg_ptr;

static struct file_operations fops = {
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.release = device_release
};

// :NOTE: using $ insmod bannerdev.ko banner_str="new_string"
//        new_string cannot contain spaces
//static char *banner_str = "default banner string";

// module_param(name, data_type, permissions);
//module_param(banner_str, charp, 0000);
//MODULE_PARM_DESC(banner_str, "The banner string");

// INIT_MODULE ================================================================================
static int __init bannerdev_init(void)
{
	// try to register the device
	Major = register_chrdev(0, DEVICE_NAME, &fops);
	if (Major < 0) {
		printk(KERN_ALERT "Registering banner device failed with %d\n", Major);
		return Major;
	}

	printk(KERN_INFO "assigned major #: %d\n", Major);
	printk(KERN_INFO "To talk to the driver create a device file with\n");
	printk(KERN_INFO "    'mknod /dev/%s c %d 0'.\n", DEVICE_NAME, Major);
	printk(KERN_INFO "Try various minor numbers other than '0'.\n");
	printk(KERN_INFO "You can undo mknod with 'unlink'.\n");

	sprintf(msg, "WORK DAMNIT\n");	// default banner message

	return SUCCESS;
}
module_init(bannerdev_init);

// CLEANUP_MODULE =============================================================================
static void __exit bannerdev_exit(void)
{
	// unregister the device
	unregister_chrdev(Major, DEVICE_NAME);
	printk(KERN_INFO "Goodbye world!\n");
}
module_exit(bannerdev_exit);

// METHODS ====================================================================================
// called when a process tries to open the device file: cat /dev/mycharfile
static int device_open(struct inode *inode, struct file *file)
{
	if (Device_Open)
		return -EBUSY;

	Device_Open++;
	msg_ptr = msg;	// init the message
	try_module_get(THIS_MODULE);
	return SUCCESS;
}

// called when a process closes the device file.
static int device_release(struct inode *inode, struct file *file)
{
	Device_Open--;
	module_put(THIS_MODULE);
	return SUCCESS;
}

// called when a process, which already opened the dev file, attemps to read from it
static ssize_t device_read(struct file *filp,	// see  include/linux/fs.h
                           char *buffer,		// buffer to fill with data
                           size_t length,		// length of the buffer
                           loff_t *offset)
{
	// number of bytes actually written to the buffer
	int bytes_read = 0;

	// return 0 to signify end of file
	if (*msg_ptr == 0) return 0;

	// actually put the data into the buffer
	while (length && *msg_ptr) {
		// the buffer is in the user data segment, not the kernel segment so "*" assignment
		// won't work. we have to use put_user which copies data from the kernel data segment
		// to the user data segment.
		put_user(*(msg_ptr++),buffer++);
		length--;
		bytes_read++;
	}

	// most read functions return the number of bytes but into the buffer
	return bytes_read;
}

// called when a process writes to dev file: echo "hi" > /dev/hello
static ssize_t device_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{
	int i;

//	for (i = 0; i < BUFF_LEN; i++)
//		msg[i] = '\0';

	// actually write to the buffer
	for (i = 0; i < len && i < (BUFF_LEN - 1); i++)
		get_user(*(msg_ptr++), buff + i);
	*(msg_ptr) = '\0';
	msg_ptr = msg;

	// return the number of input characters used
	return i;
}
