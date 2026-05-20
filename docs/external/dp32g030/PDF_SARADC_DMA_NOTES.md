# DP32G030 参考手册 PDF 摘录（SARADC / DMA / 音频测频）

> 来源：本目录 [`DP32G030.pdf`](DP32G030.pdf)（WPS 导出，432 页，约 24 MiB）。  
> 本文从 PDF 文本提取与 **方案 B（中断）**、**方案 C（DMA+FIFO）**、**PA8/CH3 线入** 直接相关的段落，并标注手册章节页码（PDF 内「第 N 页」）。

---

## 1. SARADC 概要（§5.19，约第 323–325 页）

| 项目 | 手册说明 |
|------|----------|
| 结构 | 12 bit SAR，逐次逼近 |
| 有效通道 | 14 路；**通道 0–10 为外部模拟输入**（CH3 = 外部通道 3，对应 `SARADC_CH3` / PA8） |
| 最高采样率 | **2.4 Msps**（需配合最短采样窗口等配置） |
| 通道 0–10 | **高速通道**，建立时间短 |
| 通道 13–15 | 片上温度 / 1.2 V 参考 / AVDD/3（低速） |
| 存储 | 各通道寄存器，或 **全通道共用 16 级 FIFO** |
| 触发 | CPU 软触发；或 **12 路外部触发**（TIMERPLUS、PWMPLUS） |
| 平均 | 可 1/2/4/8 次采样取平均（对应 `AVG` / 手册 `SPL_NUM`） |
| 模式 | 单次：遍历所选通道一轮后停；连续：循环直到软件停 |
| DMA | **支持 DMA 读取 FIFO** |
| 中断 | 各通道 EOC、**FIFO 满、FIFO 半满**（共 18 个中断源，见图 5-160） |

---

## 2. 采样时钟（§5.6，约第 72、79 页）

- `CLK_SEL.SARADC_SMPL_CLK_SEL`：`sys_clk` **1/2/4/8 分频**（与 `driver/adc.c` 中 `SYSCON_CLK_SEL_W_SARADC_SMPL_VALUE_DIV2` 一致）。
- 外围总线时钟与系统时钟同频；不用时可关 `DEV_CLK_GATE.SARADC_CLK_GATE`。
- **k5-v5**：`SYSTEM_ConfigureClocks` 后 `sys_clk` 常为 **48 MHz / 2 = 24 MHz**（以实机 `SYSCON` 为准）。  
  音频测频需在 **速率** 与 **建立时间** 之间折中：手册称最高 2.4M，电池路径用 8 次平均 + 15 cycle 窗口会远低于该值。

---

## 3. `ADC_CFG` 关键位（§5.19 寄存器，约第 339 页）

与 extended SVD **一致**，与 k5 `saradc.h` 命名对照：

| 位域 | 手册值 | 含义 |
|------|--------|------|
| `ADC_MEM_MODE` | **0** | **FIFO 模式**（方案 C） |
| | **1** | **通道模式**（当前 `board.c` 电池路径） |
| `DMA_EN` | 0 | CPU 读 FIFO |
| | 1 | **DMA 读 FIFO** |
| `ADC_TRIG` | 0 | CPU 触发（`ADC_START`） |
| | 1 | 外部信号触发 |
| `CONT` | 0/1 | 单次 / 连续采样 |
| `ADC_CH_SEL[15:0]` | bit3=1 | 仅选 **通道 3**（PA8 音频） |

**注意**：手册寄存器名 `ADC_MEM_MD` / `ADC_MD` 在 SVD 中为 `ADC_MEM_MODE` / `CONT`，实现时以 `bsp/dp32g030/saradc.h` 为准。

---

## 4. FIFO（§5.19.4，约第 333–334、344–345 页）

- 深度：**16 级**（满时 `ADC_FIFO_LEVEL` 表示 16 笔数据，见 `ADC_FIFO_STAT` 说明）。
- **FIFO 模式**：未及时读走 → **新数据丢弃**（非覆盖）。
- **通道模式**：未及时读 → **覆盖旧数据**。
- 读出口：`ADC_FIFO_DATA`（**0xA4**），含通道号 + 12 bit 数据（与 SVD 一致）。
- 清 FIFO：`ADC_START.FIFO_CLR` 写 1（SVD 有，k5 `saradc.def` **尚未生成**）。

---

## 5. 外部触发（方案 B 可选，§5.19.4，约第 334 页）

`ADC_TRIG=1` 时，由 `EXTTRIG_SEL`（手册 `ADC_EXTTRIG_SEL`）选择：

