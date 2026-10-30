/*
 * Copyright (C) 2026 Aeron-Aeron
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "dolby-effects-proxy"

#include "EffectsFactoryProxy.h"

#include <android/hardware/audio/effect/7.0/IEffectsFactory.h>
#include <hardware/audio_effect.h>
#include <hidl/HidlTransportSupport.h>
#include <log/log.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace {
using android::sp;
using android::hardware::Return;
using android::hardware::Void;
using android::hardware::hidl_death_recipient;
using android::hardware::hidl_handle;
using android::hardware::hidl_string;
using android::hardware::hidl_vec;
using android::hardware::audio::common::V7_0::AudioIoHandle;
using android::hardware::audio::common::V7_0::AudioPortHandle;
using android::hardware::audio::common::V7_0::AudioSession;
using android::hardware::audio::common::V7_0::Uuid;
using android::hardware::audio::effect::V7_0::EffectDescriptor;
using android::hardware::audio::effect::V7_0::IEffect;
using android::hardware::audio::effect::V7_0::IEffectsFactory;
using android::hardware::audio::effect::V7_0::Result;

constexpr size_t kDolbyEffectCount = 4;
const std::array<Uuid, kDolbyEffectCount> kDolbyUuids = {{
        {0x9d4921da, 0x8225, 0x4f29, 0xaefa,
         std::array<uint8_t, 6>{{0x39, 0x53, 0x7a, 0x04, 0xbc, 0xaa}}},
        {0x3783c334, 0xd3a0, 0x4d13, 0x874f,
         std::array<uint8_t, 6>{{0x00, 0x32, 0xe5, 0xfb, 0x80, 0xe2}}},
        {0x64a0f614, 0x7fa4, 0x48b8, 0xb081,
         std::array<uint8_t, 6>{{0xd5, 0x9d, 0xc9, 0x54, 0x61, 0x6f}}},
        {0xccd4cf09, 0xa79d, 0x46c2, 0x9aae,
         std::array<uint8_t, 6>{{0x06, 0xa1, 0x69, 0x8d, 0x6c, 0x8f}}},
}};
constexpr uint64_t kRemoteIdBit = uint64_t{1} << 63;

size_t dolbyIndex(const Uuid& uuid) {
    for (size_t i = 0; i < kDolbyUuids.size(); ++i) {
        if (uuid == kDolbyUuids[i]) return i;
    }
    return kDolbyUuids.size();
}

bool isSoftwareInsert(const EffectDescriptor& descriptor) {
    return (descriptor.flags & EFFECT_FLAG_TYPE_MASK) == EFFECT_FLAG_TYPE_INSERT &&
            (descriptor.flags & EFFECT_FLAG_HW_ACC_MASK) == 0 &&
            (descriptor.flags & EFFECT_FLAG_OFFLOAD_SUPPORTED) == 0;
}

class CompanionDeath final : public hidl_death_recipient {
    void serviceDied(uint64_t, const android::wp<android::hidl::base::V1_0::IBase>&) override {
        // Reconnect all AudioFlinger clients through the normal audio-HAL restart.
        LOG_ALWAYS_FATAL("The Dolby effects host died");
    }
};

class EffectsFactoryProxy final : public IEffectsFactory {
  public:
    EffectsFactoryProxy(const sp<IEffectsFactory>& local, const sp<IEffectsFactory>& remote)
        : mLocal(local), mRemote(remote), mDeath(new CompanionDeath) {}

    bool initialize() {
        auto death = mRemote->linkToDeath(mDeath, 0);
        if (!death.isOk() || !static_cast<bool>(death)) return false;
        Result status = Result::NOT_INITIALIZED;
        auto call = getAllDescriptors([&](Result result, const hidl_vec<EffectDescriptor>&) {
            status = result;
        });
        return call.isOk() && status == Result::OK;
    }

    Return<void> getAllDescriptors(getAllDescriptors_cb callback) override {
        std::vector<EffectDescriptor> descriptors;
        Result status = Result::NOT_INITIALIZED;
        auto local = mLocal->getAllDescriptors(
                [&](Result result, const hidl_vec<EffectDescriptor>& list) {
                    status = result;
                    if (result != Result::OK) return;
                    for (const auto& descriptor : list) {
                        if (dolbyIndex(descriptor.uuid) == kDolbyUuids.size()) {
                            descriptors.push_back(descriptor);
                        }
                    }
                });
        if (!local.isOk()) return local;
        if (status != Result::OK) {
            callback(status, {});
            return Void();
        }

        std::array<bool, kDolbyEffectCount> found{};
        bool valid = true;
        auto remote = mRemote->getAllDescriptors(
                [&](Result result, const hidl_vec<EffectDescriptor>& list) {
                    status = result;
                    if (result != Result::OK) return;
                    for (const auto& descriptor : list) {
                        const size_t index = dolbyIndex(descriptor.uuid);
                        if (index == kDolbyUuids.size()) continue;
                        // Raw hardware/preprocessing handles must stay in the 32-bit process.
                        if (found[index] || !isSoftwareInsert(descriptor)) {
                            valid = false;
                            continue;
                        }
                        found[index] = true;
                        descriptors.push_back(descriptor);
                    }
                });
        if (!remote.isOk()) return remote;
        if (status != Result::OK || !valid ||
            !std::all_of(found.begin(), found.end(), [](bool value) { return value; })) {
            ALOGE("The Dolby effects host did not expose all four software effects");
            callback(Result::NOT_INITIALIZED, {});
            return Void();
        }
        hidl_vec<EffectDescriptor> result;
        result.resize(descriptors.size());
        std::copy(descriptors.begin(), descriptors.end(), result.begin());
        callback(Result::OK, result);
        return Void();
    }

    Return<void> getDescriptor(const Uuid& uuid, getDescriptor_cb callback) override {
        return (dolbyIndex(uuid) == kDolbyUuids.size() ? mLocal : mRemote)
                ->getDescriptor(uuid, callback);
    }

    Return<void> createEffect(const Uuid& uuid, AudioSession session, AudioIoHandle io,
                              AudioPortHandle device, createEffect_cb callback) override {
        if (dolbyIndex(uuid) == kDolbyUuids.size()) {
            return mLocal->createEffect(uuid, session, io, device, callback);
        }
        return mRemote->createEffect(
                uuid, session, io, device,
                [&](Result status, const sp<IEffect>& effect, uint64_t id) {
                    if (status == Result::OK && (!effect || id == 0 || (id & kRemoteIdBit))) {
                        if (effect) {
                            auto close = effect->close();
                            if (!close.isOk()) ALOGW("Could not close invalid remote effect");
                        }
                        callback(Result::NOT_INITIALIZED, nullptr, 0);
                        return;
                    }
                    // The IEffect Binder points directly to the 64-bit host. Only its
                    // optional raw-HAL ID is tagged to avoid a local EffectMap collision.
                    callback(status, effect, status == Result::OK ? id | kRemoteIdBit : 0);
                });
    }

    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& options) override {
        auto local = mLocal->debug(fd, options);
        if (!local.isOk()) return local;
        return mRemote->debug(fd, options);
    }

  private:
    const sp<IEffectsFactory> mLocal;
    const sp<IEffectsFactory> mRemote;
    const sp<hidl_death_recipient> mDeath;
};
}  // namespace

bool registerDolbyEffectsFactory() {
    auto local = IEffectsFactory::getService("default", true /* getStub */);
    auto remote = IEffectsFactory::getService("dolby");
    if (!local || !remote) {
        ALOGE("Cannot connect the local and Dolby effects factories");
        return false;
    }
    sp<EffectsFactoryProxy> factory = new EffectsFactoryProxy(local, remote);
    return factory->initialize() && factory->registerAsService("default") == android::OK;
}
