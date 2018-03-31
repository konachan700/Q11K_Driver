# Q11K_Driver
Huion Q11K Driver

Beta version, only for 4.5.*+ kernel versions

```make & make install```

# Attention!
- This driver conflict with module "uclogic", please rmmod uclogic before load this module.
- If you are use a 4.14.+ vanilla kenel, please, add vid&pid to list "hid_have_special_driver" in "drivers/hid/hid-core.c". Without it action driver was confilct with hid-generic.

# Keys 
Keys hardcoded as CTRL+[0-7] for tablet keys and BUTTON_MIDDLE & BUTTON_RIGHT for stylus. <br>
Stylus has hardware bug and not sent a keycode when pressure not null. 
