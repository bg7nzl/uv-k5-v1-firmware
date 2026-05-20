# 音频线入测频：方案 B（中断）与方案 C（DMA+FIFO）参考资料

> 适用：**电脑模拟音频 → PA8（SARADC_CH3）→ 测频 → 本地 digimode 发射**（无 UART 符号钟）。  
> 目标：测频与 `VERNIER_Solve` / BK4819 写寄存器 **时间解耦**，避免主循环轮询被换频阻塞。  
> **完整实施计划（含接线、元件、A/B/C 里程碑）**：[`AUDIO_ADC_DIGIMODE_PLAN.md`](AUDIO_ADC_DIGIMODE_PLAN.md)。

本仓库 **固件现状** 仅实现 **CPU 单次触发 ADC**（电池 CH4/CH9），**未实现** 方案 B/C。本文汇总 **芯片资料、已下载文件、与 k5-v5 代码映射**，供后续实现。

---

## 1. 方案对照

| 方案 | 做法 | 优点 | 缺点 |
|------|------|------|------|
| **A** 主循环轮询 | `ADC_Start` + 等 EOC + 读 CH3，边沿算周期 | 无新驱动；与 `board.c` 一致 | Vernier/BK4819 占用主循环时 **停采样** |
| **B** 中断 | SARADC EOC 或 Timer 周期触发 ADC；ISR 写环形缓冲/过零时间戳 | 实现量中等；**不依赖 DMA 手册** | ISR 要短；需处理与电池 ADC 复用 |
| **C** DMA+FIFO | `MEM_MODE=FIFO`、`DMA_EN=1`，DMA 读 `ADC_FIFO_DATA` 入 SRAM | 采样率最高；主循环只消费缓冲 | **k5-v5 头文件缺 FIFO 寄存器**；HSREQ 映射需实测/查 TRM |

**推荐路径**：先 **B**，验证 PA8 测频与发射策略；需要更高采样率再上 **C**（并扩展 `bsp/dp32g030/saradc.h`）。

---

## 2. 外部资料（含官方 PDF）

路径：**[`docs/external/dp32g030/`](external/dp32g030/)**

| 文件 | 用途 |
|------|------|
| **[`DP32G030.pdf`](external/dp32g030/DP32G030.pdf)** | **Panchip 参考手册**（432 页）：SARADC §5.19、DMA 图 5-179 |
| **[`PDF_SARADC_DMA_NOTES.md`](external/dp32g030/PDF_SARADC_DMA_NOTES.md)** | 从 PDF 摘录的测频相关要点（**建议先读**） |
| `DP32G030-extended.svd` | 寄存器英文说明（与 PDF 互证） |
| `fetch-external.sh` | 重新拉取 SVD 等（**不含 PDF**） |

### 2.1 PDF 已补齐的关键信息（原缺口）

| 原缺口 | PDF 中的答案（见摘录 §6） |
|--------|---------------------------|
| DMA 与 SARADC 如何连接 | 源侧 `MS_SEL = 011` → **SARADC**；目的为 **SRAM**（`MD_SEL=000`） |
| FIFO 深度 | **16 级**；溢出时 **丢弃**新样本 |
| 最高采样率量级 | 最高 **2.4 Msps**（实际受 `AVG`、采样窗口、分频限制） |
| 外部定时采样 | `ADC_TRIG=1` + `TIMER_PLUSx_GOAL` / `PWM_PLUSx_TRIGGER` |
| 方案 C 用哪条 DMA 通道 | **勿占 CH0**（UART）；建议 **CH1 或 CH2** + `MS_SEL=011` |

### 2.2 在线补充

| 资源 | URL |
|------|-----|
| docs.rs SARADC | https://docs.rs/dp32g030/latest/dp32g030/saradc/index.html |
| docs.rs DMA | https://docs.rs/dp32g030/latest/dp32g030/dma/index.html |
| dp32g030-rs | https://github.com/Xpl0itR/dp32g030-rs |

---

## 3. 硬件与引脚（UV-K5 / k5-v5）

