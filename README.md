# i.MX6ULL 综合驱动项目

基于《正点原子 I.MX6U 嵌入式 Linux 驱动开发指南 V2.0.1》第40章至第64章知识点构建的综合驱动项目，可运行于正点原子 ALPHA/Mini i.MX6ULL 开发板。

## 项目目标

将第40~64章中的核心驱动知识点整合成一个可编译、可加载、可测试的完整驱动工程，用于巩固：

- 字符设备驱动框架（新/旧风格）
- 设备树绑定与 OF 函数
- pinctrl / GPIO 子系统
- 并发保护（原子操作、自旋锁、互斥体）
- 阻塞 IO / 非阻塞 IO / Poll / 异步通知
- 内核定时器
- Platform 驱动模型
- Input 输入子系统
- I2C 驱动框架
- 中断处理（顶半部 + 底半部工作队列）

---

## 文件结构

```
firstcc/
├── driver/                     # 内核驱动模块
│   ├── comp_drv.h              #   公共头文件（ioctl 命令、状态结构体）
│   ├── comp_drv.c              #   综合 LED 字符设备驱动（~400 行）
│   ├── key_input.c             #   按键 Input 子系统驱动（~210 行）
│   ├── ap3216c.c               #   AP3216C I2C 传感器驱动（~310 行）
│   └── Makefile                #   驱动模块编译规则
├── app/                        # 用户空间测试程序
│   ├── test_common.h           #   公共头文件（ioctl 定义、设备路径）
│   ├── test_led_rw.c           #   LED 基本读写测试
│   ├── test_blocking.c         #   阻塞 / 非阻塞 IO 测试
│   ├── test_poll.c             #   Poll / Select 多路复用测试
│   ├── test_async.c            #   异步通知（SIGIO）测试
│   ├── test_key.c              #   按键事件读取测试
│   ├── test_ap3216c.c          #   AP3216C 传感器读取测试
│   └── Makefile                #   测试程序编译规则
├── dts/
│   └── imx6ull-custom.dtsi     # 设备树片段（添加驱动节点）
├── build.sh                    # 一键编译脚本
├── .gitignore
└── README.md
```

---

## 章节知识点 → 代码对照表

### comp_drv.c — 综合 LED 字符设备驱动

| 章节 | 知识点 | 代码位置 | 说明 |
|------|--------|----------|------|
| **Ch40** | 字符设备驱动基础 | `file_operations` 结构体 | 实现 open / release / read / write / ioctl 全套文件操作 |
| **Ch42** | 新字符设备驱动 | `alloc_chrdev_region()` → `cdev_init()` → `cdev_add()` → `class_create()` → `device_create()` | 自动分配设备号，创建 `/dev/comp_drv` 设备节点 |
| **Ch43** | 设备树基础 | `of_match_table` | 通过 `compatible = "alientek,comp-drv"` 匹配设备树节点 |
| **Ch44** | 设备树下 LED 驱动 | OF 匹配 + gpiod API | 驱动通过设备树获取 GPIO 资源，不硬编码寄存器地址 |
| **Ch45** | pinctrl & GPIO 子系统 | `devm_gpiod_get(dev, "led", GPIOD_OUT_LOW)` | 使用 gpiod 子系统 API，自动管理引脚复用 |
| **Ch46-47** | 并发 / 原子操作 | `mutex_init()`, `mutex_lock()`, `mutex_unlock()` | 互斥体保护共享数据：LED 状态、统计计数器 |
| **Ch47** | 原子操作 | `atomic_t open_count`, `atomic_inc()`, `atomic_dec()` | 原子变量跟踪设备打开次数 |
| **Ch48** | 互斥体 | `mutex_lock_interruptible()` vs `mutex_trylock()` | 可中断等待 vs 非阻塞尝试 |
| **Ch49** | 阻塞 IO | `wait_queue_head_t`, `wake_up_interruptible()` | 写操作唤醒阻塞的读进程 |
| **Ch50/52** | 非阻塞 IO | `O_NONBLOCK` + `mutex_trylock()` → `-EAGAIN` | 非阻塞模式下立即返回而非睡眠 |
| **Ch52** | Poll / Select | `poll_wait()`, `.poll = comp_drv_poll` | 支持用户空间 poll / select / epoll |
| **Ch51/53** | 异步通知 | `fasync_helper()`, `kill_fasync()` | 写操作触发 SIGIO 信号通知注册进程 |
| **Ch51** | 内核定时器 | `setup_timer()`, `mod_timer()`, `del_timer_sync()` | 实现 LED 周期闪烁功能 |
| **Ch54** | Platform 驱动 | `platform_driver` 结构体 | 注册 platform 驱动，分离设备和驱动 |
| **Ch55** | 设备树下 Platform | `of_match_table` + `compatible` | Platform 驱动与设备树节点自动匹配 |

