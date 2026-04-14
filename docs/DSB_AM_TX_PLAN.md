---
name: k5-v5 AM / USB(DSB-SC) 发射实现计划
overview: |
  AM 模式使用方案 C（BK4819 内部极小频偏 FM 近似 AM），USB 模式使用方案 A（REG_64 轮询 + PA 包络调制实现 DSB-SC）。两条路径并行实现，同一固件同时可测试两种调制。
todos:
  - id: reg40-api
    content: "driver: 封装 BK4819_SetTxDeviation()，补 BK4819_REG_40 枚举"
    status: completed
  - id: scheme-c-am
    content: "方案 C / AM TX：在 FUNCTION_Transmit 的 AM 分支中设极小频偏 + 关预加重"
    status: completed
  - id: timer-driver
    content: "方案 A 基础设施：TIMER_BASE0 驱动（init + ISR 框架 + start.S 向量）"
    status: completed
  - id: scheme-a-usb
    content: "方案 A / USB TX：dsb_tx 引擎，REG_64 读取 + PA bias 调制，零频偏纯载波"
    status: completed
  - id: tx-flow
    content: "集成发射流程：functions.c / radio.c 入口与清理"
    status: completed
  - id: makefile
    content: "Makefile 加 ENABLE_DSB_TX 编译开关 + 新源文件"
    status: completed
isProject: false
---

> **前置研究**：[../../docs/DSB-SC_RESEARCH.md](../../docs/DSB-SC_RESEARCH.md)

# k5-v5 AM / USB(DSB-SC) 发射实现计划

## 设计决策

| 调制模式 | 实现方案 | 原理 |
|----------|---------|------|
| **AM** (`MODULATION_AM`) | 方案 C — 极小频偏 FM | BK4819 内部 MIC ADC → FM 调制器，频偏设到极小值，窄带 FM 近似 AM。零 MCU 实时开销。 |
| **USB** (`MODULATION_USB`) | 方案 A — PA 包络调制 | REG_40=0 纯载波 + 定时器中断驱动 REG_64→REG_36 包络循环，实现 DSB-SC。 |

两条路径共享同一个 `ENABLE_DSB_TX` 编译开关，由运行时 `gCurrentVfo->Modulation` 区分。

---

## 硬件约束（原理图分析结论）

根据 UV-K5 PCB R51-V1.4 原理图逆向工程数据：

### MIC 信号路径

```
麦克风 (MK1) ──→ BK4819 MICP (Pin14) / MICN (Pin13)
                  ↓
             [芯片内部 MIC PGA + ADC]
```

**MIC 信号不经过 DP32G030 的任何引脚。** 麦克风直接连到 BK4819 差分输入，MCU 无法通过 SARADC 采样 MIC 音频。

### DP32G030 引脚全分配

| 引脚 | 功能 | 引脚 | 功能 |
|------|------|------|------|
| PA3–PA6 | 键盘矩阵 (输入) | PB6 | 背光 PWM |
| PA7 | UART1 TX | PB7–PB8,PB10 | SPI0 (LCD) |
| PA8 | UART1 RX | PB9,PB11 | ST7565 LCD 控制 |
| PA9 | **SARADC CH4** (电池电压) | PB14 | SWD CLK |
| PA10–PA11 | 键盘 + I2C | PB15 | BK1080 FM |
| PA12–PA13 | 键盘 + 语音芯片 | PC0–PC2 | BK4819 SPI (SCN/SCL/SDA) |
| PA14 | **SARADC CH9** (电池电流) | PC3 | 手电筒 |
| | | PC4 | 扬声器使能 |
| | | PC5 | PTT 按钮 (输入) |

**结论**：没有空闲引脚可接收模拟音频。SARADC 直采方案（原方案 B）**硬件上不可行**。

### PA 控制链路

```
MCU ──SPI──→ BK4819 REG_36 ──→ VRAMP (Pin18) ──→ LM2904 运放 ──→ PA gate bias
```

REG_36<15:8> 为 8-bit DAC（0x00=0V … 0xFF=3.2V），MCU 写入后经 VRAMP 引脚输出，通过 LM2904 缓冲驱动 PA 偏置。