| 项目 | 值 | 仓库依据 |
|------|-----|----------|
| 线入 MCU 脚 | **PA8** | `hardware/dp32g030/portcon.def`：`UART1_RX` / `SARADC_CH3` |
| ADC 通道掩码 | `ADC_CH3` (`0x0008`) | `driver/adc.h` |
| SARADC 基址 | `0x400BA000` | `bsp/dp32g030/saradc.h`、`DP32G030-extended.svd` |
| NVIC 中断号 | `DP32_SARADC_IRQn` = **4** | `bsp/dp32g030/irq.h`；SVD `<value>4</value>` |
| 向量入口 | `HandlerSARADC` | `start.S`（**当前为空转** `b .`） |

模拟前端（隔直/偏置/耳机口）见对话与 PCB 逆向笔记；**不在本文展开**。

---

## 4. k5-v5 现有 ADC 代码（方案 A 基准）

| 文件 | 内容 |
|------|------|
| `board.c` `BOARD_ADC_Init` | CH4\|CH9、8 次平均、单次、**DMA 关**、**中断关** |
| `board.c` `BOARD_ADC_GetBatteryInfo` | CPU 触发 + 轮询 EOC |
| `driver/adc.c` | `ADC_Configure` / `ADC_Start` / `ADC_GetValue` |
| `driver/adc.c` 注释 | **TRM 与固件对 `SARADC_SMPL_CLK` 位域不一致**（读时钟时注意） |

音频测频启用前需：**关闭 UART RX 对 PA8 的占用**、**PORTCON 切到 SARADC_CH3**、与电池采样 **互斥或分时复用** 同一 SARADC 模块。

---

## 5. 方案 B：中断测频

### 5.1 子型号

| 子方案 | 触发 | ISR 工作 |
|--------|------|----------|
| **B1 SARADC EOC** | `ADC_IE` 使能 CH3 对应 `ADC_CHx_EOC_IE` | 读 `ADC_CH3_DATA`（12 bit），过零/阈值 |
| **B2 Timer + CPU 触发** | `TIMER_*` 周期中断里 `ADC_Start` | 与 B1 类似，采样率由定时器决定 |
| **B3 外部触发** | `ADC_TRIG=EXTERNAL` + `EXTTRIG_SEL` | 需确认 Timer/PWM 能否接 `exttrig_in[]`（SVD 仅描述位域，**无 UV-K5 网表**） |

**建议首选 B1 或 B2**；B3 依赖未在仓库验证的布线。

### 5.2 关键寄存器（extended SVD 摘要）

**`ADC_CFG`（offset 0x00）**

- `ADC_CH_SEL[15:0]`：通道掩码，CH3 = bit3  
- `CONT`：0 单次 / 1 连续  
- `ADC_MEM_MODE`：**0 = FIFO 模式，1 = 通道模式**（电池当前为 **CHANNEL**）  
- `DMA_EN`：0 CPU 读 FIFO / 1 DMA 读 FIFO（方案 C 用）  
- `ADC_TRIG`：0 CPU / 1 外部  

**`ADC_START`（offset 0x04）**

- `START`：写 1 启动；连续模式下清 0 停止  
- `FIFO_CLR`：写 1 清 FIFO（方案 C）  
- `BUSY`：转换进行中  

**`ADC_IE` / `ADC_IF`（offset 0x08 / 0x0C）**

- `ADC_CHx_EOC_IE` / `ADC_CHx_EOC_IST`：每通道转换完成（**写 1 清**）  
- `ADC_FIFO_FULL_IE` / `ADC_FIFO_HFULL_IE`：FIFO 满/半满（方案 C 亦可唤醒 CPU）  

**通道模式数据（电池路径）**

- `ADC_CH3_STAT` @ 0x10 + 8×3 = **0x28**（每通道 8 字节间距，CH0@0x10）  
- `ADC_CH3_DATA`：12 bit 数据 + `ADC_CH_NUM`  

> **注意**：k5-v5 的 `hardware/dp32g030/saradc.def` **未列出** `ADC_FIFO_*`；实现 B 的 EOC 路径足够。若用 FIFO 中断，须对照 **extended SVD** 补 `bsp` 头文件。