| 位 | 信号 |
|----|------|
| [3:0] | `PWM_PLUS0_TRIGGER[3:0]` |
| [7:4] | `PWM_PLUS1_TRIGGER[3:0]` |
| [9:8] | `TIMER_PLUS0_GOAL[1:0]` |
| [11:10] | `TIMER_PLUS1_GOAL[1:0]` |

可用于 **定时均匀采样**（不必每次 EOC 里再 `ADC_Start`）。  
**冲突**：`TIMER_PLUS0` 等亦被背光等占用，需查 `portcon.def` 与现有固件。

---

## 6. DMA 请求映射（§5.20，约第 390–392 页）— 方案 C 关键

手册 **图 5-179**：4 个 DMA 通道；源/目的侧各用 `MS_SEL`/`MD_SEL` 的 **3 bit** 选择下表（**非** HSREQ 编号，而是表内 **对应 bit** 列）：

### 6.1 源侧（MS）— 节选

| `MS_SEL`（3 bit） | 通道 0 / 2 源 | 通道 1 / 3 源 |
|-------------------|---------------|---------------|
| `000` | UART0_RX | UART2_RX / UART0_RX |
| `001` | UART1_RX | UART2_TX / UART1_RX |
| `010` | SPI0_RX | SPI1_RX / SPI0_RX |
| **`011`** | **SARADC** | **SARADC** |
| `100` | TIMER_PLUS0_L | TIMER_PLUS1_L / … |
| `101` | TIMER_PLUS1_L | TIMER_PLUS0_L / … |

- **SARADC 仅出现在源侧**；目的侧表中 SARADC 为 **N/A**（FIFO 数据应 **DMA 写入 SRAM**，`MD_SEL=000` 存储器）。
- **k5-v5 现状**：`DMA_CH0` 已用于 **UART1 RX**（`driver/uart.c`，`MS_SEL=HSREQ_MS1` → 固件值 **2**，对应表中 **`010` SPI0_RX** 或需以实机为准；与手册「UART1_RX=001」的对应关系在 UV-K5 上 **以现有 UART 代码为准**，不要改 CH0）。

### 6.2 方案 C 推荐通道分配（k5-v5）

| DMA 通道 | 建议用途 | SARADC 配置要点 |
|----------|----------|-----------------|
| CH0 | **UART1 RX**（保持） | — |
| **CH1 或 CH2** | **SARADC → SRAM 环缓** | `MSADDR` = `ADC_FIFO_DATA`（0x400BA0A4）；`MS_SEL` = **`011`（值 3）**；`MD_SEL` = `000`（SRAM 递增）；`ADC_MEM_MODE=0`，`DMA_EN=1`，`CONT=1`，`CH_SEL` 仅 CH3 |

固件常量（与 `bsp/dp32g030/dma.h`）：

```c
/* 手册表 011 = SARADC → 使用 HSREQ_MS2（值为 3） */
DMA_CH_MOD_MS_SEL_BITS_HSREQ_MS2   /* 需确认头文件是否已定义，否则 (3u << SHIFT) */
DMA_CH_MOD_MD_SEL_BITS_SRAM        /* 000 */
```

### 6.3 DMA 使能流程（手册 §5.20.4，约第 393–394 页）

1. `DMA_CTR.DMA_EN = 1`
2. 配置 `DMA_INTEN` / 通道 `TC`（及可选 `THC`）中断  
3. 写 `DMA_CHnCTR`、`DMA_CHnMOD`、`DMA_CHnMSADDR`、`DMA_CHnMDADDR`  
4. `DMA_CHnCTR.CH_EN = 1`  
5. 源侧 SARADC 产生请求 → DMA 读 FIFO → 写 SRAM  

---

## 7. 单通道连续采样 + FIFO + DMA（方案 C 逻辑顺序）

手册操作流程（§5.19.4 末，约第 336–337 页）与音频测频的结合建议：

1. 开 `SARADC_CLK_GATE`，PORTCON：**PA8 = SARADC_CH3**（关 UART RX）  
2. `ADC_MEM_MODE = 0`（FIFO），`ADC_CH_SEL = bit3`，`CONT = 1`（连续）  
3. `AVG` 按需（音频可先用 **1 次平均** 提高速率）  
4. `IN_SMPL_WIN` / 分频：先保守，示波器确认波形后再收紧  
5. `DMA_EN = 1`，配置 **DMA CH1** 环缓  
6. `ADC_START` 软复位 → `START = 1`  
7. CPU 从 SRAM 环缓做过零/测频；**主循环** 做 Vernier / BK4819  

停止：连续模式下 `ADC_START.START = 0`，当前轮结束后停（手册约第 331–332 页）。

---

