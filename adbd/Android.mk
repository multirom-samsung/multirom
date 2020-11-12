# Copyright 2005 The Android Open Source Project
#
# Android.mk for adb
#

LOCAL_PATH:= system/core/adb

-include $(LOCAL_PATH)/../platform_tools_tool_version.mk

adb_host_sanitize :=
adb_target_sanitize :=

ADB_COMMON_CFLAGS := \
    -frtti \
    -Wall -Wextra -Werror \
    -Wno-unused-parameter \
    -Wno-missing-field-initializers \
    -Wvla \
    -DADB_VERSION="\"$(tool_version)\"" \
    -DADB_REVISION='"$(adb_version)"' \
    -D_PATH_BSHELL2="\"/mrom_bin/sh\""

ADB_COMMON_posix_CFLAGS := \
    -Wexit-time-destructors \
    -Wthread-safety \

ADB_COMMON_linux_CFLAGS := \
    $(ADB_COMMON_posix_CFLAGS) \

# Define windows.h and tchar.h Unicode preprocessor symbols so that
# CreateFile(), _tfopen(), etc. map to versions that take wchar_t*, breaking the
# build if you accidentally pass char*. Fix by calling like:
#   std::wstring path_wide;
#   if (!android::base::UTF8ToWide(path_utf8, &path_wide)) { /* error handling */ }
#   CreateFileW(path_wide.c_str());

# libadb
# =========================================================

# Much of adb is duplicated in bootable/recovery/minadb and fastboot. Changes
# made to adb rarely get ported to the other two, so the trees have diverged a
# bit. We'd like to stop this because it is a maintenance nightmare, but the
# divergence makes this difficult to do all at once. For now, we will start
# small by moving common files into a static library. Hopefully some day we can
# get enough of adb in here that we no longer need minadb. https://b/17626262
LIBADB_SRC_FILES := \
    adb.cpp \
    adb_io.cpp \
    adb_listeners.cpp \
    adb_utils.cpp \
    fdevent.cpp \
    sockets.cpp \
    transport.cpp \
    transport_local.cpp \
    transport_usb.cpp \

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?),0)
LIBADB_SRC_FILES += \
    adb_trace.cpp \
    socket_spec.cpp \
    sysdeps/errno.cpp
else
LIBADB_SRC_FILES += \
    adb_auth.cpp
endif

LIBADB_CFLAGS := \
    $(ADB_COMMON_CFLAGS) \
    -fvisibility=hidden \

include $(CLEAR_VARS)
LOCAL_CLANG := true
LOCAL_MODULE := mrom_libadbd
LOCAL_CFLAGS := $(LIBADB_CFLAGS) -DADB_HOST=0
LOCAL_SRC_FILES := \
    $(LIBADB_SRC_FILES) \
    jdwp_service.cpp \

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?),0)
LOCAL_SRC_FILES += \
    adbd_auth.cpp \
    sysdeps/posix/network.cpp
else
LOCAL_SRC_FILES += \
    adb_auth_client.cpp \
    fdevent.cpp \
    qemu_tracing.cpp \
    usb_linux_client.cpp
endif

LOCAL_SANITIZE := $(adb_target_sanitize)

# Even though we're building a static library (and thus there's no link step for
# this to take effect), this adds the includes to our path.
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?),0)
LOCAL_STATIC_LIBRARIES := libcrypto_utils libcrypto libqemu_pipe libbase
LOCAL_WHOLE_STATIC_LIBRARIES := libadbd_usb
else
LOCAL_SHARED_LIBRARIES := libbase
endif

include $(BUILD_STATIC_LIBRARY)

$(call dist-for-goals,dist_files sdk win_sdk,$(LOCAL_BUILT_MODULE))
ifdef HOST_CROSS_OS
# Archive adb.exe for win_sdk build.
$(call dist-for-goals,win_sdk,$(ALL_MODULES.host_cross_adb.BUILT))
endif


# adbd device daemon
# =========================================================

include $(CLEAR_VARS)

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?),0)
LOCAL_SRC_FILES := \
    daemon/main.cpp \
    daemon/mdns.cpp \
    shell_service.cpp \
    shell_service_protocol.cpp
else
LOCAL_CLANG := true

LOCAL_SRC_FILES := \
    adb_main.cpp
endif

LOCAL_SRC_FILES += \
    services.cpp \
    file_sync_service.cpp \
    framebuffer_service.cpp \
    remount_service.cpp \
    set_verity_enable_state_service.cpp \

LOCAL_CFLAGS := \
    $(ADB_COMMON_CFLAGS) \
    -DADB_HOST=0 \
    -D_GNU_SOURCE \
    -Wno-deprecated-declarations \

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?),0)
LOCAL_CFLAGS += \
    $(ADB_COMMON_linux_CFLAGS)
endif

LOCAL_CFLAGS += -DALLOW_ADBD_NO_AUTH=$(if $(filter userdebug eng,$(TARGET_BUILD_VARIANT)),1,0)

ifneq (,$(filter userdebug eng,$(TARGET_BUILD_VARIANT)))
LOCAL_CFLAGS += -DALLOW_ADBD_DISABLE_VERITY=1
LOCAL_CFLAGS += -DALLOW_ADBD_ROOT=1
endif

LOCAL_MODULE := mrom_adbd

LOCAL_FORCE_STATIC_EXECUTABLE := true
LOCAL_MODULE_PATH := $(TARGET_OUT_OPTIONAL_EXECUTABLES)

LOCAL_SANITIZE := $(adb_target_sanitize)
LOCAL_STRIP_MODULE := keep_symbols
LOCAL_STATIC_LIBRARIES := \
    mrom_libadbd \
    libbase \
    libfs_mgr \
    libselinux \
    liblog \

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 28; echo $$?),0)
LOCAL_STATIC_LIBRARIES += \
    libasyncio
endif

ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 24; echo $$?),0)
LOCAL_STATIC_LIBRARIES += \
    libavb_user \
    libqemu_pipe \
    libbootloader_message \
    libfec \
    libfec_rs \
    libext4_utils \
    libsquashfs_utils \
    libcutils \
    libcrypto_utils \
    libcrypto \
    libminijail \
    libmdnssd \
    libdebuggerd_handler
else
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_SBIN_UNSTRIPPED)
LOCAL_C_INCLUDES += system/extras/ext4_utils

LOCAL_STATIC_LIBRARIES += \
    libcutils \
    libc \
    libext4_utils_static \
    libmincrypt
endif

include $(BUILD_EXECUTABLE)

include $(call first-makefiles-under,$(LOCAL_PATH))
