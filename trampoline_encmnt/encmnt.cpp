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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>

extern "C" {
#include "../lib/log.h"
#include "../lib/fstab.h"
#include "../lib/framebuffer.h"
#include "../lib/util.h"
}

#include "crypto/lollipop/cryptfs.h"
#include "crypto/ext4crypt/Decrypt.h"

#include "pw_ui.h"
#include "encmnt_defines.h"

#define CMD_NONE 0
#define CMD_DECRYPT 1
#define CMD_DECRYPTFBE 2
#define CMD_REMOVE 3
#define CMD_PWTYPE 4

#define FSTAB_FLAGS "flags="

static int get_footer_from_opts(char *output, size_t output_size, const char *options)
{
    char *r, *saveptr;
    char *dup;
    int res = -1;
    int i;

    if (strstr(options, FSTAB_FLAGS) != NULL) {
        dup = strdup(options + strlen(FSTAB_FLAGS));
        r = strtok_r(dup, ";", &saveptr);
    } else {
        dup = strdup(options);
        r = strtok_r(dup, ",", &saveptr);
    }

    static const char *names[] = {
        "encryptable=",
        "forceencrypt=",
        "forcefdeorfbe=",
        NULL
    };

    while(r)
    {
        for(i = 0; names[i]; ++i)
        {
            if(strstartswith(r, names[i]))
            {
                snprintf(output, output_size, "%s", r + strlen(names[i]));
                res = 0;
                goto exit;
            }
        }

        r = strtok_r(NULL, ",", &saveptr);
    }

exit:
    free(dup);
    return res;
}

static void print_help(char *argv[]) {
    printf("Usage: %s COMMAND ARGUMENTS\n"
        "Available commands:\n"
        "     decrypt PASSWORD - decrypt data using PASSWORD.\n"
        "             Prints out dm block device path on success.\n"
        "     remove - unmounts encrypted data\n"
        "     pwtype - prints password type as integer\n",
        argv[0]);
}

static int handle_pwtype(int stdout_fd)
{
    if(cryptfs_check_footer() < 0)
    {
        ERROR("cryptfs_check_footer failed!");
        return -1;
    }

    int pwtype = cryptfs_get_password_type();
    if(pwtype < 0)
    {
        ERROR("cryptfs_get_password_type failed!");
        return -1;
    }

    char buff[32];
    snprintf(buff, sizeof(buff), "%d\n", pwtype);
    write(stdout_fd, buff, strlen(buff));
    fsync(stdout_fd);
    return 0;
}

static int handle_decryptfbe(int stdout_fd, char *password)
{
    int retry_count = 3;
    static const char *default_password = "!";
    int pwtype = -1;
    //property_set("ro.crypto.state", "encrypted");
    //property_set("ro.crypto.type", "file");
    //INFO("return %d\n", ret);
    while (!Decrypt_DE() && --retry_count)
        usleep(2000);
    if (retry_count > 0) {
        std::string filename;
        pwtype = Get_Password_Type(0, filename);
        ERROR("Password type is %d %s\n", pwtype, filename.c_str());
        if (pwtype < 0) {
            ERROR("This TWRP does not have synthetic password decrypt support\n");
            pwtype = 0; // default password
        }
    }

    if(pwtype < 0)
    {
        ERROR("get_password_type failed!");
        return -1;
    }
    else if (pwtype == CRYPT_TYPE_DEFAULT)
        password = (char*)default_password;

    if (password) {
        if (Decrypt_User(0, password)) {
            return 0;
        }
    } else
    {
        switch(pw_ui_run(pwtype, true))
        {
            default:
            case ENCMNT_UIRES_ERROR:
                ERROR("pw_ui_run() failed!\n");
                return -1;
            case ENCMNT_UIRES_BOOT_INTERNAL:
                INFO("Wants to boot internal!\n");
                write(stdout_fd, ENCMNT_BOOT_INTERNAL_OUTPUT, strlen(ENCMNT_BOOT_INTERNAL_OUTPUT));
                fsync(stdout_fd);
                return 0;
            case ENCMNT_UIRES_BOOT_RECOVERY:
                INFO("Wants to boot recoveryl!\n");
                write(stdout_fd, ENCMNT_BOOT_RECOVERY_OUTPUT, strlen(ENCMNT_BOOT_RECOVERY_OUTPUT));
                fsync(stdout_fd);
                return 0;
            case ENCMNT_UIRES_PASS_OK:
                return 0;
        }
    }

    return -1;
}

