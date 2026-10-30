/*
 * Copyright (C) 2026 Aeron-Aeron
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "dolby-effects-service"

#include <signal.h>

#include <android/hardware/audio/effect/7.0/IEffectsFactory.h>
#include <binder/ProcessState.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

int main() {
    signal(SIGPIPE, SIG_IGN);
    if (android::ProcessState::isVndservicemanagerEnabled()) {
        android::ProcessState::initWithDriver("/dev/vndbinder");
        android::ProcessState::self()->startThreadPool();
    }
    android::hardware::configureRpcThreadpool(16, true);
    using android::hardware::audio::effect::V7_0::IEffectsFactory;
    // The stock passthrough loader accepts "default" only. Publish the loaded
    // 64-bit implementation under a distinct Binder instance for the proxy.
    auto factory = IEffectsFactory::getService("default", true /* getStub */);
    LOG_ALWAYS_FATAL_IF(!factory || factory->registerAsService("dolby") != android::OK,
                        "Cannot register the 64-bit Dolby effects factory");
    android::hardware::joinRpcThreadpool();
    return 1;
}
