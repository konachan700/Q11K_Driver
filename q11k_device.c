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
#include <linux/jiffies.h>
#include <asm/unaligned.h>
#include <stdbool.h>


#define	hid_to_usb_dev(hid_dev) container_of(hid_dev->dev.parent->parent, struct usb_device, dev)

#define MODULENAME                     "q11k_device"
#define DEVNAME                        "Huion Q11K Tablet"

#define USB_VENDOR_ID_HUION		        0x256c
#define USB_DEVICE_ID_HUION_TABLET	    0x006e

#define CONFIG_BUF_SIZE                 514

#define MAX_ABS_X 50800
#define MAX_ABS_Y 31750
#define MAX_ABS_PRESSURE 8192

#define REL_PEN_DIV 1
#define REL_PEN_POS_RESET_SKIP_COUNT 1
#define REL_PEN_UP_TICK 10

#define DEBUG
#define DPRINT(d, ...)       printk(d, ##__VA_ARGS__)
#define DPRINT_DEEP(d, ...)  //printk(d, ##__VA_ARGS__)

#define Q11K_KEY_TOP_LEFT KEY_LEFTBRACE
#define Q11K_KEY_TOP_MIDDLE KEY_RIGHTBRACE
#define Q11K_KEY_TOP_RIGHT KEY_COMMA
#define Q11K_KEY_BOTTOM_LEFT KEY_DOT
#define Q11K_KEY_BOTTOM_MIDDLE KEY_SLASH
#define Q11K_KEY_BOTTOM_RIGHT KEY_BACKSLASH
#define Q11K_KEY_6 KEY_SEMICOLON
#define Q11K_KEY_7 KEY_APOSTROPHE

#define Q11K_VKEY_1_CLICK BTN_LEFT
#define Q11K_VKEY_2_CLICK BTN_RIGHT
#define Q11K_VKEY_4_CLICK KEY_ESC

#define Q11K_VKEY_2_UP       KEY_KPPLUS
#define Q11K_VKEY_2_DOWN     KEY_KPMINUS
#define Q11K_VKEY_2_LEFT     KEY_F16
#define Q11K_VKEY_2_RIGHT    KEY_F17

#define Q11K_VKEY_3_UP       KEY_F18
#define Q11K_VKEY_3_DOWN     KEY_F19
#define Q11K_VKEY_3_LEFT     KEY_F20
#define Q11K_VKEY_3_RIGHT    KEY_F21

#define Q11K_VKEY_4_MOVE       KEY_F22

#define Q11K_STYLUS_KYE_TYPE 0

#if Q11K_STYLUS_KYE_TYPE == 0    // STYLUS BUTTON on pen
    #define Q11K_STYLUS_KEY_DEVICE idev_pen
    #define Q11K_STYLUS_KEY_1 BTN_STYLUS
    #define Q11K_STYLUS_KEY_2 BTN_STYLUS2
    #define Q11K_STYLUS_KEY_SYNC()
#elif Q11K_STYLUS_KYE_TYPE == 1  // MOUSE BUTTON
    #define Q11K_STYLUS_KEY_DEVICE idev_keyboard
    #define Q11K_STYLUS_KEY_1 BTN_MIDDLE
    #define Q11K_STYLUS_KEY_2 BTN_RIGHT
    #define Q11K_STYLUS_KEY_SYNC() input_sync(idev_keyboard)
#else
#error "unknown stylus key type"
#endif

typedef unsigned short (*q11k_key_mapping_func_t)(u8 b_key_raw, unsigned short** last_key_pp);

typedef struct __tag_relative_pen_t
{
    bool enabled;
    int last_x;
    int last_y;

    int origin_x;
    int origin_y;

    int reseting_count;

    u64 last_jiffies;
} relative_pen_t;

static unsigned short def_keymap[] = {
    Q11K_KEY_TOP_LEFT,
    Q11K_KEY_TOP_MIDDLE,
    Q11K_KEY_TOP_RIGHT,
    Q11K_KEY_BOTTOM_LEFT,
    Q11K_KEY_BOTTOM_MIDDLE,
    Q11K_KEY_BOTTOM_RIGHT,
    Q11K_KEY_6,
    Q11K_KEY_7,

    BTN_MIDDLE,
    BTN_RIGHT,

    KEY_RIGHTCTRL,
    KEY_RIGHTALT,

    Q11K_VKEY_1_CLICK,
    Q11K_VKEY_2_CLICK,
    Q11K_VKEY_4_CLICK,
    Q11K_VKEY_2_UP,
    Q11K_VKEY_2_DOWN,
    Q11K_VKEY_2_LEFT,
    Q11K_VKEY_2_RIGHT,

    Q11K_VKEY_3_UP,
    Q11K_VKEY_3_DOWN,
    Q11K_VKEY_3_LEFT,
    Q11K_VKEY_3_RIGHT,

    Q11K_VKEY_4_MOVE
};

