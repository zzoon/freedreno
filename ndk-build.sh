export NDK_PROJECT_PATH=`pwd`
export NDK_ROOT=$HOME/dev/tools/android/android-ndk-r13b
$NDK_ROOT/ndk-build NDK_APPLICATION_MK=./Application.mk -j4
