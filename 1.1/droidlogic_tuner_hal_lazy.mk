
USE_TH_TEST_SETUP := false
#$(warning USE TEST TESTUP=$(USE_TH_TEST_SETUP))
#$(warning TARGET_PRODUCT=$(TARGET_PRODUCT))

PRODUCT_VENDOR_PROPERTIES += ro.tuner.lazyhal=true
PRODUCT_PACKAGES += \
    android.hardware.tv.tuner@1.1-service.droidlogic-lazy

PRODUCT_COPY_FILES += \
    frameworks/native/data/etc/android.hardware.tv.tuner.xml:$(TARGET_COPY_OUT_VENDOR)/etc/permissions/android.hardware.tv.tuner.xml \
    hardware/amlogic/tuner/config/tuner_vts_config_1_1.xml:$(TARGET_COPY_OUT_VENDOR)/etc/tuner_vts_config_1_1.xml \
    hardware/amlogic/tuner/config/tuner_vts_config_1_0.xml:$(TARGET_COPY_OUT_VENDOR)/etc/tuner_vts_config_1_0.xml