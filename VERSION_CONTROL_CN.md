# 相机 AIDL 版本控制

## 概述

此相机实现通过条件编译和构建系统配置，支持 Android 14 (V2 AIDL) 和 Android 15+ (V3 AIDL) 接口，涵盖所有相机设备类型：
- 内部相机设备
- 外部相机设备
- HDMI 相机设备
- 虚拟相机设备

## 版本选择

### 方法 1：使用 CAMERA_AIDL_VERSION（推荐）

在您的 `BoardConfig.mk` 中添加：

```makefile
# 为 Android 15+ 启用 V3 AIDL 支持
CAMERA_AIDL_VERSION := v3

# 或使用默认 V2 支持（无需指定）
# CAMERA_AIDL_VERSION := v2
```

### 方法 2：使用 soong_config_set

在您的 `BoardConfig.mk` 中添加：

```makefile
# 为 Android 15+ 启用 V3 支持
$(call soong_config_set,camera_rockchip,camera_aidl_version,v3)

# 为 Android 14 启用 V2 支持 - 默认
$(call soong_config_set,camera_rockchip,camera_aidl_version,v2)
```

### 方法 3：自动版本检测

在您的 `BoardConfig.mk` 中添加：

```makefile
# 自动检测 Android 版本并设置相应的 AIDL 版本
ifeq ($(android_version),15)
    CAMERA_AIDL_VERSION := v3
else ifeq ($(android_version),16)
    CAMERA_AIDL_VERSION := v3
else
    CAMERA_AIDL_VERSION := v2
endif
```

## 构建系统配置

`Android.bp` 文件使用 `soong_config_string_variable` 为所有相机模块处理版本选择：

```bp
soong_config_string_variable {
    name: "camera_aidl_version",
    values: [
        "v2",
        "v3",
    ],
}

camera_rockchip_cc_defaults {
    name: "rockchip_camera_device_aidl_lib_default",
    soong_config_variables: {
        camera_aidl_version: {
            conditions_default: {
                # V2 依赖项（默认）
                shared_libs: [
                    "android.hardware.camera.device-V2-ndk",
                ],
            },
            v2: {
                # V2 依赖项
                shared_libs: [
                    "android.hardware.camera.device-V2-ndk",
                ],
            },
            v3: {
                # V3 依赖项，带条件编译
                cflags: ["-DCAMERA_V3_SUPPORT"],
                shared_libs: [
                    "android.hardware.camera.device-V3-ndk",
                ],
            },
        },
    }
}
```

## 支持的相机模块

所有相机设备模块都使用相同的版本控制机制：

- `camera.device-internal-impl-rk` - 内部相机设备
- `camera.device-external-impl-rk` - 外部相机设备（USB 等）
- `camera.device-hdmi-impl-rk` - HDMI 相机设备
- `camera.device-virtual-impl-rk` - 虚拟相机设备

## 代码实现

### 条件编译

V3 特定代码在所有设备类中使用 `#ifdef CAMERA_V3_SUPPORT` 包装：

```cpp
#ifdef CAMERA_V3_SUPPORT
// V3 AIDL 方法实现
ScopedAStatus CameraDevice::constructDefaultRequestSettings(
        RequestTemplate in_type, CameraMetadata* _aidl_return) {
    // V3 实现
}

ScopedAStatus CameraDevice::getSessionCharacteristics(
        const StreamConfiguration& in_sessionConfig, CameraMetadata* _aidl_return) {
    // V3 实现
}
#endif // CAMERA_V3_SUPPORT
```

### V3 方法

当为所有相机类型定义 `CAMERA_V3_SUPPORT` 时，实现以下 V3 方法：

- `constructDefaultRequestSettings` - 带 CameraMetadata 返回的 V3 签名
- `isStreamCombinationWithSettingsSupported` - 增强的流验证
- `getSessionCharacteristics` - 会话特定特征
- `configureStreamsV2` - 增强的流配置

## 兼容性

- **Android 14**：为所有相机类型使用 V2 AIDL 接口（默认）
- **Android 15+**：当 `CAMERA_AIDL_VERSION := v3` 时为所有相机类型使用 V3 AIDL 接口

## 测试

### VTS 测试

V3 实现包括所有相机模块的 VTS 测试合规性：
- `getSessionCharacteristics` 仅返回供应商特定标签
- 对不支持操作的正确错误处理
- 内存管理修复以提高稳定性

### 构建验证

```bash
# 使用 V2 支持构建所有相机模块（默认）
m camera.device-internal-impl-rk camera.device-external-impl-rk camera.device-hdmi-impl-rk camera.device-virtual-impl-rk

# 使用 V3 支持构建所有相机模块
CAMERA_AIDL_VERSION=v3 m camera.device-internal-impl-rk camera.device-external-impl-rk camera.device-hdmi-impl-rk camera.device-virtual-impl-rk
```

## 故障排除

### 常见问题

1. **链接器错误**：确保所有模块的 AIDL 库依赖项正确
2. **VTS 失败**：检查所有设备类型的 `getSessionCharacteristics` 实现
3. **内存崩溃**：验证所有相机模块 V3 方法中的元数据处理

### 调试标志

在 `BoardConfig.mk` 中添加调试：

```makefile
# 强制特定版本进行测试
CAMERA_AIDL_VERSION := v3
``` 