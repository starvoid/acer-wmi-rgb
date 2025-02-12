# Gaming RGB keyboard backlight for Acer laptops (Unofficial)
Non official kernel module for managing keyboard backlight color


## Porpouse
The objective of this project is to create an interface from Linux to change the keyboard backlight color on Acer laptops with this capacity.

Most of the hard work was taken from here: https://github.com/JafarAkhondali/acer-predator-turbo-and-rgb-keyboard-linux-module.git.

All the reverse engineering needed as the basic idea of creating a kernel module exposing a user space device have been copied from there.

This approach is based on keeping untouched the "official" acer_wmi module of the kernel. I have no intention to upload this code to the linux kernel mainstream.

This independent module will only deal with keyboard backlight feature and you can upload it to memory and remove it without interacts with any other module.

The aim of this develop is also keep it simple, no additional services or python based translators are needed, just this kernel module.

This repo makes sense until this functionality will be included in other modules in the kernel mainstream.

<font color=red>**Warning**
#### Use at your own risk! This project runs a kernel module talking with acpi subsystem using WMI.</font>

It has been succesfully tested in an Acer Nitro AN517-41, but probably it will work on any device listed in the compatibility table posted in: https://github.com/JafarAkhondali/acer-predator-turbo-and-rgb-keyboard-linux-module.git


## License
GNU General Public License v3


## Installation modes
You can use the module in three ways.

1.- Upload to the memory now, and play with it until the next reboot or until you remove from memory.

1.1.- To upload to memory: `./module.sh load`

1.2.- To remove from memory: `./module.sh unload`

2.- Install the module to be upload automatically at boot time. You can play with it until you install a new version of the kernel or until you uninstall it.

2.1.- To install to the loading modules system at boot: `./module.sh install`

2.2.- To remove from the loading modules system at boot: `./module.sh uninstall`

3.- Install the module under the subsystem of dkms to be recompiled in a kernel version change and set it to be upload automatically at boot time. You can play with it until you uninstall it.

3.1.- To install to the dkms system: `./module.sh dkms_install`

3.2.- To remove from dkms system: `./module.sh dkms_uninstall`

## Using device /dev/acer-kb-rgb-0.
Once the module is loaded into memory, a char device is created in user space and it can be used to send the keyboard backlight configuration. The device will be **/dev/acer-kb-rgb-0**. There are a lot of ways to send chars to a device, one of the most easier is using echo command line. For example, ina terminal, type: `echo 'm3' > /dev/acer-kb-rgb-0`.

## Configuration syntax
This syntax is the same to:
- Sending commands to the device (acer-kb-rgb-0):
    - > echo **m3 b80** > /dev/acer-kb-rgb-0
- Setting default initial string (calling module.sh):
    - > ./module.sh install **'m3 b80'**

The basic syntax is a command followed by one or more values. The complete command string could be always placed into quotation marks, it is not needed using echo command but it is needed using the shell script. The values are always bytes, that is, unsigned integers from 0 to 255.

The [commands](#the-valid-commands-are) are a single char. The values could be written in base 10,16, or 8. 

The spaces between commands and its first value and between different commands are ignored, but they are needed to separate values belonging to the same command, for example, setting the three color components.

The next commands are the same:
```
m3t55
m3 t0x37
m 3 t x67
```

The color related commands have three vales (color components) always in the sequence of: R G B. For example:
```
c 128 250 63
c 89 0xA5 x120
```

More than a command can be send in the same string. The order doesn't matter. For example, the nest two commands have the same effect:
```
m 2 c 128 250 63
c 128 250 63 m 2
```

### The valid commands are:
```
'm': Mode [0..5]
'v': Velocity [0..9]
't': Brightness [0..100]
'd': Direction [1..2]
'c': Color [0..255] [0..255] [0..255]
'z': Zone [1..4] [0..255] [0..255] [0..255]
```
#### The mode command values meaning is:

```
Mode 0: Static color. It uses zone's colors.
Mode 1: Breath. It uses velocity and color.
Mode 2: Neon. It uses velocity.
Mode 3: Wave. It uses velocity 
Mode 4: Shifting. It uses color, direction and velocity.
Mode 5: Zoom. It uses color and velocity.

All the modes uses brightness.
```
### Some examples:

`m0 b90` -> Set mode to static, set brightness to 90 and keeps the current color scheme.

`m0 z1 0xFF 0xFA 0x00 z2 0x00 0xAA 0x66 z3 0x99 0x99 0xFF z4 0x99 0x00 0xAA` -> Set mode to static and set colors for each zone, but keeps current brightness.

`m1 v2 b90 c 255 0 0`-> Set mode to breath, set velocity to 2, set brightness to 90 and set color to red.

`m1 v6 b50 c 0 255 0`-> Set mode to breath, set velocity to 6, set brightness to 50 and set color to green.

`m2`-> Set mode to Neon, keeps velocity and brightness.

`m2 c 255 0 0 v2 b 20` -> Set mode to Neon, set color red, set velocity to 2 and brightness to 20.

`m3 v3` -> Set mode to wave, set velocity to 2.

`m3` -> Set mode to wave and keeps the current color scheme.

`m4 c 255 255 0 d 2 b20` -> Set mode to Shifting, set color yellow, set direction right to left, set brightness to 20. Keeps current velocity.

`m4 v 4` -> Set mode to Shifting, set velocity to 4, keeps color, direction, brightness.

`m5 c 255 0 2` -> Set mode to Zoom, set color pink, keeps brightness and velocity.

`m5 c 0 255 255 b80 v 4` -> Set mode to Zoom, set color green-blue, set brightness to 80 set velocity to 4

A mode has to be set always, if not, mode 0 will be understood, that is, the command: `b90` will set mode to 0 (static) and set brightness to 90 also.

### Default settings
The module is able to restore the default settings (mode, color schema, brightness, etc) configuration in each boot (including resuming from hibernation), despite the current settings modified sending string to /dev/acer-kb-rgb-0.

This default configuration is set when the module is installed using module.sh with the commands install or install_dkms.

Once installed, to change the default (and current) settings you don't need to re-install the module (but you can), simply call `# module.sh default new_default_string_command`.
___

