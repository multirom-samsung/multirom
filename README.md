# multirom
MultiROM is a one-of-a-kind multi-boot solution. It can boot android ROM while
keeping the one in internal memory intact or boot Ubuntu without formating
the whole device. MultiROM can boot either from internal memory of the device
or from USB flash drive.

### Sources
MultiROM:

    git clone --recursive https://github.com/multirom-samsung/multirom.git system/extras/multirom

It also needs libbootimg:

    git clone https://github.com/multirom-samsung/libbootimg.git system/extras/libbootimg

TWRP-MultiROM recovery:

    git clone https://github.com/multirom-samsung/android_bootable_recovery bootable/recovery

### Build
I currently using https://github.com/minimal-manifest-twrp/platform_manifest_twrp_omni tree for building.

Use something like this to build:

```sh
export ALLOW_MISSING_DEPENDENCIES=true
. build/envsetup.sh
lunch omni_<device>-eng
mka recoveryimage
make multirom_zip
make multirom_uninstaller
```
