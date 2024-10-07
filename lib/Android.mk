LOCAL_PATH:= $(call my-dir)

common_SRC_FILES := \
    animation.c \
    button.c \
    colors.c \
    containers.c \
    framebuffer.c \
    framebuffer_drm.c \
    framebuffer_fbdev.c \
    framebuffer_generic.c \
    framebuffer_png.c \
    framebuffer_truetype.c \
    fstab.c \
    inject.c \
    input.c \
    listview.c \
    keyboard.c \
    mrom_data.c \
    notification_card.c \
    progressdots.c \
    tabview.c \
    touch_tracker.c \
    util.c \
    workers.c \
    klog.c \

common_C_INCLUDES := $(multirom_local_path)/lib \
    external/libpng \
    external/zlib \
    external/freetype/include \
    system/extras/libbootimg/include \
    external/libdrm \
    external/libdrm/include/drm

# With these, GCC optimizes aggressively enough so full-screen alpha blending
# is quick enough to be done in an animation
common_C_FLAGS := -O3 -funsafe-math-optimizations

ifeq ($(MR_INPUT_TYPE),)
    MR_INPUT_TYPE := type_b
endif
ifeq ($(MR_CUSTOM_INPUT_TYPE),)
    common_SRC_FILES += input_$(MR_INPUT_TYPE).c
else
    common_SRC_FILES += ../../../../$(MR_CUSTOM_INPUT_TYPE)
endif

ifeq ($(MR_USE_QCOM_OVERLAY),true)
    common_C_FLAGS += -DMR_USE_QCOM_OVERLAY
    common_SRC_FILES += framebuffer_qcom_overlay.c
ifneq ($(MR_QCOM_OVERLAY_HEADER),)
    common_C_FLAGS += -DMR_QCOM_OVERLAY_HEADER=\"../../../../$(MR_QCOM_OVERLAY_HEADER)\"
else
    $(error MR_USE_QCOM_OVERLAY is true but MR_QCOM_OVERLAY_HEADER was not specified!)
endif
ifneq ($(MR_QCOM_OVERLAY_CUSTOM_PIXEL_FORMAT),)
    common_C_FLAGS += -DMR_QCOM_OVERLAY_CUSTOM_PIXEL_FORMAT=$(MR_QCOM_OVERLAY_CUSTOM_PIXEL_FORMAT)
endif
ifeq ($(MR_QCOM_OVERLAY_USE_VSYNC),true)
    common_C_FLAGS += -DMR_QCOM_OVERLAY_USE_VSYNC
endif
endif

include $(CLEAR_VARS)

LOCAL_MODULE := libmultirom_static
LOCAL_MODULE_TAGS := optional
LOCAL_MODULE_PATH := $(TARGET_ROOT_OUT)
LOCAL_UNSTRIPPED_PATH := $(TARGET_ROOT_OUT_UNSTRIPPED)
LOCAL_CFLAGS += $(common_C_FLAGS)
LOCAL_C_INCLUDES += $(common_C_INCLUDES)
LOCAL_WHOLE_STATIC_LIBRARIES := libdrm
LOCAL_SRC_FILES := $(common_SRC_FILES)

MR_NO_KEXEC_MK_OPTIONS := true 1 allowed 2 enabled 3 ui_confirm 4 ui_choice 5 forced
ifneq (,$(filter $(MR_NO_KEXEC), $(MR_NO_KEXEC_MK_OPTIONS)))
    # clone libbootimg to /system/extras/ from
    # https://github.com/Tasssadar/libbootimg.git
    LOCAL_STATIC_LIBRARIES += libbootimg
    LOCAL_C_INCLUDES += system/extras/libbootimg/include
    LOCAL_SRC_FILES += ../no_kexec.c
endif

include $(multirom_local_path)/device_defines.mk

include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := libmultirom
LOCAL_MODULE_TAGS := optional
LOCAL_SHARED_LIBRARIES := libcutils libc libm libpng libz libft2
LOCAL_WHOLE_STATIC_LIBRARIES := libdrm
LOCAL_CFLAGS += $(common_C_FLAGS)
LOCAL_SRC_FILES := $(common_SRC_FILES)
LOCAL_C_INCLUDES += $(common_C_INCLUDES)

MR_NO_KEXEC_MK_OPTIONS := true 1 allowed 2 enabled 3 ui_confirm 4 ui_choice 5 forced
ifneq (,$(filter $(MR_NO_KEXEC), $(MR_NO_KEXEC_MK_OPTIONS)))
    # clone libbootimg to /system/extras/ from
    # https://github.com/Tasssadar/libbootimg.git
    LOCAL_STATIC_LIBRARIES += libbootimg
    LOCAL_C_INCLUDES += system/extras/libbootimg/include
    LOCAL_SRC_FILES += ../no_kexec.c
endif

include $(multirom_local_path)/device_defines.mk

include $(BUILD_SHARED_LIBRARY)
