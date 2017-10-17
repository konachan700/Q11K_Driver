# Q11K_Driver
Huion Q11K Driver

Beta version, only for 4.5.*+ kernel versions

```make & make install```

# Attention!
It driver conflict with module "uclogic", please rmmod uclogic before load this module.

# Keys 
Keys hardcoded as CTRL+[0-9], 0-7 for tablet keys and 8-9 for stylus. <br>
Stylus has hardware bug and not sent a keycode when pressure not null. 
