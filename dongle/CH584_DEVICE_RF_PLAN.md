# CH584 设备端射频实现方案（配合当前 Dongle）

本文档定义 CH584 发射端（手柄设备端）如何与当前 `dongle` 固件对齐，实现稳定的 2.4G 输入链路。

目标：
- 与当前接收端协议完全兼容（`rf_protocol` + `rf_link_stub` 现状）。
- 支持配对持久化、快速重连、自动跳频、抗干扰与重传。
- 保障输入低延迟和断链可恢复。

## 1. 对齐基线（必须一致）

与接收端一致的协议定义：
- 协议版本：`RF_PROTO_VERSION = 1`
- 帧格式：`8B header + payload + 1B checksum(sum8)`
- `header` 字段：
  - `version/type/flags/seq/ack_seq/ack_bits/hop_idx/epoch_lsb`
- 最大 payload：`24B`

包类型（设备端要支持）：
- 上行主路径：`ADV_REQ`、`PAIR_CONFIRM`、`CONN_ACK`、`INPUT_DATA`
- 下行处理：`ADV_RSP`、`CONN_REQ`、`HEARTBEAT`、`UNBIND`

## 2. 设备端状态机

建议状态：
- `DEV_IDLE`：未广播、低功耗待机。
- `DEV_PAIR_ADV`：发送 `ADV_REQ`，等待 `ADV_RSP`。
- `DEV_PAIR_CONFIRM`：收到 `ADV_RSP` 后发送 `PAIR_CONFIRM`。
- `DEV_BONDED_WAIT_CONN`：已绑定，监听/等待 `CONN_REQ`。
- `DEV_CONNECTED`：稳定连接，上报 `INPUT_DATA`，响应心跳。

核心流转：
1. 无绑定时进入 `DEV_PAIR_ADV` 周期广播。
2. 收到合法 `ADV_RSP` 后，保存 `nonce_local/hop_seed`，回 `PAIR_CONFIRM`。
3. 配对成功后持久化 bond，进入 `DEV_BONDED_WAIT_CONN`。
4. 收到合法 `CONN_REQ` 后回复 `CONN_ACK`，进入 `DEV_CONNECTED`。
5. 超时未收到下行（`CONN_REQ/HEARTBEAT`）则回 `DEV_BONDED_WAIT_CONN`。
6. 收到 `UNBIND` 清空 bond，回 `DEV_IDLE`。

## 3. Bond 数据与持久化

设备端 bond 结构建议：

```c
typedef struct {
    uint8_t  valid;
    uint32_t peer_uid;      // dongle 侧 UID（可约定固定或会话写入）
    uint32_t nonce_local;   // 来自 ADV_RSP[0..3]
    uint32_t nonce_peer;    // 本端在 PAIR_CONFIRM 中生成
    uint8_t  hop_seed;      // 来自 ADV_RSP[4]
    uint32_t auth_tag;      // calc_auth_tag(...)
    uint32_t crc32;
} dev_bond_store_t;
```

持久化策略：
- 写入时机：`PAIR_CONFIRM` 成功后立即写入 Flash/EEPROM。
- 上电校验：`magic/version/length/crc32/auth_tag` 全部通过才认为有效。
- 无效记录处理：直接清空并回到可配对态。

`auth_tag` 计算必须与接收端一致：

```c
x = peer_uid ^ nonce_local ^ rotl(nonce_peer, 7);
x ^= ((uint32_t)hop_seed << 24) | ((uint32_t)hop_seed << 8);
x ^= 0x6D5A56A5u;
```

> 注意：连接阶段双方使用的参数顺序不同（见第 4 节），这是接收端当前实现要求。

## 4. 握手与鉴权细节（按当前接收端实现）

### 4.1 配对阶段

1) 设备发 `ADV_REQ`：
- payload：`[peer_uid:4]`

2) 收 `ADV_RSP`（payload 9B）：
- `[nonce_local:4][hop_seed:1][feature:1][channel_cnt:1][phy:1][rsv:1]`

3) 设备发 `PAIR_CONFIRM`：
- payload：`[nonce_peer:4][echo_nonce_local:4]`

### 4.2 连接阶段

1) 收 `CONN_REQ`（payload 9B）：
- `[peer_uid:4][hop_seed:1][req_tag:4]`
- `req_tag` 校验公式（设备端）：
  - `calc_auth_tag(peer_uid, nonce_local, nonce_peer, hop_seed)`

