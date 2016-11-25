#!/bin/bash

cat << EOF
LOCAL_PATH := \$(call my-dir)

#
# Build libwrap:
#

include \$(CLEAR_VARS)
LOCAL_MODULE	:= libwrap
LOCAL_SRC_FILES	:= wrap/wrap-util.c wrap/wrap-syscall.c
LOCAL_C_INCLUDES := \$(LOCAL_PATH)/includes \$(LOCAL_PATH)/util
LOCAL_LDLIBS := -llog -lc -ldl
include \$(BUILD_SHARED_LIBRARY)

include \$(CLEAR_VARS)
LOCAL_MODULE    := libwrapfake
LOCAL_SRC_FILES := wrap/wrap-util.c wrap/wrap-syscall-fake.c
LOCAL_C_INCLUDES := \$(LOCAL_PATH)/includes \$(LOCAL_PATH)/util
LOCAL_LDLIBS := -llog -lc -ldl
include \$(BUILD_SHARED_LIBRARY)


#
# 3D Test Apps:
#
EOF

for f in tests-3d/test-*.c; do
	modname=`basename $f`
	modname=${modname%.c}
	grep GLES3 $f > /dev/null && gles="GLESv3" || gles="GLESv2"

	cat << EOF

include \$(CLEAR_VARS)
LOCAL_MODULE    := $modname
LOCAL_SRC_FILES := $f
LOCAL_C_INCLUDES := \$(LOCAL_PATH)/includes \$(LOCAL_PATH)/util
LOCAL_CFLAGS := -DBIONIC -std=c99
LOCAL_LDLIBS := -llog -lc -ldl -lEGL -l$gles
include \$(BUILD_EXECUTABLE)
EOF
done

cat << EOF

#
# 2D Test Apps:
#
EOF

for f in tests-2d/test-*.c; do
	modname=`basename $f`
	modname=${modname%.c}

	cat << EOF

include \$(CLEAR_VARS)
LOCAL_MODULE    := $modname
LOCAL_SRC_FILES := $f tests-2d/c2d2-shim.c
LOCAL_C_INCLUDES := \$(LOCAL_PATH)/includes \$(LOCAL_PATH)/util
LOCAL_CFLAGS := -DBIONIC
LOCAL_LDLIBS := -lc -ldl -llog -lm
include \$(BUILD_EXECUTABLE)
EOF
done

