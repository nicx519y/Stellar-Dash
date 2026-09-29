# XORA 流光显现 · 四个本地方案

延续上一批编号：05 银白斜扫、06 冰蓝横扫、07 青紫流光、08 香槟金展开。双击 `index.html` 同步预览或切换原始尺寸。

规格：320 × 172，96 帧，8 秒，无限循环。GIF 延时以 80 / 80 / 90 ms 循环，平均精确 12 FPS。光带缓慢显现 Logo，停留时轻扫反光，最后淡出至黑色背景。

原字形来自 `../../application/www/public/images/xora-mono-slate.svg`。全部在本地使用 SVG 矢量渲染和 GIF 编码；无上传、无设备访问、无默认待机图替换。保留前一批方案。

`validation.json`：逐帧解码、尺寸、延时、循环和时长检查。`compatibility.json`：现有 WebConfig gifuct-js 解码与时间采样函数检查，以及 JPEG payload 契约验证。本地 sharp JPEG 质量 82 的容量仅为估算，浏览器编码可能不同；未实机验收。

在仓库根目录依次执行 `node output/xora-light-reveal/render.cjs`、`python output/xora-light-reveal/encode.py`、`node output/xora-light-reveal/verify.cjs` 可复现。
