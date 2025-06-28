# Hisilicon DVB Driver

Custom DVB driver for Hisilicon-based set-top boxes.

This project provides a custom DVB driver for set-top boxes using Hisilicon chipsets, with support for multiple hardware configurations.

---

## 📦 Build Instructions

Clone this project into the Hisilicon kernel source tree and build the kernel using the instructions available in the corresponding repository wikis.

### 🔗 Kernel Sources

- [Hisilicon Kernel v4.4.35](https://github.com/leandrotsampa/hisilicon-kernel)
- [Hisilicon Kernel v3.18.24](https://github.com/leandrotsampa/hisilicon-kernel3)

### 📁 Driver path in the kernel

```bash
drivers/msp/drv/dvb-hisi
```

---

## 🔧 Box Configuration

Box configuration is handled in the file:

[box_detector.c](https://github.com/leandrotsampa/dvb-hisi/blob/main/box_detector.c)

### Parameter: `box_type`

| Value | Box                         | Description                          |
|-------|-----------------------------|--------------------------------------|
| `0`   | Auto-detect                 | *(default)*                          |
| `1`   | Atto i-Smart                | DVB-C Quad                           |
| `2`   | Atto Pixel                  | DVB-C Triple                         |
| `3`   | Atto Pixel                  | DVB-C Triple + ISDB-T                |
| `4`   | Atto Pixel                  | DVB-C Triple + DVB-S                 |
| `5`   | Atto Pixel Premium          | DVB-C Triple + DVB-S Duo             |
| `6`   | 96Boards Poplar             | DVB-T                                |
| `7`   | SmartSTB U5-PVR             | DVB-S + DVB-T                        |
| `8`   | Formuler S-Mini             | DVB-S                                |

## 🔒 IKS Code

The **IKS**-related code has been **removed** from this repository.

---
