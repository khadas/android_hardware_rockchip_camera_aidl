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

#include <inttypes.h>

#include <log/log.h>

#include "CameraProvider.h"
#include "CameraDevice.h"
#include "HwCamera.h"
#include "debug.h"
#include "VendorTagDescriptor.h"
#include <cutils/properties.h>
#include <linux/v4l2-subdev.h>
#include <poll.h>

namespace android {
namespace hardware {
namespace camera {
namespace provider {
namespace implementation {
namespace {
#define MIPI_CAMERA_NUMBERS "persist.vendor.camera.numbers"
#define MIPI_HDMI_STATUS "persist.vendor.camera.mipistatus"

#define MIPI_HDMI_STATUS_ON  "1"
#define MIPI_HDMI_STATUS_OFF "0"

#define V4L2_EVENT_PRIVATE_START          0x08000000

#define BASE_VIDIOC_PRIVATE 192     /* 192-255 are private */

#define RKMODULE_GET_HDMI_MODE       \
        _IOR('V', BASE_VIDIOC_PRIVATE + 34, __u32)

/* Private v4l2 event */
#define RK_HDMIRX_V4L2_EVENT_SIGNAL_LOST \
        (V4L2_EVENT_PRIVATE_START + 1)

#define CLEAR(x) memset (&(x), 0, sizeof (x))

enum hdmi_status{
    HDMI_PLUG_OFF = 0,
    HDMI_PLUG_IN = 1,
};

const int kMaxDevicePathLen = 256;
const char* kDevicePath = "/dev/";
const char kPrefix[] = "v4l-subdev";
const int kPrefixLen = sizeof(kPrefix) - 1;
const int kDevicePrefixLen = sizeof(kDevicePath) + kPrefixLen + 1;
char kV4l2DevicePath[kMaxDevicePathLen];

ndk::ScopedAStatus toScopedAStatus(const aidl::android::hardware::camera::common::Status s) {
    return ndk::ScopedAStatus::fromServiceSpecificError(static_cast<int32_t>(s));
}
std::string getHdmiID(){
    std::string  hdmiid;
    char value[PROPERTY_VALUE_MAX]={0};
    property_get("persist.vendor.camera.hdmiid", value, "");
    ALOGD("%s:persist.vendor.camera.hdmiid %s",__FUNCTION__,value);
    hdmiid = value;
    return hdmiid;
}
void updateHdmiID(const char* value){
     ALOGD("%s:persist.vendor.camera.mipi %s",__FUNCTION__,value);
     property_set("persist.vendor.camera.mipi", value);
}
std::string getMipiCameraNumbers(){
    std::string  numbers;
    char value[PROPERTY_VALUE_MAX]={0};
    property_get("persist.vendor.camera.numbers", value, "");
    ALOGD("persist.vendor.camera.numbers %s",value);
    numbers = value;
    return numbers;
}
}
using aidl::android::hardware::camera::common::Status;
using ::aidl::android::hardware::camera::common::CameraMetadataType;
using ::android::hardware::camera::common::V1_0::helper::VendorTagDescriptor;
//using android::hardware::device::implementation::CameraDevice;


CameraProvider::CameraProvider(const int deviceIdBase,
                               Span<const device::implementation::hw::HwCameraFactory> availableCameras)
        : camera_module_callbacks_t({sCameraDeviceStatusChange,
                                   sTorchModeStatusChange})
        ,mDeviceIdBase(deviceIdBase)
        , mAvailableCameras(availableCameras) {
    mHotPlugThread = std::make_shared<HotplugThread>(this);
    for (int i = 0; i < mAvailableCameras.size(); ++i) {
        auto hwCamera = mAvailableCameras[i]();
        if (hwCamera) {
            mModule =  hwCamera->getModule();
            if (mModule!=nullptr)
            {
                mModule->setCallbacks(this);
            }
            std::string legacyId =  hwCamera->getCameraId();
            int legacyIdInt = atoi(legacyId.c_str());
            char cameraId[kMaxCameraIdLen];
            snprintf(cameraId, sizeof(cameraId), "%d",mDeviceIdBase + legacyIdInt);
            std::string cameraIdStr(cameraId);
            std::string hdmi_id = getHdmiID();

            //ALOGD("%s: mDeviceIdBase=%d id = %d cameraIdStr=%s hdmi_id=%s", __FUNCTION__, mDeviceIdBase,legacyIdInt,cameraIdStr.c_str(), hdmi_id.c_str());
            if (hdmi_id != "" && hdmi_id == legacyId) {
                mHotPlugThread->setMipiCameraId(legacyIdInt);
                hdmi_status status =(hdmi_status) mHotPlugThread->getMipiStatusFromFd();
                if (status == HDMI_PLUG_IN)
                {
                    ALOGD("%s: HDMI camera is plugged in! cameraId = %s", __func__, cameraId);
                    mCameraStatusMap[cameraIdStr] = CAMERA_DEVICE_STATUS_PRESENT;
                    addDeviceNames(legacyIdInt);
                }else {
                    ALOGD("%s: HDMI camera is not plugged in! cameraId = %s", __func__, cameraId);
                }
            }else{
                ALOGD("%s: Add mipi camera device cameraId = %s!", __func__, cameraId);
                mCameraStatusMap[cameraIdStr] = CAMERA_DEVICE_STATUS_PRESENT;
                addDeviceNames(legacyIdInt);
            }
        }
    }
    mHotPlugThread->run();
}

CameraProvider::~CameraProvider() {}

ScopedAStatus CameraProvider::setCallback(
        const std::shared_ptr<ICameraProviderCallback>& callback) {
    if (callback == nullptr) {
        return toScopedAStatus(FAILURE(Status::ILLEGAL_ARGUMENT));
    }
    mCallback = callback;
    if (mModule!=nullptr)
    {
        mModule->setCallbacks(this);
    }
    for (auto const& statusPair : mCameraStatusMap) {
        int id = std::stoi(statusPair.first) - mDeviceIdBase;
        auto status = static_cast<CameraDeviceStatus>(statusPair.second);
        if (status != CameraDeviceStatus::NOT_PRESENT) {
            addDeviceNames(id, status, true);
        }
    }
    return ScopedAStatus::ok();
}

ScopedAStatus CameraProvider::getVendorTags(std::vector<VendorTagSection>* vts) {
    *vts = {};
    sp<VendorTagDescriptor> desc = VendorTagDescriptor::getGlobalVendorTagDescriptor();
    if (desc)
    {
        auto  sectionNames = desc->getAllSectionNames();
        size_t numSections = sectionNames->size();
        std::vector<std::vector<VendorTag>> tagsBySection(numSections);
        int tagCount = desc->getTagCount();
        std::vector<uint32_t> tags(tagCount);
        desc->getTagArray(tags.data());
        for (int i = 0; i < tagCount; i++) {
            VendorTag vt;
            vt.tagId = tags[i];
            vt.tagName = desc->getTagName(tags[i]);
            vt.tagType = (CameraMetadataType)desc->getTagType(tags[i]);
            ssize_t sectionIdx = desc->getSectionIndex(tags[i]);
            tagsBySection[sectionIdx].push_back(vt);
        }
        vts->resize(numSections);
        for (size_t s = 0; s < numSections; s++) {
            (*vts)[s].sectionName = (*sectionNames)[s].c_str();
            (*vts)[s].tags = tagsBySection[s];
        }
    }
    return ScopedAStatus::ok();
}

ScopedAStatus CameraProvider::getCameraIdList(std::vector<std::string>* camera_ids) {
    int cavailebleCameras = 0;
    for (auto const& deviceNamePair : mCameraDeviceNames) {
        if (std::stoi(deviceNamePair.first) >= mAvailableCameras.size()) {
            continue;
        }
        if (mCameraStatusMap[deviceNamePair.first] == CAMERA_DEVICE_STATUS_PRESENT) {
            cavailebleCameras++;
        }
    }
    camera_ids->reserve(cavailebleCameras);

    for (auto const& deviceNamePair : mCameraDeviceNames) {
        if (std::stoi(deviceNamePair.first) >= mAvailableCameras.size()) {
            continue;
        }
        if (mCameraStatusMap[deviceNamePair.first] == CAMERA_DEVICE_STATUS_PRESENT) {
            camera_ids->push_back(deviceNamePair.second);
        }
    }
    return ScopedAStatus::ok();
}

ScopedAStatus CameraProvider::getCameraDeviceInterface(
        const std::string& name,
        std::shared_ptr<ICameraDevice>* device) {
    const std::optional<int> maybeIndex = CameraDevice::parsePhysicalId(name);
    if (!maybeIndex) {
        return toScopedAStatus(FAILURE(Status::ILLEGAL_ARGUMENT));
    }

    const int index = maybeIndex.value() - mDeviceIdBase;
    //ALOGD("CameraProvider::getCameraDeviceInterface index %d, name %s,mabeIndex %d, mDeviceIdBase %d",index, name.c_str(), maybeIndex.value(), mDeviceIdBase);

    if ((index >= 0) && (index < mAvailableCameras.size())) {
        auto hwCamera = mAvailableCameras[index]();
        if (hwCamera) {
            std::string hdmiid = getHdmiID();
            if (hdmiid != "")
            {
                if (hdmiid == hwCamera->getCameraId())
                {
                    updateHdmiID(std::to_string(maybeIndex.value()).c_str());
                }
            }
            char cameraId[kMaxCameraIdLen];
            snprintf(cameraId, sizeof(cameraId), "%d", mDeviceIdBase + index);
            std::string cameraIdStr(cameraId);
            //ALOGD("CameraProvider::getCameraDeviceInterface cameraId:%s, hdmiid:%s, index:%d,name:%s",cameraId,hdmiid.c_str(),index,name.c_str());
            if (mCameraStatusMap.count(cameraIdStr) == 0 ||
                    mCameraStatusMap[cameraIdStr] != CAMERA_DEVICE_STATUS_PRESENT) {
                return toScopedAStatus(FAILURE(Status::ILLEGAL_ARGUMENT));
            }
            auto p = ndk::SharedRefBase::make<CameraDevice>(std::move(hwCamera),hwCamera->getCameraId());
            p->mSelf = p;
            *device = std::move(p);
            return ScopedAStatus::ok();
        } else {
            return toScopedAStatus(FAILURE(Status::INTERNAL_ERROR));
        }
    } else {
        return toScopedAStatus(FAILURE(Status::ILLEGAL_ARGUMENT));
    }
}

ScopedAStatus CameraProvider::notifyDeviceStateChange(const int64_t /*deviceState*/) {
    return ScopedAStatus::ok();
}

ScopedAStatus CameraProvider::getConcurrentCameraIds(
        std::vector<ConcurrentCameraIdCombination>* concurrentCameraIds) {
    *concurrentCameraIds = {};
    return ScopedAStatus::ok();
}

ScopedAStatus CameraProvider::isConcurrentStreamCombinationSupported(
        const std::vector<CameraIdAndStreamCombination>& /*configs*/,
        bool* support) {
    *support = false;
    return ScopedAStatus::ok();
}
void CameraProvider::addDeviceNames(int camera_id, CameraDeviceStatus status, bool cam_new)
{
    ALOGD("addDeviceNames: camera_id=%d, status=%d, cam_new=%d, mDeviceIdBase=%d",camera_id, status, cam_new, mDeviceIdBase);
    char cameraId[kMaxCameraIdLen];
    snprintf(cameraId, sizeof(cameraId), "%d", mDeviceIdBase + camera_id);
    std::string cameraIdStr(cameraId);

    mCameraIds.add(cameraIdStr);

    // initialize mCameraDeviceNames and mOpenLegacySupported
    mOpenLegacySupported[cameraIdStr] = false;
    int deviceVersion = mModule->getDeviceVersion(camera_id);
    auto deviceNamePair = std::make_pair(cameraIdStr,
                                          CameraDevice::getPhysicalId(mDeviceIdBase + camera_id));
    mCameraDeviceNames.add(deviceNamePair);
    if (cam_new) {
        mCallback->cameraDeviceStatusChange(deviceNamePair.second, status);
    }
}

void CameraProvider::removeDeviceNames(int camera_id)
{
    ALOGD("removeDeviceNames: camera_id=%d mDeviceIdBase=%d", camera_id, mDeviceIdBase);
    std::string cameraIdStr = std::to_string(mDeviceIdBase + camera_id);

    mCameraIds.remove(cameraIdStr);

    int deviceVersion = mModule->getDeviceVersion(camera_id);
    auto deviceNamePair = std::make_pair(cameraIdStr,
                                           CameraDevice::getPhysicalId(mDeviceIdBase + camera_id));
    mCameraDeviceNames.remove(deviceNamePair);
    mCallback->cameraDeviceStatusChange(deviceNamePair.second, CameraDeviceStatus::NOT_PRESENT);
    mModule->removeCamera(camera_id);
}
/**
 * static callback forwarding methods from HAL to instance
 */
void CameraProvider::sCameraDeviceStatusChange(
        const struct camera_module_callbacks* callbacks,
        int camera_id,
        int new_status) {
    CameraProvider* cp = const_cast<CameraProvider*>(
            static_cast<const CameraProvider*>(callbacks));
    if (cp == nullptr) {
        ALOGE("%s: callback ops is null", __FUNCTION__);
        return;
    }
    // ALOGD("%s: camera_id=%d, new_status=%d", __FUNCTION__,cp->mDeviceIdBase + camera_id, new_status);
    Mutex::Autolock _l(cp->mCbLock);
    if (cp->mCallback == nullptr) {
        // For camera connected before mCallback is set, the corresponding
        // addDeviceNames() would be called later in setCallbacks().
        return;
    }

    char cameraId[kMaxCameraIdLen];
    snprintf(cameraId, sizeof(cameraId), "%d",cp->mDeviceIdBase + camera_id);
    std::string cameraIdStr(cameraId);
    cp->mCameraStatusMap[cameraIdStr] = (camera_device_status_t) new_status;
    bool found = false;
    CameraDeviceStatus status = (CameraDeviceStatus)new_status;
    for (auto const& deviceNamePair : cp->mCameraDeviceNames) {
        if (cameraIdStr.compare(deviceNamePair.first) == 0) {
            cp->mCallback->cameraDeviceStatusChange(deviceNamePair.second, status);
            found = true;
        }
    }
    switch (status) {
        case CameraDeviceStatus::PRESENT:
        case CameraDeviceStatus::ENUMERATING:
            if (!found) {
                cp->addDeviceNames(camera_id, status, true);
            }
            break;
        case CameraDeviceStatus::NOT_PRESENT:
            if (found) {
                cp->removeDeviceNames(camera_id);
            }
    }
}

void CameraProvider::sTorchModeStatusChange(
        const struct camera_module_callbacks* callbacks,
        const char* camera_id,
        int new_status) {
    CameraProvider* cp = const_cast<CameraProvider*>(
            static_cast<const CameraProvider*>(callbacks));

    if (cp == nullptr) {
        ALOGE("%s: callback ops is null", __FUNCTION__);
        return;
    }

    Mutex::Autolock _l(cp->mCbLock);
    if (cp->mCallback != nullptr) {
        std::string cameraIdStr(camera_id);
        for (int i = 0; i < cp->mAvailableCameras.size(); ++i) {
            auto hwCamera = cp->mAvailableCameras[i]();
            if (hwCamera) {
                std::string id = hwCamera->getCameraId();
                if (cameraIdStr.compare(id) == 0)
                {
                    cameraIdStr = CameraDevice::getPhysicalId(cp->mDeviceIdBase + atoi(camera_id));
                    TorchModeStatus status = (TorchModeStatus) new_status;
                    cp->mCallback->torchModeStatusChange(cameraIdStr, status);
                }
            }
        }
    }
}
// Start CameraProvider::HotplugThread functions

int CameraProvider::HotplugThread::subscribeEvent(int event)
{
    if (mFd == -1) {
        ALOGW("Device %d already closed. cannot subscribe.",mFd);
        return -1;
    }
    struct v4l2_event_subscription sub;
    CLEAR(sub);
    sub.type = event;
    if(event == V4L2_EVENT_CTRL){
        sub.id = V4L2_CID_DV_RX_POWER_PRESENT;
    }
    int ret = ioctl(mFd, VIDIOC_SUBSCRIBE_EVENT, &sub);
    if (ret < 0) {
        ALOGE("error subscribing event %x: %s", event, strerror(errno));
        return ret;
    }
    return ret;
}

int CameraProvider::HotplugThread::getMipiStatusFromFd(){
    struct v4l2_control control;
    memset(&control, 0, sizeof(struct v4l2_control));
    control.id = V4L2_CID_DV_RX_POWER_PRESENT;
    int err = ioctl(mFd, VIDIOC_G_CTRL, &control);
    if (err < 0) {
        ALOGE("V4L2_CID_DV_RX_POWER_PRESENT failed ,%d(%s)", errno, strerror(errno));
        return -1;
    }
    ALOGD("%s VIDIOC_G_CTRL:%d",__FUNCTION__,control.value);
    updateMipiHdmiStatusToProp(control.value);
    return control.value;
}
CameraProvider::HotplugThread::HotplugThread(CameraProvider* parent)
: mParent(parent){
    getHdmiID();
    initialize();
    getMipiStatusFromFd();
}

CameraProvider::HotplugThread::~HotplugThread() {
    write(mPipeFd[1], "q", 1);
    close(mPipeFd[0]);
    close(mPipeFd[1]);
    // Clean up inotify descriptor if needed.
    if (mFd >= 0) {
        close(mFd);
    }
}

bool CameraProvider::HotplugThread::initialize() {
    bool initialized = false;
    DIR* devdir = opendir(kDevicePath);
    if(devdir == 0) {
        ALOGE("%s: cannot open %s! ", __FUNCTION__, kDevicePath);
        return initialized;
    }
    struct dirent* de;
    int videofd,ret;
    while ((de = readdir(devdir)) != 0) {
        // Find external v4l devices that's existing before we start watching and add them
        if (!strncmp(kPrefix, de->d_name, kPrefixLen)) {
            std::string deviceId(de->d_name + kPrefixLen);
            //ALOGD("found %s", de->d_name);
            //char v4l2DeviceDriver[16];
            snprintf(kV4l2DevicePath, kMaxDevicePathLen,"%s%s", kDevicePath, de->d_name);
            videofd = open(kV4l2DevicePath, O_RDWR);
            if (videofd < 0){
                ALOGE("[%s %d] open device failed:%x [%s]", __FUNCTION__, __LINE__, videofd,strerror(errno));
                continue;
            } else {
                uint32_t ishdmi;
                ret = ::ioctl(videofd, RKMODULE_GET_HDMI_MODE, (void*)&ishdmi);
                if (ret < 0) {
                    //ALOGV("RKMODULE_GET_HDMI_MODE Failed, error: %s", strerror(errno));
                    close(videofd);
                    continue;
                }
                ALOGD("%s RKMODULE_GET_HDMI_MODE:%d",kV4l2DevicePath,ishdmi);
                if (ishdmi)
                {
                    mFd = videofd;
                    ALOGD("MipiHdmi fd:%d",mFd);
                    if (mFd < 0)
                    {
                        return initialized;
                    }
                }
            }
        }
    }
    closedir(devdir);
    subscribeEvent(V4L2_EVENT_SOURCE_CHANGE);
    subscribeEvent(RK_HDMIRX_V4L2_EVENT_SIGNAL_LOST);
    subscribeEvent(V4L2_EVENT_CTRL);
    if (pipe(mPipeFd) < 0) {
        ALOGE("pipe failed: %s\n", strerror(errno));
    }
    mIsInitialized = true;
    return mIsInitialized;
}
void CameraProvider::HotplugThread::updateMipiHdmiStatusToProp(int value){
    if (value == 1)
    {
        property_set(MIPI_HDMI_STATUS, MIPI_HDMI_STATUS_ON);
    }else{
        property_set(MIPI_HDMI_STATUS,MIPI_HDMI_STATUS_OFF);
    }
}
bool CameraProvider::HotplugThread::threadLoop() {
    // Initialize inotify descriptors if needed.
    if (!mIsInitialized && !initialize()) {
        close(mPipeFd[0]);
        close(mPipeFd[1]);
        return false;
    }

    struct pollfd fds[2];
    fds[0].fd = mPipeFd[0];
    fds[0].events = POLLIN;

    fds[1].fd = mFd;
    fds[1].events = POLLPRI;
    struct v4l2_event ev;
    CLEAR(ev);

    if (poll(fds, 2, 5000) < 0) {
        ALOGD("%d: poll failed: %s\n", mFd, strerror(errno));
        return false;
    }
    if (fds[0].revents & POLLIN) {
        ALOGD("%d: quit message received\n", mFd);
        return false;
    }
    if (fds[1].revents & POLLPRI) {
        if (ioctl(fds[1].fd, VIDIOC_DQEVENT, &ev) == 0) {
            if (mMipiCameraId == -1)
            {
                std::string hdmiid = getHdmiID();
                if (hdmiid != "")
                {
                    mMipiCameraId = atoi(hdmiid.c_str());
                }
            }
            std::string numbers = getMipiCameraNumbers();
            int cameraNum = atoi(numbers.c_str());
            switch (ev.type) {
                case V4L2_EVENT_SOURCE_CHANGE:
                    {
                        ALOGD("%d: V4L2_EVENT_SOURCE_CHANGE cameraNum=%d hdmi id:%d status=:%d \n", mFd,
                         cameraNum,mMipiCameraId,HDMI_PLUG_IN);
                        updateMipiHdmiStatusToProp(HDMI_PLUG_IN);
                        sCameraDeviceStatusChange(mParent,mMipiCameraId,(int)CameraDeviceStatus::PRESENT);
                    }
                    break;
                case RK_HDMIRX_V4L2_EVENT_SIGNAL_LOST:
                    {
                        ALOGD("%d: RK_HDMIRX_V4L2_EVENT_SIGNAL_LOST cameraNum=%d hdmi id:%d status=:%d \n",
                         mFd, cameraNum -1,mMipiCameraId,HDMI_PLUG_OFF);
                        sCameraDeviceStatusChange(mParent,mMipiCameraId,(int)CameraDeviceStatus::NOT_PRESENT);
                        updateMipiHdmiStatusToProp(HDMI_PLUG_OFF);
                    }
                    break;
                case V4L2_EVENT_CTRL:{
                        struct v4l2_event_ctrl* ctrl =(struct v4l2_event_ctrl*) &(ev.u);
                        ALOGD("%d:V4L2_EVENT_CTRL event cameraNum=%d hdmi id:%d status=:%d \n", mFd,
                            ctrl->value == HDMI_PLUG_IN ? cameraNum: cameraNum-1,
                            mMipiCameraId,
                            ctrl->value);
                        if(ctrl->value == 1){
                            updateMipiHdmiStatusToProp(HDMI_PLUG_IN);
                            sCameraDeviceStatusChange(mParent,mMipiCameraId,(int)CameraDeviceStatus::PRESENT);
                        }else{
                            sCameraDeviceStatusChange(mParent,mMipiCameraId,(int)CameraDeviceStatus::NOT_PRESENT);
                            updateMipiHdmiStatusToProp(HDMI_PLUG_OFF);
                        }
                    }
                    break;
                default:
                    ALOGD("%d: unknown event :%d\n", mFd,ev.type);
                    break;
            }
        } else {
            ALOGD("%d: VIDIOC_DQEVENT failed: %s\n",mFd, strerror(errno));
        }
    }
    return true;
}
// End CameraProvider::HotplugThread functions

}  // namespace implementation
}  // namespace provider
}  // namespace camera
}  // namespace hardware
}  // namespace android