## 8. 方案 B（中断）在手册中的依据

| 子方案 | 手册依据 |
|--------|----------|
| **B1 EOC** | 每通道 `ADC_CHx_EOC` 中断，读通道寄存器或 FIFO（§5.19.4 时序图，约 328–329 页） |
| **B2 Timer 触发** | `ADC_TRIG=1` + `TIMER_PLUSx_GOAL`（§5.19.4，334 页） |
| **B3 FIFO 半满/满** | `ADC_FIFO_HFULL` / `ADC_FIFO_FULL` 中断（§5.19.2，325 页） |

ISR 内仅读数据/记时间戳；**不要** 调用 BK4819 SPI（与 [`ADC_AUDIO_FREQ_MEASUREMENT_REF.md`](../../ADC_AUDIO_FREQ_MEASUREMENT_REF.md) 一致）。

---

## 9. 与 k5-v5 固件差异（实现前核对）

| 项目 | PDF | k5-v5 现状 |
|------|-----|------------|
| FIFO 寄存器 0xA0/0xA4 | 有 | `saradc.def` **无**，需补 |
| `FIFO_CLR` | 有 | **无** |
| `SARADC_SMPL` 位域 | `CLK_SEL` [10:9] 写 | `adc.c` 注释：**与 TRM 读位域 [8:7] 不一致**，改时钟时 **以 PDF + 实测** 为准 |
| DMA SARADC | `MS_SEL=011` | 未实现；CH0 已占 UART |

---

## 10. PDF 内章节索引（便于翻纸质/电子版）

| 主题 | PDF 页码（约） |
|------|----------------|
| SARADC 采样时钟 | 79 |
| SARADC 概述 / 特性 | 323–325 |
| 通道 / 单次 / 连续 / FIFO | 327–333 |
| 外部触发 / 中断 | 334–335 |
| 操作流程图 | 336–337 |
| `ADC_CFG` / `ADC_FIFO_*` 寄存器 | 339–345 |
| **DMA 外设请求映射（图 5-179）** | **390–392** |
| DMA 工作流程 | 393–394 |

---

## 11. 信息是否「够用」— 更新结论

| 目标 | PDF 加入后 |
|------|------------|
| **方案 B** | **足够** 写设计与初版代码（EOC / FIFO 半满 / 外部触发） |
| **方案 C** | **DMA 源侧选 SARADC（MS_SEL=011）**、FIFO 深度、工作顺序 **已明确**；仍需 **上板** 验证采样率、DMA 环缓与 UART 并发 |
| **模拟前端 / 测频算法** | 手册 **不涵盖**（仍靠接线 + 信号处理） |

---

## 12. 通读 PDF 补充发现（2026-05-20）

> 以下条目为对全书文本检索 + §5.19 / §5.20 / §5.21 / §6.3.16 精读后的**新增**要点，部分与 [`AUDIO_ADC_DIGIMODE_PLAN.md`](../../AUDIO_ADC_DIGIMODE_PLAN.md) 尚未写入。

### 12.1 硬件冲突：OPA0 占用 PA7 / PA8（§5.21，约第 355 页）

| 运放 | + 输入 | − 输入 | 输出到 ADC |
|------|--------|--------|------------|
| **OPA0** | **PA7** | **PA8** | **SARADC CH1**（或 PA6 脚） |
| OPA1 | PC5 | PC6 | CH10（或 PC7） |

- 使能 `OPA_CFG.OPA_EN[0]` 后，**PA7/PA8 变为运放输入**，与 **UART1 TX/RX、J6 偏置 E、PA8 直连 CH3** 方案冲突。
- 音频线入走 **CH3 + 禁用 OPA** 即可；进测频模式时确认 `OPA_EN=0`。
- 手册 **未** 提供 PA8 上的片内比较器过零；COMP0 在 PA3/PA4，**不能** 替代软件过零。

### 12.2 SARADC 通道与保留位（§5.19.1）

- **CH11、CH12：保留，不要使用**。
- CH1 可采 **OPA0 输出**（内部路由）；CH10 可采 OPA1 输出。PA8 正常用法是 **外部 CH3**，与 OPA 无关。
- 功能描述中的 `ADC_MD` / `SPL_NUM` / `ADC_MEM_MD` 在寄存器里分别为 **`CONT` / `AVG` / `ADC_MEM_MODE`**（与 k5 `saradc.h` 一致）。

### 12.3 操作流程与寄存器细节（§5.19.4、§5.19 寄存器）

