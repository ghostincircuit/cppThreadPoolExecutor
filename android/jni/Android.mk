LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := jni
LOCAL_C_INCLUDES := ThreadPoolExecutor
LOCAL_SRC_FILES := jni.cpp ThreadPoolExecutor/TestThreadPoolExecutor.cc ThreadPOolExecutor/ThreadPOolExecutor.cc

LOCAL_CFLAGS += -std=c++11 -g -ggdb -O0 -pthread
#LOCAL_CFLAGS += -std=c++11

LOCAL_LDLIBS += -latomic -llog

include $(BUILD_SHARED_LIBRARY)
