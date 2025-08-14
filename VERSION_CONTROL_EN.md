# Camera AIDL Version Control

## Overview

This camera implementation supports both Android 14 (V2 AIDL) and Android 15+ (V3 AIDL) interfaces
through conditional compilation and build system configuration across all camera device types:
- Internal camera devices
- External camera devices
- HDMI camera devices
- Virtual camera devices

## Version Selection

### Method 1: Using CAMERA_AIDL_VERSION (Recommended)

Add to your `BoardConfig.mk`:

```makefile
# Enable V3 AIDL support for Android 15+
CAMERA_AIDL_VERSION := v3

# Or use default V2 support (no need to specify)
# CAMERA_AIDL_VERSION := v2
```

### Method 2: Using soong_config_set

Add to your `BoardConfig.mk`:

```makefile
# For Android 15+ (V3 support)
$(call soong_config_set,camera_rockchip,camera_aidl_version,v3)

# For Android 14 (V2 support) - default
$(call soong_config_set,camera_rockchip,camera_aidl_version,v2)
```

### Method 3: Automatic Version Detection

Add to your `BoardConfig.mk`:

```makefile
# Automatically detect Android version and set appropriate AIDL version
ifeq ($(android_version),15)
    CAMERA_AIDL_VERSION := v3
else ifeq ($(android_version),16)
    CAMERA_AIDL_VERSION := v3
else
    CAMERA_AIDL_VERSION := v2
endif
```

## Build System Configuration

The `Android.bp` file uses `soong_config_string_variable` to handle version selection for all camera modules:

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
                # V2 dependencies (default)
                shared_libs: [
                    "android.hardware.camera.device-V2-ndk",
                ],
            },
            v2: {
                # V2 dependencies
                shared_libs: [
                    "android.hardware.camera.device-V2-ndk",
                ],
            },
            v3: {
                # V3 dependencies with conditional compilation
                cflags: ["-DCAMERA_V3_SUPPORT"],
                shared_libs: [
                    "android.hardware.camera.device-V3-ndk",
                ],
            },
        },
    }
}
```

## Supported Camera Modules

All camera device modules use the same version control mechanism:

- `camera.device-internal-impl-rk` - Internal camera devices
- `camera.device-external-impl-rk` - External camera devices (USB, etc.)
- `camera.device-hdmi-impl-rk` - HDMI camera devices
- `camera.device-virtual-impl-rk` - Virtual camera devices

## Code Implementation

### Conditional Compilation

V3-specific code is wrapped with `#ifdef CAMERA_V3_SUPPORT` in all device classes:

```cpp
#ifdef CAMERA_V3_SUPPORT
// V3 AIDL method implementations
ScopedAStatus CameraDevice::constructDefaultRequestSettings(
        RequestTemplate in_type, CameraMetadata* _aidl_return) {
    // V3 implementation
}

ScopedAStatus CameraDevice::getSessionCharacteristics(
        const StreamConfiguration& in_sessionConfig, CameraMetadata* _aidl_return) {
    // V3 implementation
}
#endif // CAMERA_V3_SUPPORT
```

### V3 Methods

The following V3 methods are implemented when `CAMERA_V3_SUPPORT` is defined for all camera types:

- `constructDefaultRequestSettings` - V3 signature with CameraMetadata return
- `isStreamCombinationWithSettingsSupported` - Enhanced stream validation
- `getSessionCharacteristics` - Session-specific characteristics
- `configureStreamsV2` - Enhanced stream configuration

## Compatibility

- **Android 14**: Uses V2 AIDL interfaces (default) for all camera types
- **Android 15+**: Uses V3 AIDL interfaces when `CAMERA_AIDL_VERSION := v3` for all camera types

## Testing

### VTS Tests

V3 implementations include VTS test compliance for all camera modules:
- `getSessionCharacteristics` returns only vendor-specific tags
- Proper error handling for unsupported operations
- Memory management fixes for stability

### Build Verification

```bash
# Build all camera modules with V2 support (default)
m camera.device-internal-impl-rk camera.device-external-impl-rk camera.device-hdmi-impl-rk camera.device-virtual-impl-rk

# Build all camera modules with V3 support
CAMERA_AIDL_VERSION=v3 m camera.device-internal-impl-rk camera.device-external-impl-rk camera.device-hdmi-impl-rk camera.device-virtual-impl-rk
```

## Troubleshooting

### Common Issues

1. **Linker errors**: Ensure proper AIDL library dependencies for all modules
2. **VTS failures**: Check `getSessionCharacteristics` implementation across all device types
3. **Memory crashes**: Verify metadata handling in V3 methods for all camera modules

### Debug Flags

Add to `BoardConfig.mk` for debugging:

```makefile
# Force specific version for testing
CAMERA_AIDL_VERSION := v3
``` 