# Smart Environment Monitor — i.MX6ULL 嵌入式 Linux 综合驱动项目

基于正点原子 i.MX6ULL 开发板的完整嵌入式 Linux 系统，涵盖 **6 个内核驱动 + 9 个用户空间程序**，实现对多总线下传感器的统一采集、处理与监控。

## 技术亮点

```
驱动层（内核空间）
├── comp_drv.ko      字符设备 + Platform + GPIO + 全IO模型(阻塞/非阻塞/Poll/异步通知) + 内核定时器
├── key_input.ko     Input子系统 + 中断顶半部 + 工作队列底半部 + 自旋锁
├── ap3216c.ko       I2C总线 + SMBus + i2c_transfer Burst Read
├── icm20608.ko      SPI总线 + spi_write_then_read + 4种SPI模式
├── uart_sensor.ko   UART/串口框架 + kfifo环形缓冲 + TTY架构
└── sys_monitor.ko   Misc杂项设备 + module_param + sysfs + debugfs

应用层（用户空间）
├── smart_monitor     统一监控守护进程（epoll多路复用6路传感器 + CSV日志）
└── test_*            8个独立驱动测试程序
```

## 驱动知识覆盖 (第40~66章)

| 分类 | 涵盖知识点 |
|------|-----------|
| **字符设备** | 新风格CDEV (`alloc_chrdev_region` + `cdev_add`)、Misc设备 (`misc_register`) |
| **总线通信** | I2C (`i2c_driver`, SMBus, `i2c_transfer`)、SPI (`spi_driver`, `spi_write_then_read`)、UART (TTY/串口架构, `kfifo`) |
| **设备树** | `compatible` 匹配、GPIO/I2C/SPI 节点绑定、自定义属性 |
| **并发保护** | 原子操作 (`atomic_t`)、互斥体 (`mutex`)、自旋锁 (`spin_lock_irqsave`) |
| **IO模型** | 阻塞IO、非阻塞IO (`O_NONBLOCK`/`-EAGAIN`)、Poll/Select、异步通知 (`SIGIO`/`FASYNC`) |
| **中断处理** | 顶半部 (`request_irq`) + 底半部工作队列 (`INIT_DELAYED_WORK`)、双边沿触发、消抖 |
| **内核工具** | 内核定时器 (`setup_timer`)、等待队列、`devm_` 托管资源 |
| **驱动接口** | `sysfs` 属性组、`debugfs` 调试接口、`module_param` 模块参数 |

## 项目结构

```
firstcc/
├── driver/                    # 6个内核驱动模块
│   ├── comp_drv.h/c           #   LED综合字符设备驱动
│   ├── key_input.c            #   按键Input子系统驱动
│   ├── ap3216c.c              #   AP3216C I2C光线/距离传感器
│   ├── icm20608.h/c           #   ICM20608 SPI 6轴传感器
│   ├── uart_sensor.c          #   UART串口传感器驱动
│   ├── sys_monitor.c          #   系统监控Misc设备
│   └── Makefile               #   Kbuild编译规则
├── app/                       # 9个用户空间程序
│   ├── smart_monitor.c        #   统一监控守护进程 (epoll + CSV日志)
│   ├── test_common.h          #   公共头文件
│   ├── test_led_rw.c          #   LED读写 + ioctl测试
│   ├── test_blocking.c        #   阻塞/非阻塞IO测试
│   ├── test_poll.c            #   Poll/Select多路复用测试
│   ├── test_async.c           #   异步通知SIGIO测试
│   ├── test_key.c             #   按键Input事件测试
│   ├── test_ap3216c.c         #   I2C传感器测试
│   ├── test_icm20608.c        #   SPI传感器测试
│   ├── test_uart.c            #   UART串口通信测试
│   └── Makefile               #   用户程序编译规则
├── dts/
│   └── imx6ull-custom.dtsi    # 设备树片段（6个设备节点）
├── build.sh                   # 一键交叉编译脚本
└── firstcc_知识点详解.md       # 完整知识点文档（18章）
```

## 快速开始

### 前置环境

- ARM 交叉编译器：`arm-linux-gnueabihf-`
- 已编译的 i.MX6ULL 内核源码树（Linux 4.1.15+）
- 正点原子 ALPHA / Mini i.MX6ULL 开发板

### 编译

```bash
# 一键编译全部（驱动 + 应用）
export KERNEL_DIR=/path/to/linux-imx
./build.sh

# 或分步编译
make -C driver KERNEL_DIR=$KERNEL_DIR ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
make -C app    CC=arm-linux-gnueabihf-gcc
```

### 部署

```bash
scp driver/*.ko root@<board_ip>:/lib/modules/$(uname -r)/
scp app/test_* app/smart_monitor root@<board_ip>:/root/
```

### 运行

```bash
# 加载全部驱动
modprobe comp_drv       # → /dev/comp_drv + sysfs + debugfs
modprobe key_input      # → /dev/input/eventX
modprobe ap3216c        # → /dev/ap3216c
modprobe icm20608       # → /dev/icm20608
modprobe uart_sensor    # → /dev/uart_sensor
modprobe sys_monitor    # → /dev/sys_monitor

# 运行统一监控程序
./smart_monitor data.csv   # epoll监听全部传感器 + CSV记录

# 或单独测试各驱动
./test_led_rw on           # 点亮LED
./test_led_rw blink        # LED闪烁
./test_key /dev/input/event1  # 读取按键
./test_async &             # 异步通知 + ./test_led_rw on
./test_ap3216c             # I2C传感器
./test_icm20608 accel      # SPI加速度计
./test_uart read           # UART串口数据
```

### sysfs / debugfs 使用示例

```bash
# sysfs — 运行时查看/修改驱动参数
cat /sys/devices/platform/comp_drv.0/led_state
echo 1000 > /sys/devices/platform/comp_drv.0/blink_period
cat /sys/devices/platform/comp_drv.0/read_count

# debugfs — 驱动调试信息
cat /sys/kernel/debug/comp_drv/stats
cat /sys/kernel/debug/key_input/irq_count

# module_param — 加载时传参
modprobe comp_drv default_blink_ms=250 debug=1
modprobe key_input default_debounce=10 default_keycode=28
```

## 驱动对比要点

| 维度 | I2C (ap3216c) | SPI (icm20608) |
|------|--------------|----------------|
| 总线类型 | 2线，半双工 | 4线，全双工 |
| 驱动结构 | `i2c_driver` | `spi_driver` |
| 通信API | `i2c_smbus_read_byte_data` | `spi_write_then_read` |
| 设备树 | `reg = <0x1e>` (从地址) | `reg = <0>` (片选号) |

| 维度 | CDEV (comp_drv) | Misc (sys_monitor) |
|------|----------------|-------------------|
| 注册API | `alloc_chrdev_region` + `cdev_add` | `misc_register()` 一步完成 |
| 设备节点 | 需 `class_create` + `device_create` | 自动创建 |
| 代码量 | ~20行 | ~5行 |
| 适用场景 | 复杂设备 | 简单传感器/状态接口 |

## 许可证

GPL v2 — 与 Linux 内核保持一致。
