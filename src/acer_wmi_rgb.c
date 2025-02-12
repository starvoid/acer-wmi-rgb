//#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <acpi/video.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/dmi.h>
#include <linux/fb.h>
#include <linux/backlight.h>
#include <linux/leds.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/i8042.h>
#include <linux/rfkill.h>
#include <linux/workqueue.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/cdev.h>
#include <linux/input/sparse-keymap.h>
#include <linux/version.h>

MODULE_VERSION("1.0");
MODULE_AUTHOR("Starvoid");
MODULE_DESCRIPTION("Acer Laptop WMI RGB Keyboard Driver");
MODULE_LICENSE("GPL");
MODULE_SOFTDEP("pre: acer-wmi");

// Init parameter
static char *init_conf = NULL;
MODULE_PARM_DESC(init_conf, "Init configuration string"); 
module_param(init_conf, charp, 0000); 


/*
 * Magic Number
 * Meaning is unknown - this number is required for writing to ACPI for AMW0
 * (it's also used in acerhk when directly accessing the BIOS)
 */
#define ACER_AMW0_WRITE	0x9610

/*
 * Acer ACPI WMI method GUIDs
 */
#define WMID_GUID3		"61EF69EA-865C-4BC3-A502-A0DEBA0CB531"
#define WMID_GUID4		"7A4DDFE7-5B5D-40B4-8595-4408E0CC7F56"

/*
 * Method IDs for WMID interface
 */
#define ACER_WMID_SET_GAMING_LED_METHODID 2
#define ACER_WMID_GET_GAMING_LED_METHODID 4
#define ACER_WMID_GET_GAMING_SYSINFO_METHODID 5
#define ACER_WMID_SET_GAMING_STATIC_LED_METHODID 6

/*
 * Method IDs for WMID interface
 */
#define ACER_WMID_SET_GAMINGKBBL_METHODID		20
#define ACER_WMID_GET_GAMINGKBBL_METHODID		21

/*
The name of the device in /dev
*/
#define GAMING_KB_RGB_DEV_NAME "acer-kb-rgb"

#define DEFAULT_SPEED 2 // 1 <--> 9
#define DEFAULT_BRIGHTNESS 90 // 0 <--> 100
#define DEFAULT_DIRECTION 1 // 1 -->, 2 <--
#define DEFAULT_COLOR 64,128,255 // RGB

/*
 * RTLNX_VER_MIN
 * Evaluates to true if the linux kernel version is equal or higher to the
 * one specfied.
 */
#define RTLNX_VER_MIN(a_Major, a_Minor, a_Patch) \
    (LINUX_VERSION_CODE >= KERNEL_VERSION(a_Major, a_Minor, a_Patch))

// WMI version and capacity
#define ACER_WMID_GAMING 4

// User space device Device globals
static dev_t kbrgb_dev = 0;
static struct class *kbrgb_class = NULL;
static struct cdev kbrgb_cdev;
static bool kbrgb_dev_added = false;
static struct device *kbrgb_st = NULL;

struct St_RGB {
	u8 R;
	u8 G;
	u8 B;
};

union U_Color {
	struct St_RGB comp;
    u8 Bytes[sizeof(struct St_RGB)];
};

struct St_Zone {
	u8 zone;
	union U_Color color;
};

struct St_Mode {
	u8 mode;
	u8 speed;
	u8 brightness;
	u8 unknown;
	u8 direction;
	union U_Color color;
};

struct St_Writing {
	struct St_Mode M;
	u8 empty[16 - sizeof(struct St_Mode)]; // Fixed size of the packet to write to the firmware
};

union U_Writing{
	struct St_Writing W;
	u8 Bytes[sizeof(struct St_Writing)];
};

static const union U_Writing defaults = {
	.W.M.mode = 0,
	.W.M.speed = DEFAULT_SPEED,
	.W.M.brightness = DEFAULT_BRIGHTNESS,
	.W.M.direction = DEFAULT_DIRECTION,
	.W.M.color = {.Bytes = {DEFAULT_COLOR}},
	.W.empty = {0,0,0,0,0,0,0,0}
};

// offset inside WMI struct
static char acer_wmi_rgp_map(const char in) {
	switch (in) {
		case 'm': // Mode
			return 0;
		case 'v': // Velocity
			return 1;
		case 'b': // Brightness
			return 2;
		case 'd': // Direction
			return 4;
	}
	return 0xff;
}

// Functions related to read and parse messages from device or from init string
static int acer_wmi_rgb_read_next_byte(const struct file *file, void *to, const char __user *from) {
	if (file == NULL) { // read from init configuration string
		*(char *)to = *from;
	} else { // read from device
		//unsigned long err = ;
		if (copy_from_user(to, from, 1) != 0) {
			pr_err("%s Copying data from userspace failed.\n", KBUILD_MODNAME);
			return 1;
		}
	}
	return 0;
}

