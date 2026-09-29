# XORA 三维电影片头候选

本批重新使用 Blender 5.0.1 / Cycles / OptiX 本地三维渲染制作。原 SVG 路径转为带真实厚度与倒角的模型；使用金属微表面、移动实体面光源、程序体积雾、空间尘埃、透视镜头与景深。没有使用 HTML / SVG 扫光动画制作成片。HTML 仅用于播放文件。

- 09 钛银片头：冷银、蓝色逆光、缓慢扫光与拉远。
- 10 黑铬片头：暗黑铬材质、琥珀侧光、较近景起镜。
- 11 离子片头：青蓝金属、蓝紫背光与空间雾。
- 12 香槟金片头：金属暖光、低角度反光与平稳镜头。

设备 GIF 为 320 × 172、96 帧、8 秒无限循环。GIF 延时 80 / 80 / 90 ms 循环，平均精确 12 FPS。640 × 344 渲染原帧保留在 `frames/`，场景保留为 `.blend`；按帧的镜头与灯光调度见 `scene.py`。另保存 640 × 344、12 FPS 的 H.264 视频 `*-master.mp4`，由本地 Blender 内置 FFmpeg 编码；对比页播放视频，保存按钮对应屏幕 GIF。视频尺寸、帧数和帧率检查见 `video-validation.json`。

造型来自 `../../application/www/public/images/xora-mono-slate.svg`。没有上传、发布或写入设备；没有更换现有默认图片。

参考的是电影片头的镜头/材质方法，并非复刻其镜头或声称达到电影制作团队的质量：

- [Alien / Art of the Title](https://d.cdnv2.artofthetitle.com/title/alien/)：延迟揭示与字形辨识节奏。
- [The Girl with the Dragon Tattoo / Art of the Title](https://www.artofthetitle.com/title/the-girl-with-the-dragon-tattoo/)：黑色材质、局部高光与 CG 造型的访谈参考。本批没有制作其流体模拟。

本地验证包含 GIF 逐帧解码、尺寸/延时/循环、现有 WebConfig gifuct-js 和实际时间采样函数、JPEG payload 契约。容量为 sharp 质量 82 的本地估算，浏览器编码可能不同；未实机验收。

复现：仓库根目录执行 `python output/xora-cinema-3d/run.py final`，再执行 `python output/xora-cinema-3d/encode.py` 和 `node output/xora-cinema-3d/verify.cjs`。当前脚本使用已安装的 Blender 路径、已有 RTX / OptiX 和仓库依赖。