static int handle_decrypt(int stdout_fd, char *password)
{
    DIR *d;
    struct dirent *de;
    char buff[256];
    int res = -1;
    static const char *default_password = "default_password";

    if(cryptfs_check_footer() < 0)
    {
        ERROR("cryptfs_check_footer failed!");
        return -1;
    }

    int pwtype = cryptfs_get_password_type();
    if(pwtype < 0)
    {
        ERROR("cryptfs_get_password_type failed!");
        return -1;
    }
    else if (pwtype == CRYPT_TYPE_DEFAULT)
        password = (char*)default_password;

    if(password)
    {
        if(cryptfs_check_passwd(password) < 0)
        {
            ERROR("cryptfs_check_passwd failed!");
            return -1;
        }
    }
    else
    {
        switch(pw_ui_run(pwtype, false))
        {
            default:
            case ENCMNT_UIRES_ERROR:
                ERROR("pw_ui_run() failed!\n");
                return -1;
            case ENCMNT_UIRES_BOOT_INTERNAL:
                INFO("Wants to boot internal!\n");
                write(stdout_fd, ENCMNT_BOOT_INTERNAL_OUTPUT, strlen(ENCMNT_BOOT_INTERNAL_OUTPUT));
                fsync(stdout_fd);
                return 0;
            case ENCMNT_UIRES_BOOT_RECOVERY:
                INFO("Wants to boot recoveryl!\n");
                write(stdout_fd, ENCMNT_BOOT_RECOVERY_OUTPUT, strlen(ENCMNT_BOOT_RECOVERY_OUTPUT));
                fsync(stdout_fd);
                return 0;
            case ENCMNT_UIRES_PASS_OK:
                break;
        }
    }

    INFO("open block device\n");
    d = opendir("/dev/block/");
    if(!d)
    {
        ERROR("Failed to open /dev/block, wth? %s", strerror(errno));
        return -1;
    }

    INFO("finding block device\n");
    if (access("/dev/block/dm-0", R_OK)) {
        INFO("/dev/block/dm-0 nhi hai\n");
    } else {
        INFO("/dev/block/dm-0 hai\n");
    }
    // find the block device
    while((de = readdir(d)))
    {
        INFO("finding block device %d %s\n", de->d_type, de->d_name);
        if(de->d_type == DT_BLK && !strncmp(de->d_name, "dm-", 3))
        {
            snprintf(buff, sizeof(buff), "/dev/block/%s\n", de->d_name);
            INFO("Found block device %s\n", buff);
            char temp[512];
            read(stdout_fd, temp, 512);
                INFO("ye tha stdout me %s\n", temp);
            write(stdout_fd, buff, strlen(buff));
            fsync(stdout_fd);
            res = 0;
            break;
        }
    }
    /*snprintf(buff, sizeof(buff), "/dev/block/dm-0\n");
    write(stdout_fd, buff, strlen(buff));
    fsync(stdout_fd);
    res = 0;*/

    closedir(d);
    return res;
}

static int handle_remove(void)
{
    if(delete_crypto_blk_dev("userdata") < 0)
    {
        ERROR("delete_crypto_blk_dev failed!");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int i;
    int res = 1;
    int cmd = CMD_NONE;
    int stdout_fd;
    char footer_location[256];
    int found_footer_location;
    struct fstab *fstab;
    struct fstab_part *p;
    char *argument = NULL;

    // output all messages to dmesg,
    // but it is possible to filter out INFO messages
    klog_set_level(6);

    mrom_set_log_tag("trampoline_encmnt");
    mrom_set_dir("/mrom_enc/");

    for(i = 1; i < argc; ++i)
    {
        if(!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
        {
            print_help(argv);
            return 0;
        }
        else if(cmd == CMD_NONE)
        {
            if(strcmp(argv[i], "decryptfbe") == 0)
                cmd = CMD_DECRYPTFBE;
            else if(strcmp(argv[i], "decrypt") == 0)
                cmd = CMD_DECRYPT;
            else if(strcmp(argv[i], "remove") == 0)
                cmd = CMD_REMOVE;
            else if(strcmp(argv[i], "pwtype") == 0)
                cmd = CMD_PWTYPE;
        }
        else if(!argument)
        {
            argument = argv[i];
        }
    }

    if(argc == 1 || cmd == CMD_NONE)
    {
        print_help(argv);
        return 0;
    }

    if (cmd == CMD_DECRYPT) {
        fstab = fstab_auto_load();
        if(!fstab)
        {
            ERROR("Failed to load fstab!");
            return 1;
        }

        p = fstab_find_first_by_path(fstab, "/data");
        if(!p)
        {
            ERROR("Failed to find /data partition in fstab\n");
            goto exit;
        }

        found_footer_location = 0;

        if(p->options)
            found_footer_location = get_footer_from_opts(footer_location, sizeof(footer_location), p->options) == 0;

        if(!found_footer_location && p->options2)
            found_footer_location = get_footer_from_opts(footer_location, sizeof(footer_location), p->options2) == 0;

        if(!found_footer_location)
        {
            ERROR("Failed to find footer location\n");
            goto exit;
        }

        INFO("Setting encrypted partition data to %s %s %s\n", p->device, footer_location, p->type);
        set_partition_data(p->device, footer_location, p->type);

        fstab_destroy(fstab);
    }
    //cryptfs prints informations, we don't want that
    stdout_fd = dup(1);
    freopen("/dev/null", "ae", stdout);
    freopen("/dev/null", "ae", stderr);

    switch(cmd)
    {
        case CMD_PWTYPE:
            if(handle_pwtype(stdout_fd) < 0)
                goto exit;
            break;
        case CMD_DECRYPT:
            if(handle_decrypt(stdout_fd, argument) < 0)
                goto exit;
            break;
        case CMD_DECRYPTFBE:
            if(handle_decryptfbe(stdout_fd, argument) < 0)
                goto exit;
            break;
        case CMD_REMOVE:
            if(handle_remove() < 0)
                goto exit;
            break;
    }

    res = 0;
exit:
    return res;
}