static int is_valid_digit(const u8 digit, const u8 radix) {
	switch (radix) {
		case 8:
			if ((digit >= '0') && (digit <= '7'))
				return digit - '0';
			return -1;
		case 10:
			if ((digit >= '0') && (digit <= '9'))
				return digit - '0';
			return -2;
		case 16:
			if ((digit >= '0') && (digit <= '9'))
				return digit - '0';
			else if ((digit >= 'a') && (digit <= 'f'))
				return digit - 'a' + 10;
	}
	return -3;
}

static int acer_wmi_rgb_read_num(const struct file *file, const char __user *buf, const size_t count, size_t *read) {
	u8 buffer = 0;
	int n = 0;
	int radix = 10;
	u8 digits = 0;
	while (count - *read > 0) {
		int err = acer_wmi_rgb_read_next_byte(file, &buffer, buf + (*read)++);
		if (err < 0) 
			return err; // An error msg has been already emited.
		buffer = tolower(buffer);
		if ((buffer == ' ') && (digits == 0)) {
			// Just ignore spaces at init
		} else if ((buffer == 'x' && n == 0)) {
			if (digits == 0)
				radix = 8;
			else if (digits == 1)
				radix = 16;
			else {
				pr_err("%s No acceptable number format at pos %lu\n", KBUILD_MODNAME, *read);
				return -1;				
			}
		} else {
			int val = is_valid_digit(buffer, radix);
			if (val >= 0) {
				n = n * radix + val;
				if (n > 255) {
					pr_err("%s No acceptable amount (%i) for a byte at pos %lu\n", KBUILD_MODNAME, n, *read);
					return -2;				
				}
				digits +=1;
			} else {
				--(*read); // Finished reading byte representation. Could be a space or the next command
				break;
			}
		}
	}
	if (digits == 0) {
		pr_err("%s No number found at pos %lu\n", KBUILD_MODNAME, *read);
		return -3;
	}
	//pr_info("%s Read number: %d using radix: %d\n", KBUILD_MODNAME, n, radix);
	return n;
}

static int acer_wmi_rgb_read_color(const struct file *file, const char __user *buf, const size_t count, size_t *read, union U_Color *color) {
	for (int i = 0; i < 3; i++) {
		int err = acer_wmi_rgb_read_num(file, buf, count, read);
		if (err < 0) 
			return err;
		color->Bytes[i] = (u8)err;
	}
	return 0;
}

// Parsing message and writing to HW
static ssize_t acer_wmi_rgb_dev_write(struct file *file, const char __user *buf, size_t count, loff_t *offset) {
	//pr_info("%s Parsing text of size %lu and offset %lli and valid file is %s\n", KBUILD_MODNAME, count, *offset, (file != NULL)?"true":"false");
	size_t read = 0;
	union U_Writing mode;
	const struct acpi_buffer mode_buffer = {sizeof(mode), &mode};
	u8 buffer; // just one byte is enough
	int err;
	acpi_status st;
	memcpy(&mode, &defaults, sizeof(mode));

	while (count - read > 0) {
		//pr_info("%s Reading command byte at pos: %lu from %lu size message\n", KBUILD_MODNAME, read, count);
		err = acer_wmi_rgb_read_next_byte(file, &buffer, buf + read++);
		if (err < 0)
			return err;
		buffer = tolower(buffer);
		//pr_info("%s Buffer: %i \n", KBUILD_MODNAME, buffer);
		if (buffer == ' ' || buffer == 0x0a) {
			// Blank or end line. Ignored
		} else if (buffer == 'z') {
			// Setting zone of static color
			struct St_Zone zone;
			const struct acpi_buffer color_buffer = {sizeof(zone), &zone};
			err = acer_wmi_rgb_read_num(file, buf, count, &read);
			if (err < 0) 
				return err;
			zone.zone = 1 << ((u8)err - 1);
			err = acer_wmi_rgb_read_color(file, buf, count, &read, &zone.color);
			if (err < 0) 
				return err;
			//pr_info("%s Setting color: in zone %d: R:%d G:%d B:%d\n", KBUILD_MODNAME, zone.zone, zone.color.comp.R, zone.color.comp.G, zone.color.comp.B);
			st = wmi_evaluate_method(WMID_GUID4, 0, ACER_WMID_SET_GAMING_STATIC_LED_METHODID, &color_buffer, NULL);
			if (st < 0) {
				pr_err("%s Writting to setting static color zone method failed with code: %i\n", KBUILD_MODNAME, st);
				return st;
			}		
		} else if (buffer == 'c') {
			union U_Color color;
			err = acer_wmi_rgb_read_color(file, buf, count, &read, &color);
			if (err < 0) 
				return err;
			mode.W.M.color = color;
		} else {
			u8 command = acer_wmi_rgp_map(buffer);
			if (command == 0xff) {
				pr_err("%s Not acceptable message at pos %lu\n", KBUILD_MODNAME, read - 1);
				return -3;
			}
			err = acer_wmi_rgb_read_num(file, buf, count, &read);
			if (err < 0) 
				return err;
			//pr_info("%s Byte: %c[%d] --> %i\n", KBUILD_MODNAME, buffer, command, err);
			mode.Bytes[command] = (u8)err;
		}
	}
	mode.Bytes[9] = 1;

	//for (int i = 0; i < 10; i++)
	//	pr_info("%s Setting byte: %d --> %d\n", KBUILD_MODNAME, i, mode.Bytes[i]);
	st = wmi_evaluate_method(WMID_GUID4, 0, ACER_WMID_SET_GAMINGKBBL_METHODID, &mode_buffer, NULL);
	if (st < 0) {
		pr_err("%s Writting to setting static color zone method failed with code: %i\n", KBUILD_MODNAME, st);
		return st;
	}
	return read;
}

