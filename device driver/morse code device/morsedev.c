/*
**  morsedev.c: Device driver for a morse code trasmitter device. The device uses a GPIO pin
**	whose high and low signals represent the morse code transmission. Writing a character to
**	the device stores sends that character in the device buffer. Reading from the device
**  prints from the device buffer to user space and produces a morse code representation on
**  the selected GPIO pin.
**
**	compile module with 	$ make
**	insert module with		$ insmod morsedev.ko
**	check log messages		$ cat /var/log/messages.log
**	(major#) written to log file
**	make node with 			$ mknod /dev/morsedev c (major#) 0
**
*/

// INCLUDES ===================================================================================
#include <asm/uaccess.h>	// put_user, get_user
#include <linux/errno.h>	// error codes
#include <linux/fs.h>		// file structures
#include <linux/init.h>		// macros
#include <linux/kernel.h>	// KERN_INFO and printk()
#include <linux/module.h>	// required for modules
#include <linux/moduleparam.h>
#include <linux/slab.h>		// kmalloc()
#include <linux/stat.h>

// TIMER USE - specifically for this device file ==============================================
#include <asm/gpio.h>
//#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/timer.h>

// MACROS & DEFINES ===========================================================================
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ben Heng <benheng@bu.edu>");
MODULE_DESCRIPTION("A driver written for a homework assignment");

#define DEVICE_NAME "morsedev"	// Dev name as it appears in /proc/devices
#define CAP1X 32
#define SUCCESS 0
#define GPIO 25
#define MAX_UNIT 600
#define TDELAY 20

// PROTOTYPES - normally in a header file =====================================================
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);
static int __init morsedev_init(void);
module_init(morsedev_init);
static void __exit morsedev_exit(void);
module_exit(morsedev_exit);

// GLOBAL VARIABLES ===========================================================================
static int Major;				// Major number assigned to our device driver
static int Device_Open = 0;		// is the device open? prevents multiple access to devices
static char *dbuff;				// device buffer
static char *dbuff_ptr;			// device buffer pointer
static unsigned capacity = CAP1X;
module_param(capacity, uint, S_IRUGO);
static char pulse[MAX_UNIT];	// each byte is one time unit
static char *pulse_ptr;			// pointers are faster than [] referencing
static int pulse_len = 0;		// number of time units
static int pulse_pos;			// used to countdown timer position
static struct timer_list timer;	// so I don't have to manually allocate memory
static int tmrbusy = 0;			// prevents writing the pulse array while it's still pulsing

static struct file_operations fops = {
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.release = device_release
};

void expire(unsigned long arg)
{
	unsigned long delay;
	if (pulse_pos-- > 0) {	// morse code sequence
		// operate LED
		if (*(pulse_ptr++) == '1') { gpio_set_value(GPIO, 1); }
		else { gpio_set_value(GPIO, 0); }

		// mod the timer if there is still work to be done
		delay = jiffies + TDELAY;
		mod_timer(&timer, delay);

		// done
		if (pulse_pos == 0) {
			tmrbusy = 0;
			del_timer(&timer);
		}
	}
}
// INIT_MODULE ================================================================================
static int __init morsedev_init(void)
{
	// Try to register the device
	Major = register_chrdev(0, DEVICE_NAME, &fops);
	if (Major < 0) {
		printk(KERN_INFO "Registering banner device failed with %d\n", Major);
		return Major;
	}

	// Allocate morsedev for the buffer
	dbuff = kmalloc(CAP1X, GFP_KERNEL);
	if (!dbuff) {
		printk(KERN_INFO "Insufficient kernel memory (%d bytes required)\n", capacity);
		return -ENOMEM;
	}
	memset(dbuff, 0, capacity);

	// Initialize GPIO
	if (gpio_request(GPIO, "Morse LED") < 0) {
		printk(KERN_INFO "GPIO %d request failed\n", GPIO);
		return -1;
	} else { printk(KERN_INFO "GPIO %d requested\n", GPIO); }
	gpio_direction_output(GPIO, 1);	// make sure it turns on

	// Initialize timer
	init_timer(&timer);

	// Print to log file for debugging
	printk(KERN_INFO "Inserted %s module.\n", DEVICE_NAME);
	printk(KERN_INFO "Assigned major #: %d\n", Major);
	printk(KERN_INFO "To talk to the driver create a device file with:\n");
	printk(KERN_INFO "    'mknod /dev/%s c %d 0'.\n", DEVICE_NAME, Major);
	printk(KERN_INFO "Try various minor numbers other than '0'.\n");
	printk(KERN_INFO "You can undo mknod with 'unlink'.\n");

	return SUCCESS;
}