static const int Q11k_KeyMapSize = sizeof(def_keymap) / sizeof(def_keymap[0]);

struct input_dev* idev_pen = NULL;
struct input_dev* idev_keyboard = NULL;

static bool stylus_pressed = false;
static bool stylus2_pressed = false;

static unsigned short last_key = 0;
static unsigned short last_vkey = 0;


static relative_pen_t rel_pen_data = {
    false,  // enabled
    -1, // last_x
    -1, // last_y
    0,  // origin_x
    0,  // origin_y
    0,  // reseting_count
    0   // last_jiffies
    };


static int q11k_probe(struct hid_device *hdev, const struct hid_device_id *id);

static int q11k_prepare_pens(struct hid_device *hdev);
static int q11k_register_pen(struct hid_device *hdev);
static int q11k_register_relative_pen(struct hid_device *hdev);
static int q11k_register_keyboard(struct hid_device *hdev, struct usb_device *usb_dev);

static int q11k_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size);

static void q11k_handle_key_event(u8 b_key_raw);
static void q11k_handle_gesture_event(u8 b_key_raw);
static void q11k_handle_mouse_event(int x_pos, int y_pos);
static void q11k_handle_pen_event(u8 b_key_raw, int x_pos, int y_pos, int pressure);

static void q11k_handle_key_mapping_event(
    unsigned short keys[],
    int keyc,
    u8 b_key_raw,
    q11k_key_mapping_func_t kmp_func);
static unsigned short q11k_mapping_keys(u8 b_key_raw, unsigned short** last_key_pp);
static unsigned short q11k_mapping_gesture_keys(u8 b_key_raw, unsigned short** last_key_pp);

static void q11k_report_keys(const int keyc, const unsigned short* keys, int s);
static void __upress_pen(void);

static void q11k_calculate_pen_data(const u8* data, int* x_pos, int* y_pos, int* pressure);
static void q11k_calculate_mouse_data(const u8* data, int* x_pos, int* y_pos);

static void q11k_relative_pen_toggle(void);
static bool q11k_relative_pen_is_enabled(void);
static void q11k_relative_pen_enable(void);
static void q11k_relative_pen_disable(void);
static void q11k_relative_pen_reset_origin(void);
static void q11k_relative_pen_update_origin(int x, int y);
static void q11k_relative_pen_check_and_try_reset_last_abs_pos(void);
static void q11k_relative_pen_reset_last_abs_pos(void);
static void q11k_relative_pen_limit_xy(int* xp, int* yp);
static void q11k_relative_pen_update_last_abs_pos(int x, int y);
static void q11k_relative_pen_get_rel_pos(int abs_x, int abs_y, int* rel_x, int* rel_y);

static int q11k_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
    int rc = 0;
    struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
    struct usb_device *usb_dev = interface_to_usbdev(intf);
    unsigned long quirks = id->driver_data;
    int if_number = intf->cur_altsetting->desc.bInterfaceNumber;

    hdev->quirks |= HID_QUIRK_MULTI_INPUT;
	hdev->quirks |= HID_QUIRK_NO_EMPTY_INPUT;

    if (id->product == USB_DEVICE_ID_HUION_TABLET) {
        DPRINT("q11k device detected if=%d", if_number);

        hid_set_drvdata(hdev, (void *)quirks);

        rc = hid_parse(hdev);
        if (rc)
        {
            hid_err(hdev, "parse failed\n");
            return rc;
        }

        rc = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
        if (rc)
        {
            hid_err(hdev, "hw start failed\n");
            return rc;
        }

        rc = hid_hw_open(hdev);
        if (rc)
        {
            hid_err(hdev, "cannot open hidraw\n");
            return rc;
        }

        if (if_number == 1)
        {
            rc = q11k_register_pen(hdev);
        }
        else if (if_number == 0)
        {
            rc = q11k_register_keyboard(hdev, usb_dev);
        }

        if (rc == 0)
        {
            return rc;
        }

        DPRINT("q11k device ok");
    }
    else
    {
        DPRINT("q11k hid strange error");
        return -ENODEV;
    }

    return 0;
}