### key_input.c — 按键 Input 子系统驱动

| 章节 | 知识点 | 代码位置 | 说明 |
|------|--------|----------|------|
| **Ch43-44** | 设备树 | `of_match_table`, `of_property_read_u32()` | 从 DT 读取 key-code、debounce-interval |
| **Ch45** | GPIO 子系统 | `devm_gpiod_get()`, `gpiod_to_irq()`, `gpiod_get_value()` | 获取 GPIO 并转换为 IRQ 号 |
| **Ch56** | 中断处理（顶半部） | `devm_request_irq()`, `key_irq_handler()` | 注册 GPIO 中断，双边沿触发 |
| **Ch60** | 工作队列（底半部） | `INIT_DELAYED_WORK()`, `schedule_delayed_work()` | 延迟工作实现按键消抖（默认 20ms） |
| **Ch47** | 自旋锁 | `spinlock_t`, `spin_lock_irqsave()` | 保护中断上下文与进程上下文共享的按键状态 |
| **Ch58** | Input 子系统 | `input_allocate_device()` → `input_register_device()` → `input_report_key()` → `input_sync()` | 标准 input 设备注册与事件上报 |
| **Ch49** | 阻塞 IO | Input 子系统内置 | `/dev/input/eventX` 天然支持阻塞读取 |
| **Ch52** | Poll | Input 子系统内置 | `/dev/input/eventX` 天然支持 poll |

### ap3216c.c — AP3216C I2C 传感器驱动

| 章节 | 知识点 | 代码位置 | 说明 |
|------|--------|----------|------|
| **Ch61** | I2C 驱动框架 | `i2c_driver`, `.probe`, `.remove`, `module_i2c_driver()` | 标准 I2C 设备驱动注册 |
| **Ch61** | I2C 数据传输 | `i2c_smbus_read_byte_data()`, `i2c_smbus_write_byte_data()`, `i2c_transfer()` | SMBus 单字节读写 + I2C 多字节 burst 传输 |
| **Ch43-44** | 设备树匹配 | `of_match_table` + `compatible = "alientek,ap3216c"` | 通过设备树描述 I2C 设备地址 |
| **Ch42** | 字符设备 | `alloc_chrdev_region()` + `cdev_init()` + `class_create()` | 创建 `/dev/ap3216c` 设备节点 |
| **Ch48** | 互斥体 | `mutex_lock()` / `mutex_unlock()` | 保护 I2C 总线和传感器数据一致性 |
| **Ch47** | 原子操作 | `atomic_t open_count` | 设备打开计数 |

### 测试程序

| 程序 | 测试的知识点 | 对应章节 |
|------|-------------|----------|
| `test_led_rw` | 基本 open / read / write / ioctl | Ch40-42 |
| `test_blocking` | 阻塞读（默认）vs 非阻塞读（`O_NONBLOCK` → `EAGAIN`） | Ch49-50/52 |
| `test_poll` | `poll()` 和 `select()` 多路复用 | Ch52 |
| `test_async` | `fcntl(F_SETOWN)` + `fcntl(FASYNC)` + `SIGIO` 信号处理 | Ch51/53 |
| `test_key` | `struct input_event` 读取 + input 设备 poll | Ch58 |
| `test_ap3216c` | 字符设备读取传感器数据（IR / ALS / PS） | Ch61 |

---

## 编译

### 前置条件

1. **ARM 交叉编译器**：`arm-linux-gnueabihf-gcc`
2. **已编译的 i.MX6ULL 内核源码树**（正点原子提供的 Linux 源码）

### 一键编译

```bash
export KERNEL_DIR=/home/alientek/linux/atk-imx6ull/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek
./build.sh
```

### 分步编译

**驱动模块：**
```bash
cd driver
make KERNEL_DIR=/path/to/kernel ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
# 生成 comp_drv.ko, key_input.ko, ap3216c.ko
```