### 5.3 固件改动要点（k5-v5）

1. **`start.S`**：实现 `HandlerSARADC`（或 `HandlerDMA` 若仅用 Timer）  
2. **`driver/adc.c`**：`ADC_Configure` 时若 `IE_CHx_EOC != NONE`，`NVIC_EnableIRQ(DP32_SARADC_IRQn)`（已有逻辑）  
3. **ISR**：仅 `ADC_GetValue`、写 `volatile` 缓冲；**禁止** BK4819 SPI、`VERNIER_Solve`  
4. **主循环**：读缓冲 → `freq_dhz` → `StageFreq` / `CommitPreparedHop`（见 `app/digmode.c`）  
5. **时间戳**：`SCHEDULER_GetMicros()`（`scheduler.c`，48 MHz）可在 ISR 调用，注意与 SysTick 的一致性  

### 5.4 参考：本仓库 UART DMA（仅 DMA 控制器用法）

| 项目 | UART RX（已实现） | SARADC（待做） |
|------|-------------------|----------------|
| 通道 | `DMA_CH0` | 建议 **CH1–CH3** 空闲（UART 占 CH0） |
| 源 | `UART1->RDR` 固定地址 | 方案 C：`ADC_FIFO_DATA` 固定 |
| 握手 | `MS_SEL = HSREQ_MS1` | **未知**，SVD 写 “因芯片而异” |
| 代码 | `driver/uart.c` | — |

---

## 6. 方案 C：DMA + FIFO 连续采样

### 6.1 数据路径（SVD 描述）

1. `ADC_MEM_MODE = 0`（**FIFO 模式**）  
2. `CONT = 1`（连续采样）或单次扫描多通道（音频通常 **仅 CH3 置位**）  
3. `DMA_EN = 1`：**DMA 读 FIFO**，CPU 不轮询 `ADC_FIFO_DATA`  
4. 转换结果入 **16 级 FIFO**（`ADC_FIFO_LEVEL` 0–15；满为 16 样本，见 SVD 说明）  
5. DMA 将 `ADC_FIFO_DATA`（含 `ADC_FIFO_NUM` + 12 bit 数据）搬运至 SRAM 环形缓冲  

**溢出**：SVD 注明 FIFO 溢出后 **新数据丢弃**（与通道模式“覆盖旧数据”不同）。

### 6.2 FIFO 寄存器（extended SVD，k5-v5 需补头文件）

| 寄存器 | Offset | 说明 |
|--------|--------|------|
| `ADC_FIFO_STAT` | **0xA0** | `FULL`/`HFULL`/`EMPTY`/`LEVEL[7:4]` |
| `ADC_FIFO_DATA` | **0xA4** | 读出一字：高 4 bit 通道号 + 12 bit 数据 |

中断：`ADC_FIFO_FULL_IF`、`ADC_FIFO_HFULL_IF`（`ADC_IF` bit16–17，写 1 清）。

### 6.3 DMA 配置（手册已明确 + 上板验证）

依据 **`DP32G030.pdf` 图 5-179（约第 390 页）** 与 [`PDF_SARADC_DMA_NOTES.md`](external/dp32g030/PDF_SARADC_DMA_NOTES.md)：

- **源**：`MS_ADDMOD = NONE`，`MSADDR = ADC_FIFO_DATA`（`0x400BA0A4`）  
- **目的**：`MD_ADDMOD = INCREMENT`，`MD_SEL = 000`（SRAM）  
- **`MS_SEL = 011`（数值 3）** → 手册表 **SARADC**（`bsp` 中即 `DMA_CH_MOD_MS_SEL_VALUE_HSREQ_MS2`）  
- **通道**：k5-v5 **CH0 保留 UART**；SARADC 用 **CH1 或 CH2**  
- **ADC**：`ADC_MEM_MODE=0`（FIFO），`DMA_EN=1`，`CONT=1`，`ADC_CH_SEL` 仅 CH3  