static int q11k_register_pen(struct hid_device *hdev)
{
    int rc;

    idev_pen = input_allocate_device();
    if (idev_pen == NULL)
    {
        hid_err(hdev, "failed to allocate input device for pen\n");
        return -ENOMEM;
    }

    input_set_drvdata(idev_pen, hdev);

    idev_pen->name       = "Huion Q11K Tablet";
    idev_pen->id.bustype = BUS_USB;
    idev_pen->id.vendor  = 0x56a;
    idev_pen->id.version = 0;
    idev_pen->dev.parent = &hdev->dev;

    set_bit(EV_REP, idev_pen->evbit);

    input_set_capability(idev_pen, EV_ABS, ABS_X);
	input_set_capability(idev_pen, EV_ABS, ABS_Y);
    input_set_capability(idev_pen, EV_ABS, ABS_PRESSURE);
    input_set_capability(idev_pen, EV_KEY, BTN_TOOL_PEN);
    input_set_capability(idev_pen, EV_KEY, BTN_STYLUS);
    input_set_capability(idev_pen, EV_KEY, BTN_STYLUS2);

    input_set_abs_params(idev_pen, ABS_X, 1, 50800, 0, 0);  // 55662
    input_set_abs_params(idev_pen, ABS_Y, 1, 31750, 0, 0);  // 34789
    input_set_abs_params(idev_pen, ABS_PRESSURE, 1, 8192, 0, 0);

    rc = input_register_device(idev_pen);
    if (rc)
    {
        hid_err(hdev, "error registering the input device for pen\n");
        input_free_device(idev_pen);
        return rc;
    }
    return 0;
}

static int q11k_register_keyboard(struct hid_device *hdev, struct usb_device *usb_dev)
{
    int rc = 0;
    int i = 0;
    char buf[CONFIG_BUF_SIZE];
    rc = usb_string(usb_dev, 0x02, buf, CONFIG_BUF_SIZE);
    if (rc > 0) DPRINT("String(0x02) = %s", buf);

    rc = usb_string(usb_dev, 0xc9, buf, 256);
    if (rc > 0) DPRINT("String(0xc9) = %s", buf);

    rc = usb_string(usb_dev, 0xc8, buf, 256);
    if (rc > 0) DPRINT("String(0xc8) = %s", buf);

    rc = usb_string(usb_dev, 0xca, buf, 256);
    if (rc > 0) DPRINT("String(0xca) = %s", buf);

    idev_keyboard = input_allocate_device();
    if (idev_keyboard == NULL)
    {
        hid_err(hdev, "failed to allocate input device [kb]\n");
        return -ENOMEM;
    }

    idev_keyboard->name                 = "Huion Q11K Keyboard";
    idev_keyboard->id.bustype           = BUS_USB;
    idev_keyboard->id.vendor            = 0x04b4;
    idev_keyboard->id.version           = 0;
    idev_keyboard->keycode              = def_keymap;
    idev_keyboard->keycodemax           = Q11k_KeyMapSize;
    idev_keyboard->keycodesize          = sizeof(def_keymap[0]);

    input_set_capability(idev_keyboard, EV_MSC, MSC_SCAN);

    for (i=0; i<Q11k_KeyMapSize; i++)
    {
        input_set_capability(idev_keyboard, EV_KEY, def_keymap[i]);
    }

    rc = input_register_device(idev_keyboard);
    if (rc)
    {
        hid_err(hdev, "error registering the input device [kb]\n");
        input_free_device(idev_keyboard);
        return rc;
    }

    return 0;
}

static int q11k_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size)
{
    int pressure, x_pos, y_pos;

    if ((idev_keyboard == NULL) || (idev_pen == NULL)) return -ENODEV;

    DPRINT_DEEP("q11k_raw_event: %d\t%*phC", size, size, data);

    if ((size == 12) && (data[0] == 0x08))
    {
        switch (data[1])
        {
            case 0xe0:
            {
                q11k_handle_key_event(data[4]);
                return 0;
            }
            case 0xe1:
            {
                q11k_handle_gesture_event(data[4]);
                return 0;
            }
            case 0x90:
            {
                q11k_calculate_mouse_data(data, &x_pos, &y_pos);
                q11k_handle_mouse_event(x_pos, y_pos);
                return 0;
            }
            case 0x80:
            case 0x81:
            case 0x82:
            case 0x84:
            {
                q11k_calculate_pen_data(data, &x_pos, &y_pos, &pressure);
                q11k_handle_pen_event(data[1], x_pos, y_pos, pressure);
                return 0;
            }
        }
    }

    return 0;
}