**测试程序：**
```bash
cd app
make CC=arm-linux-gnueabihf-gcc
# 生成 test_led_rw, test_blocking, test_poll, test_async, test_key, test_ap3216c
```

---

## 设备树配置

编辑 `dts/imx6ull-custom.dtsi`，根据实际硬件修改 GPIO 引脚：

```dts
/* LED: 改为你板子上的实际 LED GPIO */
led-gpios = <&gpio1 3 GPIO_ACTIVE_LOW>;

/* 按键: 改为你板子上的按键 GPIO */
key-gpios = <&gpio1 18 GPIO_ACTIVE_LOW>;
key-code = <28>;   /* KEY_ENTER */

/* AP3216C: 添加到你板子对应的 I2C 总线节点下 */
&i2c1 {
    ap3216c@1e {
        compatible = "alientek,ap3216c";
        reg = <0x1e>;
    };
};
```

将 `dts/imx6ull-custom.dtsi` 包含到主设备树中重新编译，或通过设备树 overlay 方式加载。

---

## 部署与测试

### 1. 将文件拷贝到开发板

```bash
scp driver/*.ko       root@<board_ip>:/lib/modules/$(uname -r)/
scp app/test_*        root@<board_ip>:/root/
scp dts/imx6ull-custom.dtsi root@<board_ip>:/root/
```

### 2. 加载驱动

```bash
# 在开发板上执行
depmod
modprobe comp_drv       # 加载 LED 驱动 → 创建 /dev/comp_drv
modprobe key_input      # 加载按键驱动 → 创建 /dev/input/eventX
modprobe ap3216c        # 加载传感器驱动 → 创建 /dev/ap3216c

# 验证
ls -la /dev/comp_drv /dev/input/event* /dev/ap3216c
dmesg | tail -20        # 查看驱动加载日志
```

### 3. 运行测试

```bash
# ---- LED 驱动测试 ----

# 点亮 LED
./test_led_rw on

# 关闭 LED
./test_led_rw off

# 500ms 周期闪烁
./test_led_rw blink

# 读取驱动状态（struct comp_drv_status）
./test_led_rw status

# 通过 ioctl 读取状态
./test_led_rw ioctl_status

# ---- 阻塞 / 非阻塞 IO 测试 ----

# 阻塞模式：循环读取状态（Ctrl+C 停止）
./test_blocking

# 非阻塞模式：立即返回 EAGAIN 或数据
./test_blocking nonblock

# ---- Poll / Select 测试 ----

# Poll 模式（1 秒超时）
./test_poll poll 1000

# Select 模式（2 秒超时）
./test_poll select 2000

# ---- 异步通知测试 ----

# 终端 1：启动异步监听
./test_async &
# 终端 2：触发 SIGIO 信号
./test_led_rw on
# 终端 1 将收到类似输出：
# [SIGIO #1] fd=3 band=0x1 [POLL_IN]

# ---- 按键测试 ----

# 读取按键事件（先自动 poll 5 秒，再进入阻塞读）
./test_key /dev/input/event1

# ---- AP3216C 传感器测试 ----

./test_ap3216c 10    # 连续读取 10 次
```

---

## 驱动 API 参考

### /dev/comp_drv

**write() 命令：**

| 写入内容 | 效果 |
|----------|------|
| `"on"` 或 `"1"` | 点亮 LED |
| `"off"` 或 `"0"` | 关闭 LED |
| `"blink"` | 开始闪烁（周期由 ioctl 设定） |

**read()：** 返回 `struct comp_drv_status`（24 字节），包含当前 LED 状态、闪烁周期、读写计数。

**ioctl() 命令：**

| 命令 | 参数 | 说明 |
|------|------|------|
| `COMP_DRV_SET_BLINK_PERIOD` | `unsigned long` 周期值(ms) | 设置闪烁周期 |
| `COMP_DRV_GET_STATUS` | `struct comp_drv_status *` | 获取完整状态 |
| `COMP_DRV_START_BLINK` | 无 | 开始闪烁 |
| `COMP_DRV_STOP_BLINK` | 无 | 停止闪烁并关闭 LED |

**文件操作支持：**
- `O_RDWR` — 读写打开
- `O_NONBLOCK` — 非阻塞模式
- `poll()` / `select()` / `epoll()` — IO 多路复用
- `fcntl(FASYNC)` — 异步通知（SIGIO）

### /dev/input/eventX (key_input)

标准的 Linux input 设备接口，每个事件为 `struct input_event`（16 字节）：

