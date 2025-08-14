# 相机 AIDL 版本控制文档 / Camera AIDL Version Control Documentation

## 文档版本 / Document Versions

### 中文版本 / Chinese Version
- **文件**: `VERSION_CONTROL_CN.md`
- **描述**: 相机 AIDL 版本控制的完整中文说明文档
- **Description**: Complete Chinese documentation for camera AIDL version control

### 英文版本 / English Version
- **文件**: `VERSION_CONTROL_EN.md`
- **描述**: 相机 AIDL 版本控制的完整英文说明文档
- **Description**: Complete English documentation for camera AIDL version control

## 内容概述 / Content Overview

两个文档都包含以下内容 / Both documents contain the following content:

- **版本选择方法** / Version Selection Methods
- **构建系统配置** / Build System Configuration
- **支持的相机模块** / Supported Camera Modules
- **代码实现说明** / Code Implementation Details
- **兼容性信息** / Compatibility Information
- **测试指南** / Testing Guidelines
- **故障排除** / Troubleshooting

## 快速开始 / Quick Start

### 启用 V3 支持 / Enable V3 Support
```makefile
# 在 BoardConfig.mk 中添加 / Add to BoardConfig.mk
CAMERA_AIDL_VERSION := v3
```

### 使用默认 V2 支持 / Use Default V2 Support
```makefile
# 无需指定，v2 是默认值 / No need to specify, v2 is default
# CAMERA_AIDL_VERSION := v2
``` 