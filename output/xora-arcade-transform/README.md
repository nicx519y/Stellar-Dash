# XORA 街机控制器变形版

开头以完整、可识别的街机控制器展示约两秒：左侧球头摇杆、右侧八颗街机按钮、金属面板与矩形机身。变形时各部件从原位置直接运动，避免再次聚拢为无明确造型的零件团。

对应关系：十块面板翻折成 X / R / A；八颗按钮重组成 O 的八段外圈；摇杆球头移动到 O 的中央，杆体收回；机壳随后折向后方。面板与按键的形变、位置和姿态都烘焙在同一组对象上。末尾为完整 XORA 与金属扫光，最后淡出循环。

- `14-arcade-to-xora.gif`：320 × 172、120 帧、10 秒、无限循环。80 / 80 / 90 ms 延时循环实现平均精确 12 FPS。
- `14-arcade-to-xora-master.mp4`：640 × 344、12 FPS、10 秒，无音频。
- `index.html`：本地视频预览及设备尺寸 GIF。
- `14-arcade-to-xora.blend`：可播放的完整关键帧动画。

使用本地 Blender 5.0.1 / Cycles / OptiX / 48 samples；原 Logo 来自 `../../application/www/public/images/xora-mono-slate.svg`。本目录的 `base_scene.py` 和 `mechanics.py` 为之前场景与几何工具的独立副本，不修改之前的作品。未上传、发布、访问设备或替换默认待机画面。

验证包括：GIF 逐帧解码和尺寸/时长/循环、WebConfig 实际解码与 12 FPS 采样函数、JPEG payload 契约、视频尺寸/帧数/帧率、场景中完整控制器的两秒停留及同一组物体从控制器到 Logo 的连续变化。容量为本地 sharp JPEG 质量 82 估算，浏览器编码可能不同；未实机验收。

仓库根目录复现：`python output/xora-arcade-transform/run.py final`，随后执行同目录 `encode.py` 和 `verify.cjs`；视频编码与三维场景检查分别使用 Blender 后台执行 `export_video.py`、`check_video.py`、`check_scene.py`。
