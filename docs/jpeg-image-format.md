# XORA JPEG 背景图片

新上传的静态图、GIF 在浏览器转为 320 × 172 baseline JPEG（Canvas quality 0.82），
设备使用 STM32H750 JPEG 硬件解码和 DMA2D YCbCr→RGB565 转换。
这是有损逐帧压缩，不是 GIF 解码，也不包含视频帧间压缩。

## 容量和时间轴

不改变 QSPI 分区，使用现有 USER_IMAGE 区域。扣除 4096 字节头部后的 payload
上限为 0x17F000（1,568,768 字节）。GIF 按原时间轴每秒采样 12 次，保留停留时间；
当前设备最多 180 个播放时刻（15 秒），相同 JPEG 字节共享数据。网页先查询设备容量；完整转换后若超过字节或帧数上限，
报错且阻止安装，不截断、不加速，也不采用网页固定容量兜底。最后不足一个采样周期的时长向上取整。
实际容量取决于图像复杂度，不能保证所有图片都能存 180 个不同帧。

## UIMG v5

复用 UIMG v4 的 100 字节字段布局和 4096 字节头区：version=5，format=3，
width=320，height=172，frame_count=1..180，fps=静态 0 / 动画 12（兼容旧 6 FPS 文件），
frame_size=110080（解码大小），frames_offset=4096，total_size=压缩 payload 长度。
旧 frame_offsets[12] 全部为零；id、payload CRC32、header CRC32 的偏移仍为 76、92、96。
网页文件头区未使用字节为零，固件持久化只写有效结构，剩余为擦除态。

payload 前 8 字节：ASCII JSEQ、LE uint16 帧数、uint16 零。
随后每个播放时刻用 8 字节记录 LE uint32 offset、length（相对 payload）。
首个 JPEG 紧跟完整目录，各唯一帧以出现顺序连续排列并补零到 4 字节对齐；
重复条目必须精确引用之前出现过的 offset/length。禁止重叠、向前引用和尾部垃圾。
JPEG 要求 SOF0、8-bit、3 分量 YCbCr、4:4:4 / 4:2:2 / 4:2:0，包含量化及 Huffman
表、单个交错扫描和 EOI。头部校验不等价于完整熵解码；损坏熵流由硬件错误或超时结束。

WebHID 保留既有 22 字节 BEGIN 和传输版本 3，imageType=2 表示 JPEG。
请求 0x34 的第二字节为 3 时返回 88 字节 catalog v5（offset 64 = 5）。
旧 82 字节结构不变，offset 82 为 LE uint32 可用 payload 字节数（由分区大小减头区计算），
offset 86 为 uint8 JPEG 帧数上限，offset 87 为 uint8 最高动画 FPS（12）。
flag bit 3 宣告 JPEG 能力，maxUserFrames=12 仍表示旧 RGB565 上限；旧请求继续返回旧长度。
新网页在转换前和安装事务内分别查询容量；擦除前校验容量、JPEG 和能力位。
容量未知、帧数或字节超限均阻止 BEGIN；旧固件需升级。
设备 COMMIT 依次检查流 CRC、Flash 回读 CRC、JPEG 目录及头部，最后写入有效 UIMG 头。
旧 v3/v4 继续可读，备份导入导出保留格式；旧图库需重新上传原图才能获得压缩收益。

## 播放和资源

JPEG FIFO 由主循环分批调用 HAL IRQ 服务函数处理（JPEG NVIC 关闭，不占 MDMA）。
每次至多 32 次 FIFO 服务；使用 7680 字节、32 字节对齐的 D2 MCU 行缓冲。
DMA2D 异步把一 MCU 行写入现有 LCD RGB565 帧缓冲，等待完成并释放 HAL 锁后继续。
最后一行裁到 172 像素高；维护 CPU/DMA 的 cache 一致性。完整帧就绪后才提交 SPI 刷屏。
150 ms 超时、错误或取消会终止转换并丢弃脏标记。12 FPS 按时间戳选帧，不追赶播放旧帧。
解码期间 LCD UI 更新暂缓，游戏输入采样中断不由 JPEG ISR 抢占；实际输入时序仍须实机验证。

HAL JPEG/DMA2D 文件来自 ST stm32h7xx-hal-driver v1.11.3，与现有 HAL 版本一致。
为避免 AXI SRAM 溢出，链接器把 codec 和图片命令处理对象的代码/常量放入空闲 ITCM，
包含在既有 startup vector-span 拷贝范围中；向量入口地址不变，QSPI 写入路径仍在 RAM。
无需修改烧录脚本、保护位或 Flash 分区。
参考：[ST AN4996](https://www.st.com/resource/en/application_note/dm00356635-hardware-jpeg-codec-peripheral-in-stm32f7677xxx-and-stm32h743534555475750a3b3b0xx-microcontrollers-stmicroelectronics.pdf)。

## 验证入口和限制

- Web：jpeg-image、gif-timing、mock-device-transport、uimg-v3、webhid-protocol 定向测试，typecheck，build:hosted。
- 服务端：image-gallery 测试。
- 固件主机：`python -m unittest tools.tests.test_user_image_qspi_reliability tools.tests.test_jpeg_player`。
  前者用生产 handler 验证 Flash 失败/CRC/最后提交头，后者用生产 player 验证连续行、
  DMA HAL 锁、三种采样、末行裁剪、完成前不发布、错误/取消/超时。
- 无锁编译：`make -C application HBOX_SECURE_BOOT_REQUIRED=0`。

主机模拟无法证明硬件像素颜色、DMA2D 裁剪和 12 FPS 帧耗时。实际烧录后仍需验证
三种采样图像、循环动画、从屏保唤醒、连续上传及控制器输入时序。本次实现不自动烧录。

2026-09-29 浏览器复查：共享实现明确使用 `.cjs` / `.d.cts`，避免 Next.js 将
`module.exports` 留入页面和 Worker 的 ESM 代码。Hosted 重新打包及真实浏览器
Worker 验证通过：两帧各停留 500 ms 的 GIF 得到 6 个播放时刻、2 个唯一 JPEG，
浏览器重解码颜色检查通过，页面无未捕获异常。静态预览的账户 API 404 和未授权
WebHID 提示属于未连接后端/设备的预览环境，不代表硬件验收。

12 FPS 定向测试覆盖整秒及变长停留、转换容量恰好可容纳/差一字节、帧数超限、容量缺失、
安装前重新查询容量、拒绝时未发 BEGIN，以及生产目录响应通过外层 RPC 校验。

GIF 安装 CSP 回归：安装时复用已下载的原图 Blob，不使用 fetch 读取预览 blob URL。
生产 CSP 的 connect-src 保持 self；10,119,297 字节图库 GIF 在该策略下转换为
39 帧、12 FPS、650,744 字节 JPEG payload，差一字节容量仍被拒绝。此检查验证网页
下载/转换/预览，未进行设备写入。
