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

#define USB_VENDOR_ID_HUION		       0x256c
#define USB_DEVICE_ID_HUION_TABLET	   0x006e

#define DEBUG

#define Q11K_RDESC_ORIG_SIZE1 83
static __u8 q11k_rdesc_fixed1[] = {
    0x05, 0x0D,        // Usage Page (Digitizer)
    0x09, 0x02,        // Usage (Pen)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x0A,        //   Report ID (10)
    0x09, 0x20,        //   Usage (Stylus)
    0xA1, 0x00,        //   Collection (Physical)
    0x09, 0x42,        //     Usage (Tip Switch)
    0x09, 0x44,        //     Usage (Barrel Switch)
    0x09, 0x45,        //     Usage (Eraser)
    0x09, 0x3C,        //     Usage (Invert)
    0x09, 0x43,        //     Usage (Secondary Tip Switch)
    0x09, 0x44,        //     Usage (Barrel Switch)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x75, 0x01,        //     Report Size (1)
    0x95, 0x06,        //     Report Count (6)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x09, 0x32,        //     Usage (In Range)
    0x75, 0x01,        //     Report Size (1)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x81, 0x03,        //     Input (Const,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x55, 0x0D,        //     Unit Exponent (-3)
    0x65, 0x33,        //     Unit (System: English Linear, Length: Inch)
    0x26, 0xFF, 0x7F,  //     Logical Maximum (32767)
    0x35, 0x00,        //     Physical Minimum (0)
    0x46, 0x00, 0x08,  //     Physical Maximum (2048)
    0x75, 0x10,        //     Report Size (16)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x0D,        //     Usage Page (Digitizer)
    0x09, 0x30,        //     Usage (Tip Pressure)
    0x26, 0xFF, 0x1F,  //     Logical Maximum (8191)
    0x75, 0x10,        //     Report Size (16)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

#define Q11K_RDESC_ORIG_SIZE0 18
static __u8 q11k_rdesc_fixed0[] = {
    0x06, 0x00, 0xFF,  // Usage Page (Vendor Defined 0xFF00)
    0x09, 0x01,        // Usage (0x01)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x08,        //   Report ID (8)
    0x75, 0x58,        //   Report Size (88)
    0x95, 0x01,        //   Report Count (1)
    0x09, 0x01,        //   Usage (0x01)
    0x81, 0x02,        //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              // End Collection
};

#define Q11K_KEYMAP_SIZE 11
static unsigned short def_keymap[Q11K_KEYMAP_SIZE] = {
    KEY_0, KEY_1, KEY_2, KEY_3,  
    KEY_4, KEY_5, KEY_6, KEY_7,  
    KEY_8, KEY_9, KEY_RIGHTCTRL
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
    unsigned long quirks = id->driver_data;
    int if_number = intf->cur_altsetting->desc.bInterfaceNumber;
    
    hdev->quirks |= HID_QUIRK_MULTI_INPUT;
	hdev->quirks |= HID_QUIRK_NO_EMPTY_INPUT;
    
    if (id->product == USB_DEVICE_ID_HUION_TABLET) {
        printk("q11k device detected if=%d", if_number);
        
        hid_set_drvdata(hdev, (void *)quirks);
        
        rc = hid_parse(hdev);
        if (rc) {
            hid_err(hdev, "parse failed\n");
            return rc;
        }
        
        rc = hid_hw_start(hdev, HID_CONNECT_HIDRAW);//HID_CONNECT_DEFAULT);
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
            
            idev->name = "Huion Q11K Tablet";
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
            
            input_set_abs_params(idev, ABS_X, 1, 32640, 0, 0);
            input_set_abs_params(idev, ABS_Y, 1, 32640, 0, 0);
            input_set_abs_params(idev, ABS_PRESSURE, 1, 8192, 0, 0);

            
            rc = input_register_device(idev);
            if (rc) {
                hid_err(hdev, "error registering the input device\n");
                input_free_device(idev);
                return rc;
            }
        } else if (if_number == 0) {
            idev_keyboard = input_allocate_device();
            if (idev_keyboard == NULL) {
                hid_err(hdev, "failed to allocate input device [kb]\n");
                return -ENOMEM;
            }
            
            idev_keyboard->name = "Huion Q11K Keyboard";
            idev_keyboard->id.bustype = BUS_USB;
            idev_keyboard->id.vendor  = 0x04b4;
            idev_keyboard->id.version = 0;
            idev_keyboard->keycode = def_keymap;
            idev_keyboard->keycodemax  = Q11K_KEYMAP_SIZE;
            idev_keyboard->keycodesize = sizeof(def_keymap[0]);
            
            set_bit(EV_REP, idev_keyboard->evbit);
            set_bit(EV_KEY, idev_keyboard->evbit);
            
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
        
        printk("q11k device ok");
    } else {
        printk("q11k hid strange error");
        return -ENODEV;
    }
    
    return 0;
}

static __u8 *q11k_report_fixup(struct hid_device *hdev, __u8 *rdesc, unsigned int *rsize) {
    struct usb_interface *iface = to_usb_interface(hdev->dev.parent);
    if (hdev->product == USB_DEVICE_ID_HUION_TABLET) {
        switch (iface->cur_altsetting->desc.bInterfaceNumber) {
            case 0:
                printk("USB_DEVICE_ID_HUION_TABLET (rsize=%d, size=%d)", *rsize, Q11K_RDESC_ORIG_SIZE0);
                if (*rsize == Q11K_RDESC_ORIG_SIZE0) {
                    rdesc = q11k_rdesc_fixed0;
                    *rsize = sizeof(q11k_rdesc_fixed0);
                    printk("USB_DEVICE_ID_HUION_TABLET 0 OK");
                }
                break;
            case 1:
                printk("USB_DEVICE_ID_HUION_TABLET (rsize=%d, size=%d)", *rsize, Q11K_RDESC_ORIG_SIZE1);
                if (*rsize == Q11K_RDESC_ORIG_SIZE1) {
                    rdesc = q11k_rdesc_fixed1;
                    *rsize = sizeof(q11k_rdesc_fixed1);
                    printk("USB_DEVICE_ID_HUION_TABLET 1 OK");
                }
                break;            
        }
    }
    return rdesc;
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

static int q11k_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size) {
    int key_raw = 0, pressure, x_pos, y_pos;
    
    //printk("q11k_raw_event %d -- %d -- Buf: %*phC", size, key_raw, size, data);
    //printk("hid_report %d -- %d -- Buf: %*phC", report->maxfield, (int) sizeof(struct hid_report), (int) sizeof(struct hid_report), report);
    
    if ((size == 8) && (data[0] == 0x0a) && (
        (data[1] == 0xc1) || (data[1] == 0xc2) || (data[1] == 0xc0) || (data[1] == 0xc4)
    )) {       
        x_pos = data[3] * 0xFF + data[2];
        y_pos = data[5] * 0xFF + data[4];
        pressure = (data[1] != 0xc1) ? pressure_last : data[7] * 0xFF + data[6];
        
        pressure_last = pressure;
        
        //printk("sensors: x=%05d y=%05d pressure=%05d", x_pos, y_pos, pressure);
        
        input_report_abs(idev, ABS_MISC, 0);
        
        if ((data[1] == 0xc2) || (data[1] == 0xc4)) {
            if (stylus_pressed == 0) {
                if (data[1] == 0xc2) __q11k_rkey_press(KEY_8, data[4], 1);
                if (data[1] == 0xc4) __q11k_rkey_press(KEY_9, data[4], 1);
            }
            stylus_pressed = 1;
        } else {
            if (stylus_pressed == 1) {
                __q11k_rkey_press(KEY_8, data[4], 0);
                __q11k_rkey_press(KEY_9, data[4], 0);
            }
            stylus_pressed = 0;
        }
        
        
        if (data[1] == 0xc0) {
            input_report_key(idev, BTN_TOOL_PEN, 0);
            input_report_abs(idev, ABS_PRESSURE, 0);
        } else {
            input_report_key(idev, BTN_TOOL_PEN, 1);
            input_report_abs(idev, ABS_PRESSURE, pressure);
        }
        
        input_report_abs(idev, ABS_X, x_pos);
        input_report_abs(idev, ABS_Y, y_pos);
        
        input_sync(idev);
    }

    if ((size == 8) && (data[0] == 0x0a) && (data[1] == 0xe0) && (data[2] == 0x01) && (data[3] == 0x01)) {
        key_raw = data[4];
        switch (key_raw) {
            case 0x00:
                // nothing to do...
                break;
            case 0x01:
                q11k_rkey_press(KEY_0, key_raw);
                break;
            case 0x02:
                q11k_rkey_press(KEY_1, key_raw);
                break;
            case 0x04:
                q11k_rkey_press(KEY_2, key_raw);
                break;
            case 0x08:
                q11k_rkey_press(KEY_3, key_raw);
                break;
            case 0x10:
                q11k_rkey_press(KEY_4, key_raw);
                break;
            case 0x20:
                q11k_rkey_press(KEY_5, key_raw);
                break;
            case 0x40:
                q11k_rkey_press(KEY_6, key_raw);
                break;
            case 0x80:
                q11k_rkey_press(KEY_7, key_raw);
                break;
            default:
                printk("Unknown button code captured. Ignored.");
        }
    }
    
    return 0;
}

#ifdef CONFIG_PM
static int uclogic_resume(struct hid_device *hdev) {

	return 0;
}
#endif

void q11k_remove(struct hid_device *dev) {
    struct usb_interface *intf = to_usb_interface(dev->dev.parent);
    if (intf->cur_altsetting->desc.bInterfaceNumber == 0) {
                input_unregister_device(idev_keyboard);
        input_free_device(idev_keyboard);
        idev_keyboard = NULL;
        printk("Q11K keyboard unregistered");
    } else if (intf->cur_altsetting->desc.bInterfaceNumber == 1) {
        input_unregister_device(idev);
        input_free_device(idev);
        idev = NULL;
        printk("Q11K tab unregistered");
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


//32640
static const struct wacom_features wacom_features =
	{ "Wacom Penpartner", 32640, 32640, 8192, 0, 4, 40, 40 };

static const struct hid_device_id q11k_device[] = {
    { HID_USB_DEVICE(USB_VENDOR_ID_HUION, USB_DEVICE_ID_HUION_TABLET), .driver_data = (kernel_ulong_t)&wacom_features },
    {}
};

static struct hid_driver q11k_driver = {
	.name                  = MODULENAME,
	.id_table              = q11k_device,
	.probe                 = q11k_probe,
    .remove                = q11k_remove, 
	.report_fixup          = q11k_report_fixup,
	.raw_event             = q11k_raw_event,
#ifdef CONFIG_PM
	.resume	               = uclogic_resume,
	.reset_resume          = uclogic_resume,
#endif
};
module_hid_driver(q11k_driver);

MODULE_AUTHOR("Konata Izumi <konachan.700@gtmail.com>");
MODULE_DESCRIPTION("Huion Q11K device driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0.0");

MODULE_DEVICE_TABLE(hid, q11k_device);
