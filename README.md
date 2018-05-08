# multirom
MultiROM is a one-of-a-kind multi-boot solution. It can boot android ROM while
keeping the one in internal memory intact or boot Ubuntu without formating
the whole device. MultiROM can boot either from internal memory of the device
or from USB flash drive.

XDA threads:
* klte: https://forum.xda-developers.com/galaxy-s5/unified-development/mod-multirom-v33b-twrp-3-1-0-0-20170401-t3582579

### Sources
MultiROM:

    git clone --recursive https://github.com/multirom-klte/multirom.git system/extras/multirom

It also needs libbootimg:

    git clone https://github.com/multirom-klte/libbootimg.git system/extras/libbootimg

TWRP-MultiROM recovery:

    git clone https://github.com/multirom-klte/android_bootable_recovery bootable/recovery

### Build
I currently use OmniROM tree for building (means branch android-6.0 in device repos).

Use something like this to build:

```sh
. build/envsetup.sh
lunch omni_klte-eng
make -j4 recoveryimage 
make -j4 multirom_zip 
make -j4 multirom_uninstaller
```