static void q11k_handle_key_event(u8 b_key_raw)
{
    unsigned short keys[] = {
        KEY_RIGHTCTRL,
        KEY_RIGHTALT,
        0
    };
    int keyc = sizeof(keys) / sizeof(keys[0]);

    q11k_handle_key_mapping_event(keys, keyc, b_key_raw, q11k_mapping_keys);
}

static void q11k_handle_gesture_event(u8 b_key_raw)
{
    unsigned short keys[] = {
        // KEY_RIGHTCTRL,
        // KEY_RIGHTALT,
        0
    };
    int keyc = sizeof(keys) / sizeof(keys[0]);

    q11k_handle_key_mapping_event(keys, keyc, b_key_raw, q11k_mapping_gesture_keys);
}

static void q11k_handle_mouse_event(int x_pos, int y_pos)
{
    input_report_abs(idev_pen, ABS_X, x_pos);
    input_report_abs(idev_pen, ABS_Y, y_pos);
    input_sync(idev_pen);
}

static void q11k_handle_pen_event(u8 b_key_raw, int x_pos, int y_pos, int pressure)
{
    int rpt_x = x_pos;
    int rpt_y = y_pos;

    switch (b_key_raw)
    {
        case 0x80:
            __upress_pen();
            input_report_key(idev_pen, BTN_TOOL_PEN, 0);
            input_report_abs(idev_pen, ABS_PRESSURE, 0);
            break;
        case 0x81:
        {
            input_report_key(idev_pen, BTN_TOOL_PEN, 1);
            input_report_abs(idev_pen, ABS_PRESSURE, pressure);
            break;
        }
        case 0x82:
        {
            input_report_key(Q11K_STYLUS_KEY_DEVICE, Q11K_STYLUS_KEY_1, 1);
            Q11K_STYLUS_KEY_SYNC();
            stylus_pressed = true;
            break;
        }
        case 0x84:
        {
            input_report_key(Q11K_STYLUS_KEY_DEVICE, Q11K_STYLUS_KEY_2, 1);
            Q11K_STYLUS_KEY_SYNC();
            stylus2_pressed = true;
            break;
        }
    }

    if(q11k_relative_pen_is_enabled())
    {
        int rel_x = 0;
        int rel_y = 0;
        q11k_relative_pen_check_and_try_reset_last_abs_pos();
        q11k_relative_pen_get_rel_pos(x_pos, y_pos, &rel_x, &rel_y);

        rpt_x = rel_x + rel_pen_data.origin_x;
        rpt_y = rel_y + rel_pen_data.origin_y;

        q11k_relative_pen_limit_xy(&rpt_x, &rpt_y);
        q11k_relative_pen_update_origin(rpt_x, rpt_y);

        q11k_relative_pen_update_last_abs_pos(x_pos, y_pos);
    }

    DPRINT_DEEP("sensors: x=%08d y=%08d pressure=%08d", rpt_x, rpt_y, pressure);

    input_report_abs(idev_pen, ABS_X, rpt_x);
    input_report_abs(idev_pen, ABS_Y, rpt_y);
    input_sync(idev_pen);
}

static void q11k_handle_key_mapping_event(
    unsigned short keys[],
    int keyc,
    u8 b_key_raw,
    q11k_key_mapping_func_t kmp_func)
{
    unsigned short* rkey_p = keys + keyc - 1;
    int value = 1;
    unsigned short* last_key_p = NULL;
    unsigned short new_key = kmp_func(b_key_raw, &last_key_p);

    if (new_key == 0)
    {
        value = 0;
        new_key = *last_key_p;
    }

    if (last_key_p == &last_vkey && new_key == Q11K_VKEY_4_MOVE)
    {
        if (value != 0)
        {
            q11k_relative_pen_toggle();
            *last_key_p = new_key;
        }
        else
        {
            *last_key_p = 0;
        }
    }
    else
    {
        int t_last_key = *last_key_p;
        if (t_last_key != 0 && t_last_key != new_key && value != 0)
        {
            *rkey_p = t_last_key;
            q11k_report_keys(keyc, keys, 0);
        }

        if (new_key != KEY_UNKNOWN && new_key != 0)
        {
            *rkey_p = new_key;
            *last_key_p = new_key;
            q11k_report_keys(keyc, keys, value);
        }

        if (value == 0)
        {
            *last_key_p = 0;
        }
    }
}