2) 发 `CONN_ACK`（payload 8B）：
- `[peer_uid:4][ack_tag:4]`
- `ack_tag` 必须按当前接收端期望：
  - `calc_auth_tag(peer_uid, nonce_peer, nonce_local, hop_seed)`

## 5. 跳频、扫描、重传与功率控制

### 5.1 自动跳频（必做）

建议设备端与接收端同一信道表（16 通道）：

```text
[2,5,8,11,14,17,20,23,26,29,32,35,38,12,19,31]
```

跳频索引计算：
- `hop_idx = (hop_seed + seq) % channel_count`
- 发送前切到 `hop_idx` 对应物理信道。
- 接收包时校验 `hop_idx` 与期望值（允许 1 个前序窗口容差，提升鲁棒性）。

### 5.2 连接扫描（建议）

- `DEV_BONDED_WAIT_CONN` 中周期轮询信道监听 `CONN_REQ`。
- 初始游标从 `hop_seed` 开始，按表循环，窗口建议 `1~3ms/信道`。

### 5.3 重传策略（必做）

- `CONN_ACK`：发送失败或未见后续有效下行时，最多重发 `2~3` 次并可换信道。
- `INPUT_DATA`：仅对关键控制位变化帧做有限重发（避免吞吐抖动）。
- 去重：对最近 `seq` 做窗口去重，避免重复帧二次生效。

### 5.4 功率控制（建议）

- 统计窗口（例如 200ms）：
  - `rx_ok`、`rx_fail`、`tx_fail`
- 判定策略：
  - `fail_score` 高于阈值 -> 升功率一级
  - 长时间稳定 -> 降功率一级
- 功率范围建议 4 档（与 dongle 当前抽象一致：`0~3`）。

## 6. INPUT_DATA 上报规范

payload 固定 15B（小端）：
- `byte0`: `seq`
- `byte1`: `flags`
- `byte2..3`: `buttons16`
- `byte4`: `dpad`
- `byte5`: `lt`
- `byte6`: `rt`
- `byte7..8`: `lx`
- `byte9..10`: `ly`
- `byte11..12`: `rx`
- `byte13..14`: `ry`

上报节拍建议：
- 常规 `1ms`（1000Hz）起步，稳定后可提升。
- 当输入无变化时可降频+心跳保活，减少空口占用。

## 7. CH584 工程模块划分建议

- `dev_rf_phy.c`：CH584 射频初始化、收发 DMA、信道切换、功率设置。
- `dev_rf_protocol.c`：帧编解码、checksum、包类型路由。
- `dev_link_sm.c`：配对/连接状态机、超时器、重连与重传。
- `dev_bond_store.c`：bond 持久化与完整性校验。
- `dev_input_pack.c`：按键/摇杆采样与 15B payload 打包。

接口建议：
- `dev_link_init()`
- `dev_link_poll()`
- `dev_link_on_input(raw_state)`
- `dev_link_unbind()`

## 8. 异常与恢复策略

- 鉴权失败（`peer_uid/hop_seed/tag` 任一不匹配）：丢包并计数，不更新连接活跃时间。
- 连续超时：`DEV_CONNECTED -> DEV_BONDED_WAIT_CONN`，触发扫描重连。
- 高频干扰：自动升功率 + 缩短扫描轮询周期 + 提高控制帧重传次数。
- 收到 `UNBIND`：立即清空 bond 并停止自动重连。

## 9. 联调检查清单

- 配对后断电重启：设备与 dongle 都能自动恢复绑定并快速重连。
- 干扰环境下：连接建立成功率、断链恢复时间、输入丢包率达标。
- 跳频一致性：双方 `hop_idx` 计算和信道表完全一致。
- 鉴权一致性：`CONN_REQ` 与 `CONN_ACK` 的 tag 参数顺序无误。
- 去重有效：重复包不会导致按键抖动或重复触发。

## 10. 参数建议（首版）

- 扫描驻留：`2ms/信道`
- `CONN_REQ`/`CONN_ACK` 重传：`2` 次
- 心跳周期：`8~16ms`
- 断链判定：`30ms` 无有效下行
- 质量评估窗口：`200ms`

以上参数先用于首版 bring-up，后续根据实测（办公室/展会强干扰场景）再收敛。
