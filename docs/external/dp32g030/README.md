# DP32G030 外部参考资料（本目录）

本目录存放 DP32G030 技术资料，供 [`docs/ADC_AUDIO_FREQ_MEASUREMENT_REF.md`](../ADC_AUDIO_FREQ_MEASUREMENT_REF.md) 与 [`docs/AUDIO_ADC_DIGIMODE_PLAN.md`](../AUDIO_ADC_DIGIMODE_PLAN.md) 中 **方案 B/C** 实现时对照。

> **版权**：各文件归属原作者/项目；PDF 仅供固件开发参考。

## 文件列表

| 文件 | 来源 | 说明 |
|------|------|------|
| **[DP32G030.pdf](DP32G030.pdf)** | **用户提供**（Panchip《DP32G030 参考手册》，432 页） | **主文档**：SARADC §5.19、DMA 请求映射图 5-179（约第 390 页） |
| [PDF_SARADC_DMA_NOTES.md](PDF_SARADC_DMA_NOTES.md) | 从 PDF 摘录 | SARADC/DMA/PA8 要点 + 页码索引 |
| [DP32G030-extended.svd](DP32G030-extended.svd) | [Xpl0itR/dp32g030-rs](https://github.com/Xpl0itR/dp32g030-rs) | 寄存器位域（英文），含 FIFO |
| [DP32G030-amnemonic-original.svd](DP32G030-amnemonic-original.svd) | [amnemonic/Quansheng_UV-K5_Firmware](https://github.com/amnemonic/Quansheng_UV-K5_Firmware) | 原始 SVD |
| [dp32g030-rs-README.md](dp32g030-rs-README.md) | 同上 | SVD 致谢 |
| [dp32g030.cfg](dp32g030.cfg) | [egzumer/uv-k5-firmware-custom](https://github.com/egzumer/uv-k5-firmware-custom) | OpenOCD |

## 阅读顺序

1. [PDF_SARADC_DMA_NOTES.md](PDF_SARADC_DMA_NOTES.md)（快速）  
2. `DP32G030.pdf` §5.19、§5.20（图 5-179）  
3. `DP32G030-extended.svd` + 本仓库 `hardware/dp32g030/*.def`

## 更新方式

```bash
cd docs/external/dp32g030
curl -fsSL -o DP32G030-extended.svd \
  "https://raw.githubusercontent.com/Xpl0itR/dp32g030-rs/master/src/DP32G030.svd"
```