static unsigned short q11k_mapping_keys(u8 b_key_raw, unsigned short** last_key_pp)
{
    *last_key_pp = &last_key;

    switch (b_key_raw)
    {
        case 0x00:
        {
            return 0;
        }
        case 0x01:
        {
            return Q11K_KEY_TOP_LEFT;
        }
        case 0x02:
        {
            return Q11K_KEY_TOP_MIDDLE;
        }
        case 0x04:
        {
            return Q11K_KEY_TOP_RIGHT;
        }
        case 0x08:
        {
            return Q11K_KEY_BOTTOM_LEFT;
        }
        case 0x10:
        {
            return Q11K_KEY_BOTTOM_MIDDLE;
        }
        case 0x20:
        {
            return Q11K_KEY_BOTTOM_RIGHT;
        }
        case 0x40:
        {
            return Q11K_KEY_6;
        }
        case 0x80:
        {
            return Q11K_KEY_7;
        }
        default:
        {
            return KEY_UNKNOWN;
        }
    }
}

static unsigned short q11k_mapping_gesture_keys(u8 b_key_raw, unsigned short** last_key_pp)
{
    *last_key_pp = &last_vkey;

    switch (b_key_raw)
    {
        case 0x00:
        {
            if (last_vkey > 0)
            {
                return 0;
            }
            else
            {
                return Q11K_VKEY_4_MOVE;
            }
        }
        case 0x01:
        {
            return Q11K_VKEY_1_CLICK;
        }
        case 0x11:
        {
            return Q11K_VKEY_2_CLICK;
        }
        case 0x12:
        {
            return Q11K_VKEY_2_LEFT;
        }
        case 0x13:
        {
            return Q11K_VKEY_2_RIGHT;
        }
        case 0x14:
        {
            return Q11K_VKEY_2_UP;
        }
        case 0x15:
        {
            return Q11K_VKEY_2_DOWN;
        }
        case 0x22:
        {
            return Q11K_VKEY_3_UP;
        }
        case 0x23:
        {
            return Q11K_VKEY_3_DOWN;
        }
        case 0x24:
        {
            return Q11K_VKEY_3_LEFT;
        }
        case 0x25:
        {
            return Q11K_VKEY_3_RIGHT;
        }
        case 0x31:
        {
            return Q11K_VKEY_4_CLICK;
        }
        default:
        {
            return KEY_UNKNOWN;
        }
    }
}

static void q11k_report_keys(const int keyc, const unsigned short* keys, int s)
{
    int i = 0;
    for (i = 0; i < keyc; ++i)
    {
        input_report_key(idev_keyboard, keys[i], s);
    }
    input_sync(idev_keyboard);
}

static void __upress_pen(void)
{
    bool stylus_changed = false;

    if (stylus_pressed)
    {
        input_report_key(Q11K_STYLUS_KEY_DEVICE, Q11K_STYLUS_KEY_1, 0);
        stylus_pressed = false;
        stylus_changed = true;
    }

    if (stylus2_pressed)
    {
        input_report_key(Q11K_STYLUS_KEY_DEVICE, Q11K_STYLUS_KEY_2, 0);
        stylus2_pressed = false;
        stylus_changed = true;
    }

    if (stylus_changed)
    {
        input_sync(Q11K_STYLUS_KEY_DEVICE);
    }
}

static void q11k_calculate_pen_data(const u8* data, int* x_pos, int* y_pos, int* pressure)
{
    *x_pos           = data[3] * 0xFF + data[2];
    *y_pos           = data[5] * 0xFF + data[4];
    *pressure        = data[7] * 0xFF + data[6];
}

static void q11k_calculate_mouse_data(const u8* data, int* x_pos, int* y_pos)
{
    *x_pos           = data[3] * 0xFF + data[2];
    *y_pos           = data[5] * 0xFF + data[4];
}

static void q11k_relative_pen_toggle(void)
{
    if (!q11k_relative_pen_is_enabled())
    {
        q11k_relative_pen_enable();
    }
    else
    {
        q11k_relative_pen_disable();
    }
}

