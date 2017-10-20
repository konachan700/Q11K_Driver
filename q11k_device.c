#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/dmi.h>
#include "compat.h"
#include <linux/version.h>
#include <linux/hid.h>
#include <linux/usb.h>
#include <asm/unaligned.h>

#define	hid_to_usb_dev(hid_dev) container_of(hid_dev->dev.parent->parent, struct usb_device, dev)

#define MODULENAME                     "q11k_device"
#define DEVNAME                        "Huion Q11K Tablet"

#define USB_VENDOR_ID_HUION		        0x256c
#define USB_DEVICE_ID_HUION_TABLET	    0x006e

#define CONFIG_BUF_SIZE                 514

#define DEBUG
#define DPRINT(d, ...)       printk(d, ##__VA_ARGS__)
#define DPRINT_DEEP(d, ...)  //printk(d, ##__VA_ARGS__)

#define Q11K_KEYMAP_SIZE 11
static unsigned short def_keymap[Q11K_KEYMAP_SIZE] = {
    KEY_0, KEY_1, KEY_2, KEY_3,  
    KEY_4, KEY_5, KEY_6, KEY_7,  
    BTN_MIDDLE, BTN_RIGHT, KEY_RIGHTCTRL
};

struct input_dev 
    *idev               = NULL, 
    *idev_keyboard      = NULL;
    
int 
    pressure_last       = 0, 
    stylus_pressed      = 0;

static int q11k_probe(struct hid_device *hdev, const struct hid_device_id *id) {
    int rc, i;
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
    struct usb_device *usb_dev = interface_to_usbdev(intf);
    unsigned long quirks = id->driver_data;
    int if_number = intf->cur_altsetting->desc.bInterfaceNumber;
    char buf[CONFIG_BUF_SIZE];
    
    hdev->quirks |= HID_QUIRK_MULTI_INPUT;
	hdev->quirks |= HID_QUIRK_NO_EMPTY_INPUT;
    
    if (id->product == USB_DEVICE_ID_HUION_TABLET) {
        DPRINT("q11k device detected if=%d", if_number);
        
        hid_set_drvdata(hdev, (void *)quirks);
        
        rc = hid_parse(hdev);
        if (rc) {
            hid_err(hdev, "parse failed\n");
            return rc;
        }
        
        rc = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
        if (rc) {
            hid_err(hdev, "hw start failed\n");
            return rc;
        }
        
        rc = hid_hw_open(hdev);
        if (rc) {
            hid_err(hdev, "cannot open hidraw\n");
            return rc;
        }
        
        if (if_number == 1) {
            idev = input_allocate_device();
            if (idev == NULL) {
                hid_err(hdev, "failed to allocate input device\n");
                return -ENOMEM;
            }
            
            input_set_drvdata(idev, hdev);
            
            idev->name       = "Huion Q11K Tablet";
            idev->id.bustype = BUS_USB;
            idev->id.vendor  = 0x56a;
            idev->id.version = 0;
            idev->dev.parent = &hdev->dev;
            
            set_bit(EV_REP, idev->evbit);
            set_bit(EV_KEY, idev->evbit);
            set_bit(EV_ABS, idev->evbit);
            set_bit(BTN_TOOL_PEN, idev->keybit); 
            set_bit(BTN_STYLUS, idev->keybit);
            set_bit(BTN_STYLUS2, idev->keybit);
            
            input_set_abs_params(idev, ABS_X, 1, 55662, 0, 0);
            input_set_abs_params(idev, ABS_Y, 1, 34789, 0, 0);
            input_set_abs_params(idev, ABS_PRESSURE, 1, 8192, 0, 0);

            rc = input_register_device(idev);
            if (rc) {
                hid_err(hdev, "error registering the input device\n");
                input_free_device(idev);
                return rc;
            }
        } else if (if_number == 0) {
            rc = usb_string(usb_dev, 0x02, buf, CONFIG_BUF_SIZE);
            if (rc > 0) DPRINT("String(0x02) = %s", buf);
            
            rc = usb_string(usb_dev, 0xc9, buf, 256);
            if (rc > 0) DPRINT("String(0xc9) = %s", buf);
            
            rc = usb_string(usb_dev, 0xc8, buf, 256);
            if (rc > 0) DPRINT("String(0xc8) = %s", buf);
            
            rc = usb_string(usb_dev, 0xca, buf, 256);
            if (rc > 0) DPRINT("String(0xca) = %s", buf);
            
            idev_keyboard = input_allocate_device();
            if (idev_keyboard == NULL) {
                hid_err(hdev, "failed to allocate input device [kb]\n");
                return -ENOMEM;
            }
            
            idev_keyboard->name                 = "Huion Q11K Keyboard";
            idev_keyboard->id.bustype           = BUS_USB;
            idev_keyboard->id.vendor            = 0x04b4;
            idev_keyboard->id.version           = 0;
            idev_keyboard->keycode              = def_keymap;
            idev_keyboard->keycodemax           = Q11K_KEYMAP_SIZE;
            idev_keyboard->keycodesize          = sizeof(def_keymap[0]);
            
            input_set_capability(idev_keyboard, EV_MSC, MSC_SCAN);
            
            for (i=0; i<Q11K_KEYMAP_SIZE; i++) {
                input_set_capability(idev_keyboard, EV_KEY, def_keymap[i]);
            }
            
            rc = input_register_device(idev_keyboard);
            if (rc) {
                hid_err(hdev, "error registering the input device [kb]\n");
                input_free_device(idev_keyboard);
                return rc;
            }
        } 
        
        DPRINT("q11k device ok");
    } else {
        DPRINT("q11k hid strange error");
        return -ENODEV;
    }
    
    return 0;
}

