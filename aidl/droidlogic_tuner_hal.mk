
USE_TH_TEST_SETUP := false
#$(warning USE TEST TESTUP=$(USE_TH_TEST_SETUP))
#$(warning TARGET_PRODUCT=$(TARGET_PRODUCT))
PRODUCT_VENDOR_PROPERTIES += ro.vendor.vts_tuner_configuration_variant=amlogic

PRODUCT_PACKAGES += \
    android.hardware.tv.tuner-service.droidlogic

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.tv.tuner.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.tv.tuner.xml \
    hardware/amlogic/tuner/config/tuner_vts_config_aidl_V1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/tuner_vts_config_aidl_V1.amlogic.xml \
    hardware/amlogic/tuner/config/tuner_cts_config_V1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/tuner_cts_config_V1.xml


ifeq ($(USE_TH_TEST_SETUP), true)
PRODUCT_PACKAGES += \
    TunerFrameworkSetup
endif