// CLEANUP_MODULE =============================================================================
static void __exit morsedev_exit(void)
{
	// unregister the device and free memory if necessary
	unregister_chrdev(Major, DEVICE_NAME);
	if (dbuff) { kfree(dbuff); }
	gpio_set_value(GPIO, 0);
	gpio_free(GPIO);
	if (tmrbusy) del_timer(&timer); // if the timer isn't busy, it has already been deleted
	printk(KERN_INFO "Removed %s\n", DEVICE_NAME);
}

// METHODS ====================================================================================
// called when a process tries to open the device file: cat /dev/mycharfile
static int device_open(struct inode *inode, struct file *file)
{
	if (Device_Open) {
		printk(KERN_INFO "Open cancelled: %s is busy\n", DEVICE_NAME);
		return -EBUSY;
	}
	Device_Open++;
	gpio_set_value(GPIO, 0);	// reset (turn off)
	if (!tmrbusy) pulse_ptr = pulse;	// reset pulse pointer to beginning of pulse array
	dbuff_ptr = dbuff;			// reset device buffer pointer to beginning of device buffer
	printk(KERN_INFO "Open called: pid: %d, com: %s\n", current->pid, current->comm);
	return SUCCESS;
}

// called when a process closes the device file.
static int device_release(struct inode *inode, struct file *file)
{
	Device_Open--;
	gpio_set_value(GPIO, 0);	// makes sure the GPIO is off when complete
	printk(KERN_INFO "Release called: pid: %d, com: %s\n", current->pid, current->comm);
	return SUCCESS;
}

// called when a process, which already opened the dev file, attemps to read from it
static ssize_t device_read(struct file *filp,	// see include/linux/fs.h
                           char *buff,			// user space buffer
                           size_t len,			// length of the buffer
                           loff_t *off)			// offset (not used)
{
	int bytes_read = 0;

	// check if the device buffer is empty
	if (*dbuff_ptr == 0) {
		printk(KERN_INFO "Read called: buffer empty\n");
		return 0;
	}
	// copy device buffer to user space buffer
	while (*dbuff_ptr) {
		// this module is run in kernel space, but it must copy to user space so "*" assignment
		// won't work. we have to use put_user to copy data from kernel space to user space.
		put_user(*(dbuff_ptr++), buff++);
		bytes_read++;
	}
	printk(KERN_INFO "Read called: pid: %d, com: %s, len: %d, str: %s\n",
		current->pid, current->comm, len, dbuff);

	// activate morse code sequence if timer is not busy
	if (!tmrbusy) {
		// start first pulse
		tmrbusy = 1;	// the timer is now active
		pulse_pos = pulse_len;
		timer.expires = jiffies + TDELAY;
		timer.function = expire;
		add_timer(&timer);
	}
	else printk(KERN_INFO "LED is currently active. Wait for it to finish.\n");

	// most read functions return the number of bytes put into the buffer
	return bytes_read;
}