static void __q11k_rkey_press(unsigned short key, int b_key_raw, int s) {
    input_report_key(idev_keyboard, KEY_RIGHTCTRL, s);
    input_sync(idev_keyboard);
    input_report_key(idev_keyboard, key, s);
    input_sync(idev_keyboard);
}

static void q11k_rkey_press(unsigned short key, int b_key_raw) {
    __q11k_rkey_press(key, b_key_raw, 1);
    __q11k_rkey_press(key, b_key_raw, 0);
}

static void __upress_pen(void) {
    if (stylus_pressed == 1) {
        input_report_key(idev_keyboard, BTN_MIDDLE, 0);
        input_report_key(idev_keyboard, BTN_RIGHT, 0);
        input_sync(idev_keyboard);
    }
    stylus_pressed = 0;
}

static int q11k_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size) {
    int pressure, x_pos, y_pos;
    
    if ((idev_keyboard == NULL) || (idev == NULL)) return -ENODEV;
    
    DPRINT_DEEP("q11k_raw_event: %d\t%*phC", size, size, data);
    
    if ((size == 12) && (data[0] == 0x08)) {
        if (data[1] == 0xe0) {
            switch (data[4]) {
                case 0x01:
                    q11k_rkey_press(KEY_0, data[4]);
                    break;
                case 0x02:
                    q11k_rkey_press(KEY_1, data[4]);
                    break;
                case 0x04:
                    q11k_rkey_press(KEY_2, data[4]);
                    break;
                case 0x08:
                    q11k_rkey_press(KEY_3, data[4]);
                    break;
                case 0x10:
                    q11k_rkey_press(KEY_4, data[4]);
                    break;
                case 0x20:
                    q11k_rkey_press(KEY_5, data[4]);
                    break;
                case 0x40:
                    q11k_rkey_press(KEY_6, data[4]);
                    break;
                case 0x80:
                    q11k_rkey_press(KEY_7, data[4]);
                    break;
            }
            return 0;
        }
        
        x_pos           = data[3] * 0xFF + data[2];
        y_pos           = data[5] * 0xFF + data[4];
        pressure        = data[7] * 0xFF + data[6];
        
        DPRINT_DEEP("sensors: x=%08d y=%08d pressure=%08d", x_pos, y_pos, pressure);

        switch (data[1]) {
            case 0x80:
                __upress_pen();
                input_report_key(idev, BTN_TOOL_PEN, 0);
                input_report_abs(idev, ABS_PRESSURE, 0);
                break;
            case 0x81:
                __upress_pen();
                input_report_key(idev, BTN_TOOL_PEN, 1);
                input_report_abs(idev, ABS_PRESSURE, pressure);
                break;
            case 0x82:
                if (stylus_pressed == 0) {
                    input_report_key(idev_keyboard, BTN_MIDDLE, 1);
                    input_sync(idev_keyboard);
                }
                stylus_pressed = 1;
                break;
            case 0x84:
                if (stylus_pressed == 0) {
                    input_report_key(idev_keyboard, BTN_RIGHT, 1);
                    input_sync(idev_keyboard);
                }
                stylus_pressed = 1;
                break;
        }
        
        input_report_abs(idev, ABS_X, x_pos);
        input_report_abs(idev, ABS_Y, y_pos);
        input_sync(idev);
    }
    
    return 0;
}

