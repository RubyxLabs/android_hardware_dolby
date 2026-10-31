# SPDX-FileCopyrightText: 2022 FlamingoOS Project
# SPDX-FileCopyrightText: 2026 Aeron-Aeron
# SPDX-License-Identifier: Apache-2.0

DOLBY_PATH := hardware/dolby

PRODUCT_SOONG_NAMESPACES += $(DOLBY_PATH)
BOARD_VENDOR_SEPOLICY_DIRS += $(DOLBY_PATH)/sepolicy/vendor
SYSTEM_EXT_PUBLIC_SEPOLICY_DIRS += $(DOLBY_PATH)/sepolicy/public
SYSTEM_EXT_PRIVATE_SEPOLICY_DIRS += $(DOLBY_PATH)/sepolicy/private
DEVICE_FRAMEWORK_COMPATIBILITY_MATRIX_FILE += $(DOLBY_PATH)/configs/vintf/dolby_framework_matrix.xml

PRODUCT_PACKAGES += \
    LunarisDolby \
    init.dolby.rc \
    android.hardware.audio.service.rubyx \
    android.hardware.audio.effect-service.dolby \
    android.hardware.audio.effect@7.0-impl:64 \
    vendor.dolby.hardware.dms@2.0-service \
    vendor.dolby.media.c2@1.2-service \
    libcodec2_soft_ac4dec \
    libcodec2_soft_ddpdec \
    libcodec2_soft_dolby \
    libcodec2_store_dolby \
    libdapparamstorage \
    libdeccfg \
    libdlbdsservice \
    libdlbpreg \
    libspatializerparamstorage \
    libdlbvol \
    libswdap \
    libswgamedap \
    libswspatializer \
    libswvqe \
    vendor.dolby.hardware.dms@2.0 \
    vendor.dolby.hardware.dms@2.0-impl \
    libstagefright_foundation_dolby

PRODUCT_COPY_FILES += \
    $(DOLBY_PATH)/configs/audio/audio_effects.xml:$(TARGET_COPY_OUT_VENDOR)/etc/audio_effects.xml \
    $(DOLBY_PATH)/configs/dax/dax-default.xml:$(TARGET_COPY_OUT_VENDOR)/etc/dolby/dax-default.xml \
    $(DOLBY_PATH)/configs/dax/dax-default.xml:$(TARGET_COPY_OUT_ODM)/etc/dolby/dax-default.xml \
    $(DOLBY_PATH)/configs/media/media_codecs_dolby_audio.xml:$(TARGET_COPY_OUT_VENDOR)/etc/media_codecs_dolby_audio.xml

# Enable framework discovery of the actual 64-bit Spatializer effect.
PRODUCT_SYSTEM_EXT_PROPERTIES += \
    ro.audio.spatializer_enabled=true \
    ro.audio.spatializer_transaural_enabled_default=false

PRODUCT_VENDOR_PROPERTIES += \
    ro.vendor.dolby.dax.version=DAX3_3.7.0.8_r1 \
    ro.vendor.audio.dolby.dax.support=true \
    ro.vendor.audio.dolby.surround.enable=true \
    ro.vendor.audio.dolby.spatial.xml=false \
    vendor.audio.dolby.ds2.enabled=false \
    vendor.audio.dolby.ds2.hardbypass=false