| 项 | 手册说明 |
|----|----------|
| 使能顺序 | 开时钟 → PORTCON → 配 `ADC_CFG` → **软复位** → `ADC_EN=1` → `START`（k5 `board.c` 已做 `Configure/Enable/SoftReset`） |
| `ADC_EN`（bit27） | 与 `DEV_CLK_GATE` 独立；`ADC_Disable()` 必须调用 |
| `BUSY`（`ADC_START` bit1） | 1=转换进行中 |
| 连续单通道 `START` | 写 1 启动、写 0 停止；**不会像单次模式那样每次自动清零 START** |
| FIFO 半满 | `ADC_FIFO_LEVEL=8`（1000）即半满；与 B3 中断一致 |
| FIFO 文案笔误 | 正文写「16 **字节**」；寄存器为 **16 级** 采样字 |
| `EXTTRIG_SEL` @ **0xB0** | **12 位各自使能**某路 `exttrig_in`（非互斥 MUX）；B2b 需置位对应 `TIMER_PLUSx_GOAL` 位 |
| 校准 | `ADC_CALIB_OFFSET`(0xF0)、`ADC_CALIB_KD`(0xF4) + `*_VALID`；电池路径已开，音频 CH3 可复用或重校 |

### 12.4 `CLK_SEL` 文档自相矛盾（§5.6，约第 81 页）

同一 `CLK_SEL` 寄存器说明中 **`SARADC_SMPL_CLK_SEL` 出现两次**：`[11:10]` 与 `[10:9]`，且夹杂重复的 `PLL_CLK_SEL`。  
k5 `adc.c` 读位用 **[8:7]**，`syscon.h` 读写用 **[9:8]/[10:9]**。**改分频必须以硅片实测为准**，不能单信 PDF 某一列。

### 12.5 DMA 实现约束（§5.24，约第 382–401 页）

| 项 | 说明 |
|----|------|
| `SWREQ` | **仅** 存储器↔存储器；SARADC→SRAM **不能** 靠软请求，须外设握手 + `CH_EN=1` |
| `LENTH` | 实际传输次数 = **`LENTH+1`**，最大 **4096** |
| `LOOP=1` | 传完后自动重来；**`CH_EN` 不会自动清零**，须软件停 |
| `LOOP=0` | 完成后 **硬件清 `CH_EN`** |
| 改配置 | **`CH_EN=0` 时才能改** MOD/ADDR/LENTH |
| 仲裁 | DMA 与 CPU 争用总线时，CPU **至少保留一半带宽**；同优先级时 **通道号小者优先**（CH0 优先于 CH1） |
| 双缓冲 | `CHx_THC_INTEN` / `CHx_TC_INTEN`（半满/满）对应方案 C 双缓冲 |
| `MS_SEL=011` | 固件 `DMA_CH_MOD_MS_SEL_BITS_HSREQ_MS2`（值 **3**） |

### 12.6 GPIO 模拟脚（§5.7.4，约第 90 页）

复用为 SARADC 时除 PORTCON 外建议：**`PORTx_IE=0`，`PORTx_PU/PD=0`**，避免数字输入/上下拉破坏模拟波形（PA8 无板上拉，主要关 IE）。

### 12.7 低功耗（§5.5）

SLEEP/DEEPSLEEP 会关 **ADC、OPA** 等模拟块；唤醒约 **100 µs**。测频模式勿进低功耗。

### 12.8 电气特性（§6.3.16–17，约第 428–429 页）

| 参数 | 典型/极限 |
|------|-----------|
| `fADC` | 最高 **48 MHz** |
| `fs` | 最高 **2.4 MHz** |
| 外部源阻抗 `RAIN` | 表列 **≤50 kΩ** |
| 精度仿真条件 | **`RAIN < 3 kΩ`**、`fADC=48 MHz` |
| 采样电容 `CADC` | **~3.8 pF**；`RADC` **~0.65 kΩ** |

机外偏置网络 **R1∥R2≈11 kΩ** 远大于 3 kΩ，高速采样时精度可能逊于电池路径（CH4/9）；可加大 `SMPL_WIN` / 降 `fADC` 分频，或接受更大抖动。

### 12.9 对 plan 的建议修订

1. **§3 / §4**：`AnalogLineIn_Enable` 内写 **`OPA_CFG.OPA_EN=0`**，并关 PA8 的 IE/PU/PD。  
2. **§3.6**：补充「勿使能 OPA0」。  
3. **§7 / DMA**：配置前 **`DMA_CH1 CH_EN=0`**；`LENTH=N-1`；`LOOP=1` 时退出路径须清 `CH_EN`。  
4. **§8 / 测频**：片内 COMP 不可用；过零仍靠软件。  
5. **精度**：在 T4 验收中注明 11 kΩ 源阻抗下的误差预算。
