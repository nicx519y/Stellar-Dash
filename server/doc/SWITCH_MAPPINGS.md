# 轴体 ADC 映射库

服务器使用独立的 `switch_mappings.sqlite3` 保存逻辑轴体目录和不可变曲线版本。普通目录只返回与当前设备会话中的 `productId + pcbRevision + hardwareVersion` 完全匹配、且已发布的版本；下架只移除公开可见性，不删除历史版本。管理员的删除操作则会物理删除目录、全部历史版本和封面图。

## 普通设备接口

- `GET /api/switch-mappings`：需要设备会话 `config.read`，返回全部兼容的已发布目录及图片状态。设备当前映射由 WebConfig 按 `revisionId` 合并，不依赖服务器保存设备选择状态。
- `GET /api/switch-mappings/:catalogId`：需要设备会话 `config.read`，返回当前已发布版本、完整曲线和 SHA-256。
- `GET /api/switch-mappings/:catalogId/image`：需要设备会话 `config.read`，返回可选的 JPEG、PNG 或 WebP 轴体图片。

设备写入不经过服务器接口，而是由 WebConfig 校验下载内容后调用固件 `ms_install_mapping`；该命令要求设备会话 `config.write`。

## 管理接口

- `POST /api/admin/switch-mappings/drafts`
- `POST /api/admin/switch-mappings/:catalogId/revisions/:revisionId/publish`
- `POST /api/admin/switch-mappings/:catalogId/unpublish`
- `PATCH /api/admin/switch-mappings/:catalogId`
- `PUT /api/admin/switch-mappings/:catalogId/image`：上传不超过 2 MiB 的轴体图片。
- `DELETE /api/admin/switch-mappings/:catalogId`：物理删除目录、全部历史版本和封面图。

创建、发布、展示信息修改、图片上传和删除必须同时具有有效设备 Bearer 会话及管理员邮箱 Cookie；创建和发布还会从设备会话绑定兼容硬件。下架只要求管理员邮箱会话。

WebConfig 首次进入页面时并行读取设备唯一映射和服务器目录，只下载目录元数据与图片。用户选中非当前轴体后，才下载该目录的完整曲线、校验 SHA-256 并调用 `ms_install_mapping` 替换设备映射。

## 曲线摘要

设备、WebConfig 和服务器使用相同的 220 字节 `HBOX-ADC-MAP-V1` 小端规范数据计算 SHA-256：16 字节格式标记、16 字节 revision ID、16 字节设备短名称、长度、float32 步长、噪声、采样率，以及零填充到 40 项的 uint32 ADC 数组。