static bool q11k_relative_pen_is_enabled(void)
{
    return rel_pen_data.enabled;
}

static void q11k_relative_pen_enable(void)
{
    q11k_relative_pen_reset_origin();
    q11k_relative_pen_reset_last_abs_pos();

    rel_pen_data.enabled = true;
}

static void q11k_relative_pen_disable(void)
{
    rel_pen_data.enabled = false;
}

static void q11k_relative_pen_reset_origin(void)
{
    int cur_x = input_abs_get_val(idev_pen, ABS_X);
    int cur_y = input_abs_get_val(idev_pen, ABS_Y);

    q11k_relative_pen_update_origin(cur_x, cur_y);
}

static void q11k_relative_pen_update_origin(int x, int y)
{
    rel_pen_data.origin_x = x;
    rel_pen_data.origin_y = y;
}

static void q11k_relative_pen_limit_xy(int* xp, int* yp)
{
    int x = *xp;
    int y = *yp;

    if (x > MAX_ABS_X)
    {
        x = MAX_ABS_X;
        *xp = x;
    }
    else if (x < 0)
    {
        x = 0;
        *xp = x;
    }

    if (y > MAX_ABS_X)
    {
        y = MAX_ABS_Y;
        *yp = y;
    }
    else if (y < 0)
    {
        y = 0;
        *yp = y;
    }
}

static void q11k_relative_pen_check_and_try_reset_last_abs_pos(void)
{
    u64 cur_jiffies = get_jiffies_64();
    u64 dj = cur_jiffies - rel_pen_data.last_jiffies;

    if (dj > REL_PEN_UP_TICK)
    {
        q11k_relative_pen_reset_last_abs_pos();
    }
}

static void q11k_relative_pen_reset_last_abs_pos(void)
{
    rel_pen_data.last_x = -1;
    rel_pen_data.last_y = -1;
    rel_pen_data.reseting_count = REL_PEN_POS_RESET_SKIP_COUNT + 1;
}

static void q11k_relative_pen_update_last_abs_pos(int x, int y)
{
    if (rel_pen_data.reseting_count > 0)
    {
        --rel_pen_data.reseting_count;
    }

    rel_pen_data.last_x = x;
    rel_pen_data.last_y = y;
    rel_pen_data.last_jiffies = get_jiffies_64();
}

static void q11k_relative_pen_get_rel_pos(int abs_x, int abs_y, int* rel_x, int* rel_y)
{
    int dx = 0;
    int dy = 0;

    if (rel_pen_data.reseting_count > 0)
    {
        dx = 0;
        dy = 0;
    }
    else
    {
        dx = abs_x - rel_pen_data.last_x;
        dy = abs_y - rel_pen_data.last_y;
    }

    *rel_x = dx / REL_PEN_DIV;
    *rel_y = dy / REL_PEN_DIV;
}

#ifdef CONFIG_PM
static int uclogic_resume(struct hid_device *hdev)
{
	return 0;
}
#endif

static void __close_keyboard(void)
{
    if (idev_keyboard != NULL)
    {
        input_unregister_device(idev_keyboard);
        input_free_device(idev_keyboard);
        idev_keyboard = NULL;
        DPRINT("Q11K keyboard unregistered");
    }
}

static void __close_pad(void)
{
    if (idev_pen != NULL)
    {
        input_unregister_device(idev_pen);
        input_free_device(idev_pen);
        idev_pen =NULL;
        DPRINT("Q11K tab unregistered");
    }
}

void q11k_remove(struct hid_device *dev)
{
    struct usb_interface *intf = to_usb_interface(dev->dev.parent);
    if (intf->cur_altsetting->desc.bInterfaceNumber == 0) {
        __close_keyboard();
    } else if (intf->cur_altsetting->desc.bInterfaceNumber == 1) {
        __close_pad();
    }
    hid_hw_close(dev);
    hid_hw_stop(dev);
}

struct wacom_features
{
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

static const struct wacom_features wfs =
	{ "Wacom Penpartner", 55662, 34789, 8192, 0, 4, 40, 40 };

static const struct hid_device_id q11k_device[] = {
    { HID_USB_DEVICE(USB_VENDOR_ID_HUION, USB_DEVICE_ID_HUION_TABLET), .driver_data = (kernel_ulong_t)&wfs },
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