---

## 平台参数

| 项目 | 值 |
|------|-----|
| MCU | DP32G030 / Cortex-M0 / **48 MHz** |
| BK4819 总线 | **GPIO bit-bang SPI**，单次读写 **~80–100 μs** |
| SysTick | **10 ms** 周期，已用于调度 |
| 硬件定时器 | TIMER_BASE0/1/2 **未被固件使用**，可征用 |
| 主循环 | `APP_Update()` 自由运行，无固定周期 |

---

## 方案 C：AM 发射（极小频偏 FM）

### 原理

当 FM 频偏远小于调制频率（调制指数 β << 1）时，窄带 FM 频谱近似 AM：

```
NBFM ≈ carrier ± sideband（类 AM 频谱结构）
```

AM 接收机的包络检波器可解调此信号；SSB 接收机同样可用。

### REG_40 频偏估算

Application Note 3.7 节：REG_40<11:0> 默认 0x4D0（1232）对应标准 FM 偏差。

设为 0x010（16）→ 频偏缩小 ~77×。标准 ±5 kHz 变为 ±65 Hz。
调制指数 β = 65/3000 ≈ 0.02 << 1 ✓

### 寄存器配置

```c
// AM 发射进入
BK4819_WriteRegister(BK4819_REG_40, (1u << 12) | 0x010);  // enable + 极小偏差

uint16_t reg2b = BK4819_ReadRegister(BK4819_REG_2B);
reg2b |= (1 << 0);   // 关闭 Tx 预加重（保留低频）
reg2b |= (1 << 2);   // 关闭 HPF 300Hz（保留 300Hz 以下）
BK4819_WriteRegister(BK4819_REG_2B, reg2b);

// MIC ADC 和 Tx DSP 保持使能（正常 FM 路径工作，只是频偏极小）
// REG_50<15>=0 不 mute AF Tx
```

### 优缺点

| 优点 | 缺点 |
|------|------|
| **零 MCU 实时开销** | 不是真正 AM（静默时仍有载波+微量 FM 残留） |
| 0–3 kHz 全带宽天然保证 | 调制深度受限于 REG_40 最小值 |
| 实现极简（改几个寄存器） | AM 接收端可能有轻微失真 |
| 不需要定时器或中断 | 频偏最小值可能仍偏大（需实验 0x008~0x030） |

---

## 方案 A：USB / DSB-SC 发射（PA 包络调制）

### 原理

DSB-SC 信号：`s(t) = m(t) × cos(2πfct)`

实现方法：
1. REG_40=0 关闭 FM 频偏 → VCO 输出纯载波
2. REG_50 mute AF Tx → 禁止残余 FM 调制
3. 定时器中断驱动 REG_64 → REG_36 循环 → PA 输出功率跟随音频幅度

静默时 REG_64=0 → PA bias=0 → 载波被抑制（DSB-SC 特征）。

### REG_64 带宽问题

REG_64 是 VoX 包络检测器，**实际带宽未知**，需实测：

- 若 ≥2 kHz → 可传递大部分语音信息，方案 A 直接可用
- 若仅几十~几百 Hz → 只能传递语音包络，音质退化但仍可辨（类似 VOX 级别的幅度跟随）

**无论如何，方案 A 值得实现**——即使 REG_64 带宽有限，DSB-SC 的载波抑制效果本身就有价值（节省功率、减少 QRM）。

### 时间预算

```
TIMER_BASE0 @ 4 kHz（250 μs 周期）：
  SPI 读 REG_64   ≈ 100 μs
  计算 bias        ≈   5 μs
  SPI 写 REG_36   ≈  80 μs
  ─────────────────────────
  总计             ≈ 185 μs（占 74%）
  剩余给主循环     ≈  65 μs
```

4 kHz 采样率可调制信号到 ~2 kHz（Nyquist）。CPU 负载高但发射期间可接受。

### 交替采样优化（可选）

若需更高采样率：

```
TIMER_BASE0 @ 8 kHz（125 μs 周期）：
  奇数中断：读 REG_64 → 存入变量         ≈ 100 μs
  偶数中断：用上次读数计算并写 REG_36    ≈  85 μs
```