#ifdef CONFIG_PM
static int uclogic_resume(struct hid_device *hdev) {

	return 0;
}
#endif

static void __close_keyboard(void) {
    if (idev_keyboard != NULL) {
        input_unregister_device(idev_keyboard);
        input_free_device(idev_keyboard);
        idev_keyboard = NULL;
        DPRINT("Q11K keyboard unregistered");
    }
}

static void __close_pad(void) {
    if (idev != NULL) {
        input_unregister_device(idev);
        input_free_device(idev);
        idev = NULL;
        DPRINT("Q11K tab unregistered");
    }
}

void q11k_remove(struct hid_device *dev) {
    struct usb_interface *intf = to_usb_interface(dev->dev.parent);
    if (intf->cur_altsetting->desc.bInterfaceNumber == 0) {
        __close_keyboard();
    } else if (intf->cur_altsetting->desc.bInterfaceNumber == 1) {
        __close_pad();
    }
    hid_hw_close(dev);
    hid_hw_stop(dev);
}

struct wacom_features {
	const char *name;
	int x_max;
	int y_max;
	int pressure_max;
	int distance_max;
	int type;
	int x_resolution;
	int y_resolution;
	int numbered_buttons;
	int offset_left;
	int offset_right;
	int offset_top;
	int offset_bottom;
	int device_type;
	int x_phy;
	int y_phy;
	unsigned unit;
	int unitExpo;
	int x_fuzz;
	int y_fuzz;
	int pressure_fuzz;
	int distance_fuzz;
	int tilt_fuzz;
	unsigned quirks;
	unsigned touch_max;
	int oVid;
	int oPid;
	int pktlen;
	bool check_for_hid_type;
	int hid_type;
};

static const struct wacom_features wacom_features =
	{ "Wacom Penpartner", 55662, 34789, 8192, 0, 4, 40, 40 };

static const struct hid_device_id q11k_device[] = {
    { HID_USB_DEVICE(USB_VENDOR_ID_HUION, USB_DEVICE_ID_HUION_TABLET), .driver_data = (kernel_ulong_t)&wacom_features },
    {}
};

static struct hid_driver q11k_driver = {
	.name                  = MODULENAME,
	.id_table              = q11k_device,
	.probe                 = q11k_probe,
    .remove                = q11k_remove, 
	.raw_event             = q11k_raw_event,
#ifdef CONFIG_PM
	.resume	               = uclogic_resume,
	.reset_resume          = uclogic_resume,
#endif
};
module_hid_driver(q11k_driver);

MODULE_AUTHOR("Konata Izumi <konachan.700@gmail.com>");
MODULE_DESCRIPTION("Huion Q11K device driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");

MODULE_DEVICE_TABLE(hid, q11k_device);