// User space device creation
static const struct file_operations acer_wmi_rgb_dev_ops = {
		.owner      = THIS_MODULE,
		.write       = acer_wmi_rgb_dev_write
};

static int acer_wmi_rgb_dev_event(
#if RTLNX_VER_MIN(6, 2, 0)
const
#endif
struct device *dev, struct kobj_uevent_env *env)
{
	add_uevent_var(env, "DEVMODE=%#o", 0666);
	return 0;
}

static int __init acer_wmi_rgb_dev_init(void)
{
	dev_t dev;
	int err;

	err = alloc_chrdev_region(&dev, 0, 1, GAMING_KB_RGB_DEV_NAME);
	if (err < 0) {
		pr_err("%s Registering user space device for acer keyboard color failed: %d\n", KBUILD_MODNAME, err);
		return err;
	}

	kbrgb_dev = dev;

	#if RTLNX_VER_MIN(6, 4, 0)
		kbrgb_class = class_create(GAMING_KB_RGB_DEV_NAME);
	#else
		kbrgb_class = class_create(THIS_MODULE, GAMING_KB_RGB_DEV_NAME);
	#endif

	kbrgb_class->dev_uevent = acer_wmi_rgb_dev_event;

	cdev_init(&kbrgb_cdev, &acer_wmi_rgb_dev_ops);
	kbrgb_cdev.owner = THIS_MODULE;

	if (cdev_add(&kbrgb_cdev, dev, 1) < 0){
		pr_err("%s Adding user space device for acer keyboard color failed: %d\n", KBUILD_MODNAME, err);
		return err;
	}

	kbrgb_dev_added = true;

	kbrgb_st = device_create(kbrgb_class, NULL, dev, NULL, "%s-%d", GAMING_KB_RGB_DEV_NAME, 0);

	if (kbrgb_st == NULL) {
		pr_err("%s Creating user space device for acer keyboard color failed: %d\n", KBUILD_MODNAME, err);
		return -2;
	}
	
	return 0;
}

static void __exit acer_wmi_rgb_dev_exit(void)
{
	if (kbrgb_st != NULL)
		device_destroy(kbrgb_class, kbrgb_dev);
		
	if (kbrgb_dev_added)
		cdev_del(&kbrgb_cdev);

	if (kbrgb_class != NULL)
		class_destroy(kbrgb_class);

	if (kbrgb_dev != 0)
		unregister_chrdev_region(kbrgb_dev, 1);
}

// PM notif callback function
static int pm_callback(struct notifier_block *nb, unsigned long action, void *data)
{
	if (action == PM_POST_HIBERNATION && init_conf != NULL && strlen(init_conf) > 0) {
		// Restoring keyboard color status after hibernation
		acer_wmi_rgb_dev_write(NULL, init_conf, strlen(init_conf), 0);
	}
	return 0;
}

static struct notifier_block notif_block = {
		.next = NULL,
		.notifier_call = pm_callback,
		.priority = 0
};


// Module functions
int init_module(void) 
{
	pr_info("%s Init\n", KBUILD_MODNAME);
	int err;
	if (!wmi_has_guid(WMID_GUID4)) {
		pr_alert("This computer has no needed ACER WMI fuctionality");
		return -1;
	}

	err = acer_wmi_rgb_dev_init();
	if (err != 0) {
		return err;		
	}

	
	if ((init_conf != NULL) && (strlen(init_conf) > 0)) {
		loff_t offset = 0;
		acer_wmi_rgb_dev_write(NULL, init_conf, strlen(init_conf), &offset);
	}
	
	//kstrtob(); can't use because it needs null terminated string

	register_pm_notifier(&notif_block);
    return 0; 
} 
 
void cleanup_module(void) 
{
	acer_wmi_rgb_dev_exit();
	unregister_pm_notifier(&notif_block);
    pr_info("%s End\n", KBUILD_MODNAME);
} 