等效 4 kHz 更新率但 8 kHz 节拍，实际效果需对比测试。

### 调制引擎

```c
typedef struct {
    bool     active;
    bool     suppress_carrier;   // true=DSB-SC(USB), false=AM
    uint8_t  max_bias;           // 最大 PA bias
    uint8_t  carrier_bias;       // AM 载波偏置（DSB-SC 时为 0）
    uint8_t  pa_gain;            // REG_36<6:0> 增益+使能位
    uint32_t frequency;
} DSB_State_t;

static volatile DSB_State_t dsb;

// TIMER_BASE0 ISR @ 4 kHz
void HandlerTIMER_BASE0(void)
{
    TIMER_ClearFlag();  // 清中断标志
    
    if (!dsb.active) return;
    
    uint16_t raw = BK4819_ReadRegister(BK4819_REG_64);
    
    // 缩放（右移位数需根据实测动态范围调整）
    uint8_t amp = (raw > 0x7F00) ? 255 : (uint8_t)(raw >> 7);
    
    uint8_t bias;
    if (dsb.suppress_carrier) {
        // DSB-SC: bias 从 0 到 max，完全跟随幅度
        bias = (uint8_t)((amp * dsb.max_bias) / 255);
    } else {
        // AM: bias = carrier + modulation
        uint8_t range = dsb.max_bias - dsb.carrier_bias;
        bias = dsb.carrier_bias + (uint8_t)((range * amp) / 255);
    }
    
    BK4819_WriteRegister(BK4819_REG_36,
        ((uint16_t)bias << 8) | dsb.pa_gain);
}
```

---

## 发射流程集成

### 进入发射（`functions.c` → `FUNCTION_Transmit()`）

```c
#ifdef ENABLE_DSB_TX
const bool isAm  = (gCurrentVfo->Modulation == MODULATION_AM);
const bool isUsb = (gCurrentVfo->Modulation == MODULATION_USB);

if (isAm || isUsb) {
    // 借用 FM TX 链路建立 VCO/PLL
    RADIO_SetModulation(MODULATION_FM);
}
#endif

RADIO_SetTxParameters();

#ifdef ENABLE_DSB_TX
if (isAm) {
    // ── 方案 C：极小频偏 FM 近似 AM ──
    BK4819_WriteRegister(BK4819_REG_40, (1u << 12) | 0x010);
    uint16_t reg2b = BK4819_ReadRegister(BK4819_REG_2B);
    BK4819_WriteRegister(BK4819_REG_2B, reg2b | (1 << 0) | (1 << 2));
}
else if (isUsb) {
    // ── 方案 A：纯载波 + PA 包络调制 ──
    BK4819_WriteRegister(BK4819_REG_40, 0x0000);       // 零频偏
    BK4819_EnterTxMute();                               // mute AF Tx
    DSB_TX_Start(gCurrentVfo->pTX->Frequency, true);    // suppress_carrier=true
}
#endif
```

### 退出发射（`radio.c` → `RADIO_SendEndOfTransmission()`）

```c
#ifdef ENABLE_DSB_TX
if (DSB_TX_IsActive()) {
    DSB_TX_Stop();
}
// 恢复默认 FM 频偏
BK4819_WriteRegister(BK4819_REG_40, (1u << 12) | 0x4D0);
// 恢复 REG_2B 预加重设置
uint16_t reg2b = BK4819_ReadRegister(BK4819_REG_2B);
BK4819_WriteRegister(BK4819_REG_2B, reg2b & ~((1 << 0) | (1 << 2)));
RADIO_SetupRegisters(false);
return;
#endif
```

### 主循环（`app/app.c` → `APP_Update()`）

方案 C 无需主循环参与。方案 A 的核心工作在 TIMER ISR 中完成，主循环仅可选添加状态监控：

```c
#ifdef ENABLE_DSB_TX
if (gCurrentFunction == FUNCTION_TRANSMIT && DSB_TX_IsActive()) {
    // 可选：更新 UI 显示调制电平、超时检测等
}
#endif
```

