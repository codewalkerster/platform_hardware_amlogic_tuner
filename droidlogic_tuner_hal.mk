
USE_TH_TEST_SETUP := true
#$(warning USE TEST TESTUP=$(USE_TH_TEST_SETUP))
#$(warning TARGET_PRODUCT=$(TARGET_PRODUCT))

PRODUCT_PACKAGES += \
    android.hardware.tv.tuner@1.0-service.droidlogic

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.tv.tuner.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.tv.tuner.xml \

ifeq ($(USE_TH_TEST_SETUP), true)
PRODUCT_PACKAGES += \
    TunerFrameworkSetup

PRODUCT_COPY_FILES += \
    device/amlogic/ohm/prebuilt/privapp-permissions-amlogic.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/privapp-permissions-amlogic.xml
endif