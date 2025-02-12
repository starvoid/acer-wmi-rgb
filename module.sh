#!/bin/bash

mod_name=acer_wmi_rgb
mod_version=1.0
inst_folder=/lib/modules/$(uname -r)/kernel/drivers/
dkms_install_folder=/lib/modules/$(uname -r)/updates/dkms/
module_config_file=$mod_name.conf
dir=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

#key=/var/lib/shim-signed/mok/MOK.priv
#cer=/var/lib/shim-signed/mok/MOK.der

#key=/var/lib/dkms/mok.key
#cer=/var/lib/dkms/mok.pub
#sign=/usr/lib/linux-kbuild-6.1/scripts/sign-file

if [[ -f "/sys/bus/wmi/devices/7A4DDFE7-5B5D-40B4-8595-4408E0CC7F56/" ]]; then
    echo "Sorry but your device doesn't have the required WMI module"
    exit 1
fi

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root"
   exit 1
fi

clean() {
    curr=`pwd`
    cd $dir #/src
    make clean-all
    cd $curr
}

compile() {
    if [ ! -e src/$mod_name.ko ]; then
        curr=`pwd`
        cd $dir #/src
        make
        make clean
        cd $curr
    fi
}

unload() {
    res=`lsmod | grep $mod_name`
    if [ -n "$res" ]; then
        rmmod $mod_name
    fi
}

load() {
    (unload)
    (compile)
    if [ "$?" -ne "0" ] ; then
        return $?
    fi    
    insmod $dir/src/$mod_name.ko init_conf="$*"
    (clean)
}


set_init_string() {
    if [ ! -z "$*" ]; then
        echo "Setting init string: $*"
        echo "options $mod_name init_conf=\"$*\"" > /etc/modprobe.d/$module_config_file
    fi
}

configure() {
    echo $mod_name > /etc/modules-load.d/$module_config_file
    (set_init_string $*)
    depmod
    modprobe $mod_name init_conf="$*"
}

deconfigure() {
    if [ -e /etc/modules-load.d/$module_config_file ]; then
        rm /etc/modules-load.d/$module_config_file
    fi
    if [ -e /etc/modprobe.d/$module_config_file ]; then
        rm /etc/modprobe.d/$module_config_file
    fi
    depmod
}

set_default() {
    echo "Setting default: $*"
    if [ -e /etc/modules-load.d/$module_config_file ]; then
        echo $* > /dev/acer-kb-rgb-0
        (set_init_string $*)
    fi
}

uninstall() {
    (unload)
    if [ -e $inst_folder$mod_name.ko ]; then
        rm $inst_folder$mod_name.ko
    fi
    (deconfigure)
}

install() {
    (unload)
    (compile)
    cp $dir/src/$mod_name.ko $inst_folder
    (clean)
    (configure $*)
}

dkms_uninstall() {
    (unload)
    dkms uninstall -m $mod_name -v $mod_version
    dkms remove -m $mod_name -v $mod_version
    (deconfigure)
    rm -rf /usr/src/$mod_name-$mod_version 
}

dkms_install() {
    mkdir -p /usr/src/$mod_name-$mod_version/src
    cp $dir/dkms.conf $dir/Makefile /usr/src/$mod_name-$mod_version/
    cp $dir/src/$mod_name.c /usr/src/$mod_name-$mod_version/src/   
    dkms add -m $mod_name -v $mod_version
    dkms build -m $mod_name -v $mod_version --config ./.config
    dkms install -m $mod_name -v $mod_version
    (configure $*)
}


if [ "$1" == "load" ]; then
    load $2
elif [ "$1" == "unload" ]; then    
    unload
elif [ "$1" == "install" ]; then
    install $2
elif [ "$1" == "uninstall" ]; then
    uninstall
elif [ "$1" == "dkms_install" ]; then
    dkms_install $2
elif [ "$1" == "dkms_uninstall" ]; then
    dkms_uninstall
elif [ "$1" == "default" ]; then
    set_default $2
else 
    echo "Usage: module load|unload|install|uninstall|dkms_install|dkms_uninstall|default ['init-mode-string']"
    echo "    load only loads the module to memory and the user space device can be used until power-off."
    echo "    unload undoes the load's work."
    echo "    install installs the module and the device can be used from now and after reboots until a new kernel is installed or you de-install it."
    echo "    uninstall undoes the install's work."
    echo "    dkms_install installs the source code of the module as part of the dkms system, it will be automatically compiled in a kernel update and the device can be used from now until you de-install it."
    echo "    dkms_uninstall undoes the dkms_install's work."
    echo "    default changes the default mode and color scheme for install and dkms_install modes."
    echo "        The optional parameter init-mode-string is the initial mode and color scheme configuration. It's used only along with the commands load, install, dkms_install and default."
    echo "        It has to be a string with the same syntax as any valid string send to /dev/acer_wmi_rgb but must be set into quotation marks"
fi