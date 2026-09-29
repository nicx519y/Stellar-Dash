# XORA 待机动画候选

四个 GIF 均为 320 × 172、6 秒、72 帧、无限循环。GIF 延时依次为 80 / 80 / 90 ms，平均精确 12 FPS。用浏览器打开同目录 `index.html` 可同时预览、同步重播及切换原始尺寸。

1. `01-mechanical-assembly.gif`：机械拼装，冷银与冰蓝。
2. `02-reactor-awakening.gif`：核心觉醒，按钮点火与能量环。
3. `03-neon-arcade.gif`：霓虹街机，青紫残影与弹性组合。
4. `04-titanium-fold.gif`：钛金折叠，分片翻转与香槟金扫光。

形状读取自 `../../application/www/public/images/xora-mono-slate.svg`，保持原 XORA 字形，以本地矢量逐帧渲染制作。没有上传素材、发布、写入设备或替换现有默认图。

尺寸依据：`../../application/www/components/screen-standby-preview.tsx`；当前固件 JPEG 序列上限依据：`../../application/Inc/webconfig/configs/user_image_format.hpp`（180 帧、0x17f000 字节）。GIF 是供挑选及后续导入的源文件，设备实际安装时由 WebConfig 转换为 JPEG 序列。

验证：Pillow 逐帧解码、尺寸/循环/时长检查，WebConfig 实际 gifuct-js 解码和时间采样函数检查，现有 JPEG payload 契约验证。`validation.json` 和 `compatibility.json` 记录结果。JPEG 容量使用本地 sharp 质量 82 估算，浏览器编码结果可能不同；未实机验收。

复现（仓库根目录）：`node output/xora-standby-concepts/render.cjs`，然后 `python output/xora-standby-concepts/encode.py`，最后 `node output/xora-standby-concepts/verify.cjs`。依赖使用仓库现有 sharp / gifuct-js / sucrase 与本机 Pillow。