---

## TIMER_BASE0 驱动（方案 A 专用）

DP32G030 的 TIMER_BASE0/1/2 均空闲，IRQ 在 `irq.h` 已定义，`start.S` 向量表有占位。

### 初始化

```c
void TIMER_BASE0_Init(uint32_t frequency_hz)
{
    // 1. 开时钟门控
    SYSCON_DEV_CLK_GATE |= SYSCON_DEV_CLK_GATE_TIMER_BASE0;
    
    // 2. 配置计数周期: 48 MHz / target_freq
    TIMER_BASE0->LOAD = (48000000u / frequency_hz) - 1;
    TIMER_BASE0->CTRL = TIMER_CTRL_ENABLE | TIMER_CTRL_INTEN;
    
    // 3. NVIC
    NVIC_EnableIRQ(TIMER_BASE0_IRQn);
}

void TIMER_BASE0_Stop(void)
{
    TIMER_BASE0->CTRL = 0;
    NVIC_DisableIRQ(TIMER_BASE0_IRQn);
}
```

### start.S 修改

将 `HandlerTIMER_BASE0` 的 `b .` 死循环替换为指向真实 ISR：

```asm
HandlerTIMER_BASE0:
    b HandlerTIMER_BASE0_Real   @ → dsb_tx.c 中的 ISR
```

### SPI 安全性

BK4819 驱动无互斥保护。发射期间 ISR 以 4 kHz 频率操作 SPI。

**策略**：发射期间主循环**不访问 BK4819**。现有代码在 `FUNCTION_TRANSMIT` 状态下的 BK4819 操作极少（仅 DTMF/尾音等，可跳过）。如需额外保护：

```c
__disable_irq();
BK4819_SomeOperation();  // 主循环中的 BK4819 操作
__enable_irq();
```

---

## 文件修改清单

| 文件 | 操作 | 内容 |
|------|------|------|
| `driver/bk4819-regs.h` | 修改 | 补 `BK4819_REG_40` 枚举 |
| `driver/bk4819.c` + `.h` | 修改 | `BK4819_SetTxDeviation()` |
| `app/dsb_tx.c` | **新增** | DSB_TX_Start/Stop/IsActive + TIMER ISR |
| `app/dsb_tx.h` | **新增** | 接口声明 |
| `start.S` | 修改 | `HandlerTIMER_BASE0` 指向真实 ISR |
| `functions.c` | 修改 | AM / USB 发射入口分支 |
| `radio.c` | 修改 | `SendEndOfTransmission` 恢复默认 |
| `Makefile` | 修改 | `ENABLE_DSB_TX` + 新源文件 |

---

## 验证计划

| 测试 | 内容 | 工具 | 通过标准 |
|------|------|------|---------|
| AM TX | 选 AM 模式发射，另一台 AM/SSB 收 | 两台电台或 SDR | 语音可听懂 |
| USB TX | 选 USB 模式发射，另一台 USB 收 | 两台电台或 SDR | 语音可听懂 + 静默时无载波 |
| REG_64 带宽 | USB 发射时 UART 导出 ISR 采样 | PC 分析频谱 | 确认实际可调制带宽 |
| 频偏调优 | AM 模式下尝试 REG_40 = 0x008~0x030 | SDR 频谱 | 找到最佳调制深度 |
| 回归 | 正常 FM / CW 收发 | 常规操作 | 不受影响 |

---

## 风险与对策

| 风险 | 概率 | 影响 | 对策 |
|------|------|------|------|
| REG_64 带宽 < 500 Hz | 高 | USB 音质差（仅传包络） | AM 模式（方案 C）作为主力；USB 仍有载波抑制价值 |
| 方案 C 频偏最小值偏大 | 中 | AM 接收端 FM 残留失真 | 扫描 REG_40 = 0x008~0x030 找最优值 |
| ISR 中 SPI 与主循环冲突 | 低 | 偶发通信错误 | 发射期间主循环跳过 BK4819 操作 |
| PA bias DAC 带宽不够 3 kHz | 低 | USB 高频语音衰减 | 实测确认；退化到 ~2 kHz 仍可用 |
