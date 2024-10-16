/*
 * This file is part of MultiROM.
 *
 * MultiROM is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * MultiROM is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with MultiROM.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <sys/mount.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "../lib/fstab.h"
#include "../lib/util.h"
#include "../lib/log.h"
#include "encryption.h"
#include "../trampoline_encmnt/encmnt_defines.h"
#include "../hooks.h"

static char encmnt_cmd_arg[64] = { 0 };
static char *const encmnt_cmd[] = { "/mrom_enc/trampoline_encmnt", encmnt_cmd_arg, NULL };
#ifdef MR_ENCRYPTION_FAKE_PROPERTIES
static char *const encmnt_envp[] = { "LD_LIBRARY_PATH=/mrom_enc/", "LD_PRELOAD=/mrom_enc/libmultirom_fake_properties.so /mrom_enc/libmultirom_fake_propertywait.so", NULL };
#else
static char *const encmnt_envp[] = { "LD_LIBRARY_PATH=/mrom_enc/", NULL };
#endif
static int g_decrypted = 0;

int encryption_before_mount(__unused struct fstab *fstab)
{
    int exit_code = -1;
    char *output = NULL, *itr;
    int res = ENC_RES_ERR;
    struct stat stat;

#ifdef __LP64__
    chmod("/mrom_enc/linker64", 0775);
#else
    chmod("/mrom_enc/linker", 0775);
#endif
    chmod("/mrom_enc/trampoline_encmnt", 0775);
    // some fonts not in ramdisk to save space, so use regular instead
    symlink("/mrom_enc/res/Roboto-Regular.ttf", "/mrom_enc/res/Roboto-Italic.ttf");
    symlink("/mrom_enc/res/Roboto-Regular.ttf", "/mrom_enc/res/Roboto-Medium.ttf");

    if (lstat("/vendor", &stat) == 0) {
        rename("/vendor", "/vendor_boot");
    }
    symlink("/mrom_enc/vendor", "/vendor");

    if (access("/firmware", F_OK) >= 0) {
        rename("/firmware", "/firmware_boot");
    }
    mkdir("/firmware", 0775);
    struct fstab_part *fwpart = fstab_find_first_by_path(fstab, "/firmware");
    if(fwpart && (strcmp(fwpart->type, "emmc") != 0 || strcmp(fwpart->type, "vfat") != 0))
    {
        if(mount(fwpart->device, "/firmware", fwpart->type, fwpart->mountflags, NULL) < 0)
            ERROR("Mounting /firmware for encryption failed with %s\n", strerror(errno));
    }

#if MR_DEVICE_HOOKS >= 6
    tramp_hook_encryption_setup();
#endif

    INFO("Running trampoline_encmnt\n");

    strcpy(encmnt_cmd_arg, "decrypt");
    output = run_get_stdout_with_exit_with_env(encmnt_cmd, &exit_code, encmnt_envp);
    if(exit_code != 0 || !output)
    {
        ERROR("Failed to run trampoline_encmnt, exit code %d: %s\n", exit_code, output);
        goto exit;
    }

    itr = output + strlen(output) - 1;
    while(itr >= output && isspace(*itr))
        *itr-- = 0;

    if(strcmp(output, ENCMNT_BOOT_INTERNAL_OUTPUT) == 0)
    {
        INFO("trampoline_encmnt requested to boot internal ROM.\n");
        res = ENC_RES_BOOT_INTERNAL;
        goto exit;
    }

    if(strcmp(output, ENCMNT_BOOT_RECOVERY_OUTPUT) == 0)
    {
        INFO("trampoline_encmnt requested to boot recovery.\n");
        res = ENC_RES_BOOT_RECOVERY;
        goto exit;
    }

    if(!strstartswith(output, "/dev"))
    {
        ERROR("Invalid trampoline_encmnt output: %s\n", output);
        goto exit;
    }

    g_decrypted = 1;

    struct fstab_part *datap = fstab_find_first_by_path(fstab, "/data");
    if(!datap)
    {
        ERROR("Failed to find /data in fstab!\n");
        goto exit;
    }

    INFO("Updating device %s to %s in fstab due to encryption.\n", datap->device, output);
    fstab_update_device(fstab, datap->device, output);

    res = ENC_RES_OK;
exit:
    free(output);
    return res;
}

void encryption_destroy(void)
{
    int res = -1;
    int exit_code = -1;
    char *output = NULL;
    struct stat info;

    if(g_decrypted)
    {
        strcpy(encmnt_cmd_arg, "remove");
        output = run_get_stdout_with_exit_with_env(encmnt_cmd, &exit_code, encmnt_envp);
        if(exit_code != 0)
            ERROR("Failed to run trampoline_encmnt: %s\n", output);
        g_decrypted = 0;
        free(output);
    }
}

int encryption_cleanup(void)
{
    struct stat stat;
#if MR_DEVICE_HOOKS >= 6
    tramp_hook_encryption_cleanup();
#endif
    if (lstat("/vendor_boot", &stat) == 0) {
        remove("/vendor");
        rename("/vendor_boot", "/vendor");
    }

    if(access("/firmware", R_OK) >= 0 && umount("/firmware") < 0)
        ERROR("encryption_cleanup: failed to unmount /firmware: %s\n", strerror(errno));

    rmdir("/firmware");
    if (access("/firmware_boot", F_OK) >= 0) {
        rename("/firmware_boot", "/firmware");
    }
    return 0;
}