```c
struct input_event {
    struct timeval time;  // 事件时间戳
    __u16 type;           // EV_KEY = 0x01
    __u16 code;           // 按键码（如 KEY_ENTER = 28）
    __s32 value;          // 1=按下, 0=释放, 2=长按
};
```

### /dev/ap3216c

**read()：** 返回 `struct ap3216c_data`（6 字节）：

```c
struct ap3216c_data {
    unsigned short ir;   // 红外光强度
    unsigned short als;  // 环境光强度
    unsigned short ps;   // 接近距离
};
```

---

## 驱动架构图

```
用户空间
┌──────────────────────────────────────────────────────┐
│  test_led_rw   test_blocking   test_poll  test_async │
│       │              │              │          │      │
│       ▼              ▼              ▼          ▼      │
│  write/read    read(O_NONBLOCK)  poll()    SIGIO     │
└──────┬──────────────┬──────────────┬──────────┬──────┘
       │              │              │          │
       ▼              ▼              ▼          ▼
┌──────────────────────────────────────────────────────┐
│                 /dev/comp_drv                        │
│  ┌──────────────────────────────────────────────┐   │
│  │          file_operations (Ch40/42)             │   │
│  │  open / release / read / write / ioctl        │   │
│  │  poll / fasync                                │   │
│  └──────────────────┬───────────────────────────┘   │
│                     │                                │
│  ┌──────────────────▼───────────────────────────┐   │
│  │  并发保护层 (Ch46-48)                         │   │
│  │  struct mutex lock    (互斥体)               │   │
│  │  atomic_t open_count  (原子操作)             │   │
│  └──────────────────┬───────────────────────────┘   │
│                     │                                │
│  ┌──────────────────▼───────────────────────────┐   │
│  │  IO 模型层 (Ch49-53)                          │   │
│  │  wait_queue_head_t   (阻塞读)                │   │
│  │  mutex_trylock       (非阻塞)                │   │
│  │  poll_wait           (poll/select)           │   │
│  │  fasync_helper       (异步通知)              │   │
│  └──────────────────┬───────────────────────────┘   │
│                     │                                │
│  ┌──────────────────▼───────────────────────────┐   │
│  │  硬件控制层 (Ch45)                            │   │
│  │  gpiod_set_value()    LED GPIO               │   │
│  │  setup_timer()        内核定时器（闪烁）      │   │
│  └──────────────────────────────────────────────┘   │
│                                                     │
│  Platform 驱动框架 (Ch54-55)                        │
│  ┌──────────────────────────────────────────────┐   │
│  │  comp_drv_probe() ← compatible="alientek,..." │   │
│  │  comp_drv_remove()                           │   │
│  └──────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│              /dev/input/eventX                       │
│  ┌──────────────────────────────────────────────┐   │
│  │  Input 子系统 (Ch58)                          │   │
│  │  input_register_device()                     │   │
│  │  input_report_key() + input_sync()           │   │
│  └──────────────────┬───────────────────────────┘   │
│                     │                                │
│  ┌──────────────────▼───────────────────────────┐   │
│  │  中断处理 (Ch56/60)                          │   │
│  │  顶半部: key_irq_handler()                   │   │
│  │  底半部: schedule_delayed_work() (消抖)      │   │
│  │  自旋锁: spin_lock_irqsave()                 │   │
│  └──────────────────┬───────────────────────────┘   │
│                     │                                │
│  ┌──────────────────▼───────────────────────────┐   │
│  │  GPIO 子系统 (Ch45)                          │   │
│  │  gpiod_to_irq() + gpiod_get_value()          │   │
│  └──────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│              /dev/ap3216c                            │
│  ┌──────────────────────────────────────────────┐   │
│  │  I2C 驱动框架 (Ch61)                         │   │
│  │  i2c_driver + i2c_smbus_read/write           │   │
│  │  i2c_transfer() (burst read)                 │   │
│  │  mutex 保护 I2C 总线访问                     │   │
│  └──────────────────────────────────────────────┘   │
└──────────────────────────────────────────────────────┘
```

---

## 内核兼容性

- 主要面向 **Linux 4.1.15**（正点原子官方 BSP）
- 兼容 **Linux 4.x ~ 6.x** 主线内核
- 代码使用 `setup_timer()` 而非 4.14+ 的 `timer_setup()`，保证向后兼容

---

## 许可证

GPL v2（与 Linux 内核保持一致）