仍须 **上板** 验证：环缓是否推进、与 UART RX 并发、实际采样率。

模板：`driver/uart.c`（`DMA_CTR`、`DMA_CH0->MOD/CTR/MSADDR/MDADDR`），改 `MS_SEL` 为 **HSREQ_MS2（3）**、源地址改为 FIFO。

### 6.4 方案 C 风险（实现前阅读）

| 风险 | 说明 |
|------|------|
| HSREQ 映射缺失 | 无官方表；错误 `MS_SEL` 会导致 DMA 不工作或挂死 |
| 与 UART DMA 冲突 | CH0 已被 UART 占用；ADC 用其它通道并避免同时满负载调试 |
| `saradc.def` 不完整 | 需从 extended SVD 合并 `ADC_FIFO_*`、`FIFO_CLR` 等至 `bsp`/`hardware` |
| 采样时钟 | `SYSCON` `SARADC_SMPL` 分频 + `IN_SMPL_WIN`/`AVG` 决定速率；音频测频需 **估算并实测** |
| 电池 ADC | 同一 SARADC 模块；测频模式与 `BOARD_ADC_GetBatteryInfo` **不能并行** |

---

## 7. extended SVD 与 k5-v5 固件差异（实现 C 前必读）

| 功能 | extended SVD | k5-v5 `saradc.def` / `saradc.h` |
|------|----------------|----------------------------------|
| `ADC_FIFO_STAT` / `ADC_FIFO_DATA` | 有（0xA0/0xA4） | **无** |
| `ADC_START.FIFO_CLR` | 有 | **无** |
| `ADC_IE` FIFO 半满/满 | `ADC_FIFO_HFULL_IE` 等 | 仅 `FIFO_FULL`/`HFULL` 名在 IE 中部分存在 |
| `ADC_MEM_MODE` 语义 | 0=FIFO, 1=CHANNEL | 与 `saradc.h` 一致 |

**结论**：方案 **B** 可主要沿用现有 `ADC_Channel_t` 通道寄存器；方案 **C** 必须先 **扩展 BSP 头文件**（可从 `DP32G030-extended.svd` 生成或手抄 offset）。

---

## 8. 与 digimode / 测频策略的衔接

- 测频输出：`freq_dhz = round(f_hz * 10)`（通用 digimode，**非** 8 音常数表）  
- 发射：`GetVernierEntry` / `StageFreq` / `CommitPreparedHop`（`app/digmode.c`）— **仅主循环**  
- Vernier：建议进模式 **预填 fine 0–99**，见 [`DIGMODE_VERNIER_TIMING_PLAN.md`](DIGMODE_VERNIER_TIMING_PLAN.md)  
- 无符号钟时的跟踪策略（ε 稳定带 / 双 EMA 等）见前期讨论；本文只解决 **采样架构**  

---

## 9. 推荐阅读顺序

1. [`PDF_SARADC_DMA_NOTES.md`](external/dp32g030/PDF_SARADC_DMA_NOTES.md)  
2. [`DP32G030.pdf`](external/dp32g030/DP32G030.pdf) §5.19、§5.20（图 5-179）  
3. 本文 §1–§8 + `DP32G030-extended.svd`  
4. `driver/uart.c`、`app/digmode.c`  
5. 上板：PA8 波形、FIFO 水位、DMA 环缓  

---

## 10. 信息是否够用（含 PDF 后）

| 目标 | 结论 |
|------|------|
| **方案 B** | **足够** 设计与实现（EOC / FIFO 半满 / Timer 外部触发，见 PDF §5.19） |
| **方案 C** | **寄存器与 DMA 映射足够**；仍需实测采样率、CH1/CH2 与 UART 共存 |
| **模拟前端 + 无时钟跟频算法** | PDF **不覆盖**，仍靠接线与软件策略 |

---

## 11. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-05-20 | 初版：SVD + 仓库映射 |
| 2026-05-20 | 纳入 `DP32G030.pdf`；补充 DMA `MS_SEL=011`=SARADC；新增 `PDF_SARADC_DMA_NOTES.md` |
