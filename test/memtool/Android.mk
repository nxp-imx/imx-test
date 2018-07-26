ifeq ($(findstring imx, $(TARGET_BOARD_PLATFORM)), imx)
LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
       memtool.c \
       mx6dl_modules.c \
       mx6q_modules.c \
       mx6sx_modules.c \
       mx6sl_modules.c \
       mx6ul_modules.c \
       mx7d_modules.c \
       mx6ull_modules.c \
       mx7ulp_modules.c \
       mx8mq_modules.c

#LOCAL_CFLAGS += -DBUILD_FOR_ANDROID

LOCAL_C_INCLUDES += $(LOCAL_PATH) \


LOCAL_SHARED_LIBRARIES := libutils libc

LOCAL_MODULE := memtool
LOCAL_MULTILIB := both
LOCAL_MODULE_TAGS := tests
LOCAL_MODULE_STEM_32 := memtool_32
LOCAL_MODULE_STEM_64 := memtool_64
LOCAL_VENDOR_MODULE := true
include $(BUILD_EXECUTABLE)
endif
