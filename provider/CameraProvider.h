/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <memory>

#include <aidl/android/hardware/camera/provider/BnCameraProvider.h>
#include <aidl/android/hardware/camera/provider/ICameraProviderCallback.h>

#include "HwCamera.h"
#include "Span.h"
#include "CameraDevice.h"
#include "hardware/camera_common.h"
#include "utils/Mutex.h"
#include "utils/SortedVector.h"
#include <SimpleThread.h>
#include "CameraModule.h"
#include <map>

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {

using aidl::android::hardware::camera::common::VendorTagSection;
using aidl::android::hardware::camera::device::ICameraDevice;
using aidl::android::hardware::camera::provider::BnCameraProvider;
using aidl::android::hardware::camera::provider::CameraIdAndStreamCombination;
using aidl::android::hardware::camera::provider::ConcurrentCameraIdCombination;
using aidl::android::hardware::camera::provider::ICameraProviderCallback;
using ndk::ScopedAStatus;

using aidl::android::hardware::camera::common::CameraDeviceStatus;
using ::AStatus;
using aidl::android::hardware::camera::common::TorchModeStatus;
using aidl::android::hardware::camera::common::VendorTag;
using ::android::hardware::camera::common::helper::SimpleThread;
using ::android::hardware::camera::common::V1_0::helper::CameraModule;
using common::helper::VendorTagDescriptor;

using android::hardware::camera::device::implementation::Span;
using android::hardware::camera::device::implementation::CameraDevice;

struct CameraProvider : camera_module_callbacks_t,public BnCameraProvider {
    CameraProvider(int deviceIdBase, Span<const device::implementation::hw::HwCameraFactory> availableCameras);
    ~CameraProvider() override;

    ScopedAStatus setCallback(
            const std::shared_ptr<ICameraProviderCallback>& callback) override;
    ScopedAStatus getVendorTags(std::vector<VendorTagSection>* vts) override;
    ScopedAStatus getCameraIdList(std::vector<std::string>* camera_ids) override;
    ScopedAStatus getCameraDeviceInterface(
            const std::string& in_cameraDeviceName,
            std::shared_ptr<ICameraDevice>* device) override;
    ScopedAStatus notifyDeviceStateChange(int64_t in_deviceState) override;
    ScopedAStatus getConcurrentCameraIds(
            std::vector<ConcurrentCameraIdCombination>* concurrent_camera_ids) override;
    ScopedAStatus isConcurrentStreamCombinationSupported(
            const std::vector<CameraIdAndStreamCombination>& in_configs,
            bool* support) override;
public:
    static void sCameraDeviceStatusChange(
        const struct camera_module_callbacks* callbacks,
        int camera_id,
        int new_status);
    static void sTorchModeStatusChange(
        const struct camera_module_callbacks* callbacks,
        const char* camera_id,
        int new_status);
    Mutex mCbLock;
    const int mDeviceIdBase;
private:
    const Span<const device::implementation::hw::HwCameraFactory> mAvailableCameras;
    std::shared_ptr<ICameraProviderCallback> mCallback;

    sp<CameraModule> mModule;
    // Must be queried before using any APIs.
    // APIs will only work when this returns true
    bool mInitFailed;
    static const int kMaxCameraIdLen = 16;
    int mNumberOfLegacyCameras;
    std::map<std::string, camera_device_status_t> mCameraStatusMap; // camera id -> status
    std::map<std::string, bool> mOpenLegacySupported; // camera id -> open_legacy HAL1.0 supported
    SortedVector<std::string> mCameraIds; // the "0"/"1" legacy camera Ids
    // (cameraId string, hidl device name) pairs
    SortedVector<std::pair<std::string, std::string>> mCameraDeviceNames;

    int mPreferredHal3MinorVersion;
    void addDeviceNames(int camera_id, CameraDeviceStatus status = CameraDeviceStatus::PRESENT,
                        bool cam_new = false);
    void removeDeviceNames(int camera_id);
    class HotplugThread : public SimpleThread {
      public:
        explicit HotplugThread(CameraProvider* parent);
        ~HotplugThread() override;

      protected:
        bool threadLoop() override;
      public:
        int getMipiStatusFromFd();
        void setMipiCameraId(int camera_id){
          mMipiCameraId = camera_id;
        }
      private:
        bool initialize();
        void updateMipiHdmiStatusToProp(int value);
        int subscribeEvent(int event);

        CameraProvider* mParent;
        bool mIsInitialized = false;
        int mFd = -1;
        int mPipeFd[2] = {-1, -1};
        int mMipiCameraId = -1;
    };

    std::shared_ptr<HotplugThread> mHotPlugThread;

};

}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