// called when a process writes to dev file: echo "hi" > /dev/hello
static ssize_t device_write(struct file *filp, const char *buff, size_t len, loff_t *off)
{
	int i;
	// don't write a new file if the timer is busy
	if (tmrbusy) {
		printk(KERN_INFO "LED is active, cannot write to device.\n");
		return 0;
	}

	// copy user space buffer to device buffer
	// also: assign morse code values to the pulse buffer
	pulse_len = 0;	// reset pulse length
	for (i = 0; i < len && i < (CAP1X - 1); i++) {
		get_user(*(dbuff_ptr), buff++);
		switch (*(dbuff_ptr++)) {
			case 'a': goto caseA;
			case 'b': goto caseB;
			case 'c': goto caseC;
			case 'd': goto caseD;
			case 'e': goto caseE;
			case 'f': goto caseF;
			case 'g': goto caseG;
			case 'h': goto caseH;
			case 'i': goto caseI;
			case 'j': goto caseJ;
			case 'k': goto caseK;
			case 'l': goto caseL;
			case 'm': goto caseM;
			case 'n': goto caseN;
			case 'o': goto caseO;
			case 'p': goto caseP;
			case 'q': goto caseQ;
			case 'r': goto caseR;
			case 's': goto caseS;
			case 't': goto caseT;
			case 'u': goto caseU;
			case 'v': goto caseV;
			case 'w': goto caseW;
			case 'x': goto caseX;
			case 'y': goto caseY;
			case 'z': goto caseZ;
			case 'A': caseA:			// .-
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 8;
				break;
			case 'B': caseB:			// -...
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 12;
				break;
			case 'C': caseC:			// -.-.
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 14;
				break;
			case 'D': caseD:			// -..
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 10;
				break;
			case 'E': caseE:			// .
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 4;
				break;
			case 'F': caseF:			// ..-.
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 12;
				break;
			case 'G': caseG:			// --.
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 12;
				break;
			case 'H': caseH:			// ....
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 10;
				break;
			case 'I': caseI:			// ..
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 6;
				break;
			case 'J': caseJ:			// .---
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 16;
				break;
			case 'K': caseK:			// -.-
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 12;
				break;
			case 'L': caseL:			// .-..
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 12;
				break;
			case 'M': caseM:			// --
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 10;
				break;
			case 'N': caseN:			// -.
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 8;
				break;
			case 'O': caseO:			// ---
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 14;
				break;
			case 'P': caseP:			// .--.
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 14;
				break;
			case 'Q': caseQ:			// --.-
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 16;
				break;
			case 'R': caseR:			// .-.
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 10;
				break;
			case 'S': caseS:			// ...
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 8;
				break;
			case 'T': caseT:			// -
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 6;
				break;
			case 'U': caseU:			// ..-
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 10;
				break;
			case 'V': caseV:			// ...-
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 12;
				break;
			case 'W': caseW:			// .--
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 12;
				break;
			case 'X': caseX:			// -..-
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 14;
				break;
			case 'Y': caseY:			// -.--
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 16;
				break;
			case 'Z': caseZ:			// --..
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 14;
				break;
			case '0':					// -----
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 22;
				break;
			case '1':					// .----
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 20;
				break;
			case '2':					// ..---
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 18;
				break;
			case '3':					// ...--
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 16;
				break;
			case '4':					// ....-
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 14;
				break;
			case '5':					// .....
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 12;
				break;
			case '6':					// -....
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 14;
				break;
			case '7':					// --...
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 16;
				break;
			case '8':					// ---..
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 18;
				break;
			case '9':					// ----.
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '1';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 20;
				break;
			default:					// (space)
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				*(pulse_ptr++) = '0';
				pulse_len += 5;
				break;
		} // switch (*(dbuff_ptr++))
	} // for (i = 0; i < len && i < (CAP1X - 1); i++)

	// append string terminator '\0'
	*(dbuff_ptr) = '\0';
	printk(KERN_INFO "Write called: pid: %d, com: %s, len: %d, str: %s, plen: %d\n",
		current->pid, current->comm, len, dbuff, pulse_len);

	// return the number of bytes written to the device buffer
	return i;
}