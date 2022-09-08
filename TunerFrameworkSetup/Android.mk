LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_RESOURCE_DIR := $(LOCAL_PATH)/res
LOCAL_SRC_FILES := $(call all-java-files-under, src)

LOCAL_PACKAGE_NAME := TunerFrameworkSetup
LOCAL_CERTIFICATE := platform
LOCAL_PROGUARD_ENABLED := disabled
LOCAL_DEX_PREOPT := false
LOCAL_VENDOR_MODULE := true
#LOCAL_PRIVILEGED_MODULE := true
ifeq (1, $(strip $(shell expr $(PLATFORM_SDK_VERSION) \<= 30)))
LOCAL_JAVA_LIBRARIES := droidlogic
else
LOCAL_REQUIRED_MODULES := droidlogic.software.core
LOCAL_JAVA_LIBRARIES := droidlogic.software.core
LOCAL_USES_LIBRARIES := droidlogic.software.core
endif
LOCAL_LICENSE_KINDS := SPDX-license-identifier-Apache-2.0
LOCAL_LICENSE_CONDITIONS := notice
LOCAL_NOTICE_FILE := $(LOCAL_PATH)/../LICENSE
include $(BUILD_PACKAGE)
