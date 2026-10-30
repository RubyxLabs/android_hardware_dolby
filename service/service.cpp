/*
 * Copyright (C) 2021 The Android Open Source Project
 * Copyright (C) 2026 Aeron-Aeron
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "dolby-c2-service"

#include <signal.h>

#include <C2Component.h>
#include <binder/ProcessState.h>
#include <codec2/hidl/1.2/ComponentStore.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>
#include <minijail.h>

#include <memory>

namespace android {
// Provided by the matching Dolby native component-store library.
std::shared_ptr<C2ComponentStore> GetCodec2DolbyComponentStore();
}  // namespace android

int main() {
    signal(SIGPIPE, SIG_IGN);
    android::SetUpMinijail("/vendor/etc/seccomp_policy/vendor.dolby.media.c2.policy", "");

    android::ProcessState::initWithDriver("/dev/vndbinder");
    android::ProcessState::self()->startThreadPool();
    android::hardware::configureRpcThreadpool(8, true);

    auto nativeStore = android::GetCodec2DolbyComponentStore();
    if (!nativeStore) {
        ALOGE("Cannot create the Dolby native component store");
        return 1;
    }

    using android::hardware::media::c2::V1_2::utils::ComponentStore;
    android::sp<ComponentStore> store = new ComponentStore(nativeStore);
    if (store->status() != C2_OK) {
        ALOGE("Cannot initialize the Dolby Codec2 service: %d", store->status());
        return 1;
    }
    if (store->registerAsService("default1") != android::OK) {
        ALOGE("Cannot register the Dolby Codec2 service");
        return 1;
    }

    android::hardware::joinRpcThreadpool();
    return 1;
}
