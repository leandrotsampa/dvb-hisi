### Build
Clone this project to Hisilicon Kernel Source Code and build kernel using wiki information.

#isilicon Kernel v4.4.35
https://github.com/leandrotsampa/hisilicon-kernel

Hisilicon Kernel v3.18.24
https://github.com/leandrotsampa/hisilicon-kernel3

Path in kernel:
drivers/msp/drv/dvb-hisi

### Box Config
See file: box_detector.c

Param: box_type
Supported Boxes:
	0 - Auto-detect           (default)
	1 - Atto i-Smart          (DVB-C Quad)
	2 - Atto Pixel            (DVB-C Triple)
	3 - Atto Pixel            (DVB-C Triple + ISDB-T)
	4 - Atto Pixel            (DVB-C Triple + DVB-S)
	5 - Atto Pixel Premium    (DVB-C Triple + DVB-S Duo)
	6 - 96Boards Poplar       (DVB-T)
	7 - SmartSTB U5-PVR       (DVB-S + DVB-T)
	8 - Formuler S-Mini       (DVB-S)

IKS Code removed.