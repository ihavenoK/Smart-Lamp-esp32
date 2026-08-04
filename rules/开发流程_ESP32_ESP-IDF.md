# 嵌入式开发流程 --- ESP32 + ESP-IDF 专版

> 本文档是《开发流程_结构化.md》的 ESP32+ESP-IDF 适配版本。
> 工具链真实路径 `C:\Espressif\tools`，Python venv `C:\Espressif\tools\python\v6.0.2\venv`。
> 与《嵌入式C项目规则_结构化.md》配套，通过 `references` 字段交叉引用。

---

## 文档元数据

```yaml
doc_id: FLOW_ESP32
version: 1.0
date: 2026-08-04
scope: [ESP32+ESP-IDF]
doc_type: 操作步骤与执行顺序
companion_doc: 嵌入式C项目规则_结构化.md
source_doc: 开发流程_结构化.md (STM32/Keil5 通用版)
boundary: |
  本文档：先做什么、后做什么、怎么验证。
  规则文档：编码约束与标准要求。
```

---

## 流程索引

| 步骤 ID     | 阶段         | 名称                       | 类型     |
| ----------- | ------------ | -------------------------- | -------- |
| STEP-0      | 总览         | 流程总览                   | overview |
| STEP-1      | 架构         | 架构提案（RFC）与模块拆分  | process  |
| STEP-2.0    | 信息提取     | Data Sheet 信息提取        | process  |
| STEP-3.0    | 开发闭环     | 增量构建总原则             | process  |
| STEP-3.1    | 开发闭环     | 强锁规则与上下文防御       | process  |
| STEP-3.2    | 开发闭环     | 分层架构与 Host-Native 模拟 | process  |
| STEP-3.3    | 开发闭环     | AI Agent 接入编译工具链    | process  |
| STEP-3.4    | 开发闭环     | 串口与日志闭环（HIL）      | process  |
| STEP-3.4.8  | 开发闭环     | 标准排查顺序               | process  |
| STEP-3.5    | 开发闭环     | 状态快照落盘               | process  |
| STEP-3.6    | 项目级       | CI/CD 构建流水线           | process  |
| STEP-3.7    | 项目级       | OTA 更新流程               | process  |
| DEC-1.1     | 决策点       | 何时写 RFC                 | decision |
| DEC-5       | 决策点       | 流程裁减级别               | decision |
| TPL-1.2     | 模板         | RFC 内容模板               | template |
| TPL-2.1     | 模板         | pinout.md 引脚分配总表     | template |
| TPL-2.2     | 模板         | hardware_spec.md 硬件规格  | template |
| TPL-2.3     | 模板         | sdk_version.md SDK 版本    | template |
| TPL-2.4     | 模板         | datasheet_ref.md 寄存器参考 | template |
| TPL-3.1.1   | 模板         | .cursorrules 项目级规则    | template |
| TPL-3.1.2   | 模板         | 文件头 Anchor 机制         | template |
| TPL-3.5     | 模板         | PROGRESS.md 状态快照       | template |
| CHK-6       | 检查清单     | 开发流程主清单             | checklist |

---

## STEP-0 流程总览

```yaml
id: STEP-0
type: overview
env_prerequisite: |
  所有 idf.py 命令的执行前提：PowerShell 环境已激活。
  → 详见 STEP-3.3 的环境激活序列。
pipeline: |
  [环境激活] → [Data Sheet 信息提取] → [RFC 架构提案] → [黄金参考文档对齐] → [Vibe Coding 闭环]
                                                                              │
                                                            ┌─────────────────┤
                                                            │                 │
                                                      [3.0 增量构建]    [3.6 CI/CD]
                                                      （模块级迭代）    （项目级自动化）
                                                            │
                                            ┌───────────────┼───────────────┐
                                            │               │               │
                                         3.1 强锁      3.2 分层架构    3.3 编译自愈
                                         环境初始化     Host-Native     + 内存验证
                                            │               │               │
                                            └───────────────┼───────────────┘
                                                            │
                                                      3.4 HIL 硬件实测
                                                  （含标准排查顺序 3.4.8）
                                                            │
                                                      3.5 状态快照落盘
                                                      → docs/PROGRESS.md
iteration_rule: >
  每完成一个模块走一遍 3.1→3.5，再进入下一个模块（3.0）。
  所有模块稳定后接入 3.6 CI/CD。增量验证，不攒 Bug。
references: [STEP-3.0, STEP-3.1, STEP-3.2, STEP-3.3, STEP-3.4, STEP-3.5, STEP-3.6]
```

---

## STEP-1 架构提案（RFC）与模块拆分

```yaml
id: STEP-1
type: process
name: 架构提案（RFC）与模块拆分
phase: 架构
trigger: 新项目启动 / 重大功能新增 / 系统架构变更 / 跨模块协作
preconditions:
  - Data Sheet 信息提取已完成（STEP-2.0）
outputs:
  - docs/RFC-xxx.md
verification: "RFC 文档包含全部必填章节"
esp32_specific_notes:
  - "ESP32 为双核架构（PRO_CPU + APP_CPU），RFC 中需标注任务核亲和性"
  - "WiFi/BT 协议栈任务（esp_timer、wifi、ppTcpTx 等）由 ESP-IDF 自动创建，须在内存预算中预留栈空间"
  - "RTC 慢速内存可用于深度睡眠唤醒后的关键数据保留（容量从 Data Sheet 提取）"
  - "SPIRAM/PSRAM 若存在，标注其起始地址与可用容量"
references: [DEC-1.1, TPL-1.2, RULE-2.2a, RULE-2.5]
```

### DEC-1.1 何时写 RFC

```yaml
id: DEC-1.1
type: decision
question: 是否需要写 RFC？
conditions:
  - case: "重大功能（新增通信协议、重构状态机、引入新传感器、新增 WiFi/BLE 功能）"
    action: "需要 RFC，走 STEP-1 + TPL-1.2"
  - case: "系统架构变更（任务划分调整、双核负载重新分配、内存布局重排）"
    action: "需要 RFC"
  - case: "跨模块协作（多个任务共享资源、引入新中间件、WiFi+BLE 共存策略）"
    action: "需要 RFC"
  - case: "小修小补（改个阈值、加个日志、调 Kconfig 默认值）"
    action: "不需要 RFC，直接走 CHK-6 检查清单"
```

### TPL-1.2 RFC 内容模板

```yaml
id: TPL-1.2
type: template
file_path: docs/RFC-xxx.md
purpose: 架构提案文档
required_sections:
  - name: 背景与目标
    description: 为什么做这个、解决什么问题
  - name: 硬件资源映射
    description: GPIO / IO MUX / RTC GPIO / DMA / UART / SPI / I2C / IRQ 分配表
    schema:
      - field: 资源类型
        type: enum [GPIO, RTC_GPIO, IO_MUX, DMA, UART, SPI, I2C, IRQ, ADC, LEDC, RMT]
      - field: 分配
        type: string
      - field: 占用者
        type: string
      - field: 备注
        type: string
  - name: 状态机迁移图
    description: 用文字或 ASCII 画状态迁移，标注触发条件与守卫条件
  - name: 内存预算
    schema:
      - field: 段
        type: enum [Flash（app分区）, DRAM, IRAM, RTC_FAST_MEM, RTC_SLOW_MEM, SPIRAM, 各任务栈]
      - field: 预算
        type: string
      - field: 已用
        type: string
      - field: 余量
        type: string
  - name: 任务划分
    description: FreeRTOS 任务表（ESP-IDF 下所有任务）
    schema:
      - field: 任务
        type: string
      - field: 核亲和性
        type: enum [PRO_CPU (core 0), APP_CPU (core 1), TS_NO_AFFINITY]
      - field: 优先级
        type: int
      - field: 栈大小
        type: string
      - field: 周期/触发
        type: string
      - field: 职责
        type: string
  - name: 分区表规划
    description: ESP32 分区表（partitions.csv）
    schema:
      - field: 分区名
        type: string
      - field: 类型
        type: enum [app, data, phy_init, factory, ota_0, ota_1, nvs, spiffs, fat, coredump]
      - field: 子类型
        type: string
      - field: 偏移
        type: string
      - field: 大小
        type: string
  - name: 配网方案（如使用 WiFi/BLE，可选）
    description: 指定配网方式与 fallback 策略
    schema:
      - field: 配网方式
        type: enum [SoftAP, SmartConfig, BLE Provisioning, WPS——或"无"]
      - field: 配网失败降级
        type: string
      - field: 凭证存储
        type: string (NVS 命名空间+键名)
      - field: 重连策略
        type: string (超时/重试次数/间隔)
  - name: 风险与降级策略
    description: 列出可能失败的环节及对应的安全默认值
    references: [RULE-6.1]
```

---

## STEP-2.0 Data Sheet 信息提取

```yaml
id: STEP-2.0
type: process
name: Data Sheet 信息提取（写代码前第一道工序）
phase: 信息提取
trigger: 拿到 ESP32 芯片 Data Sheet / Technical Reference Manual / 模组规格书 / Schematic PDF
priority: 强制——编写任何代码前必须完成
preconditions:
  - PDF 文件已获取
inputs:
  - ESP32xx Data Sheet PDF
  - ESP32xx Technical Reference Manual PDF
  - 模组规格书 PDF（如 ESP32-WROOM-32E）
  - Schematic PDF
outputs:
  - docs/pinout.md (TPL-2.1)
  - docs/hardware_spec.md (TPL-2.2)
  - docs/sdk_version.md (TPL-2.3)
  - docs/datasheet_ref.md (TPL-2.4)
actions:
  - order: 1
    description: 识别 PDF 类型
    branches:
      - case: Data Sheet
        scope: 电气特性/引脚定义/封装/功耗
      - case: Technical Reference Manual (TRM)
        scope: 寄存器/IO MUX/GPIO Matrix/中断矩阵/DMA/外设编程模型
      - case: 模组规格书
        scope: 模组引脚映射、内置 Flash/PSRAM 容量、天线参数、认证信息
      - case: Schematic
        scope: 实际电路连接
  - order: 2
    description: 查阅 ESP-IDF examples/ 目录（优先于 Data Sheet）
    note: >
      ESP-IDF 自带海量可编译外设示例（$IDF_PATH/examples/），每个外设都有完整 init→read/write→deinit 流程。
      先对照示例代码理解外设的推荐用法，再结合 Data Sheet 深入寄存器层。
      示例路径格式: $IDF_PATH/examples/peripherals/<外设>/main/<外设>_example_main.c
    target: 外设正确初始化序列
  - order: 3
    description: 按提取清单分类提取
    extraction_list:
      - category: 引脚定义
        items: [IO MUX 功能分配, RTC GPIO 功能, Strapping 引脚, ADC2 与 WiFi 冲突引脚, JTAG 默认引脚]
        target: docs/pinout.md
      - category: 寄存器基地址
        items: [外设基地址, 寄存器偏移, 位定义]
        target: docs/datasheet_ref.md
      - category: 时钟树
        items: [XTAL 频率, PLL 倍频路径, CPU 最大频率, APB 总线分频, RTC 时钟源选项]
        target: docs/hardware_spec.md
      - category: 外设映射
        items: [IO MUX 信号映射表, GPIO Matrix 路由, DMA 通道分配, 中断源编号]
        target: docs/datasheet_ref.md
      - category: 内存映射
        items: [内部 ROM/RAM 起始地址与大小, RTC FAST/SLOW 内存, 外部 Flash 映射基址, SPIRAM 映射基址]
        target: docs/hardware_spec.md
      - category: 电气特性
        items: [供电电压范围, I/O 电平标准, 输出驱动能力, 绝对最大额定值, 工作温度, 5V 容忍（查 Data Sheet 确认该型号是否支持）]
        target: docs/hardware_spec.md
      - category: 启动与 Strapping
        items: [Strapping 引脚默认电平与功能对照, 启动模式（Flash/UART Download）, 内置上拉/下拉]
        target: docs/pinout.md
  - order: 4
    description: 校验
    rule: 提取出的寄存器地址与 TRM 的「System and Memory」章节交叉验证；IO MUX 功能与 Data Sheet 的「Pin Layout」交叉验证
  - order: 5
    description: 归档——按类别写入对应的黄金文档
  - order: 6
    description: 标注出处——每个提取的数值标注出处（如「esp32_technical_reference_manual_en.pdf，Chapter 4 IO_MUX, Table 17」）
verification: "全部 7 类信息已提取、校验、归档、标注出处"
on_failure:
  ocr_low_confidence: "标注 ⚠️ OCR-MEDIUM / OCR-LOW，需手动确认"
  cannot_extract: "留空白并用 [MANUAL REQUIRED] 标记，禁止 AI 自行填补"
  version_mismatch: "抛出 [DECISION REQUIRED: 芯片版本/TRM 版本不匹配]，列出差异点，等用户确认"
ai_behavior_constraints:
  - "写外设驱动前必须先读取 docs/datasheet_ref.md"
  - "配置引脚前必须先读取 docs/pinout.md"
  - "设置时钟/电源参数前必须先读取 docs/hardware_spec.md"
references: [TPL-2.1, TPL-2.2, TPL-2.3, TPL-2.4, RULE-1.1, RULE-1.2, RULE-1.3]
```

### TPL-2.1 pinout.md 引脚分配总表

```yaml
id: TPL-2.1
type: template
file_path: docs/pinout.md
purpose: 彻底规避引脚冲突与 Strapping 误用
esp32_specific_concepts:
  - name: IO MUX
    description: 引脚直接连接到指定外设（固定路由），延迟最低。每个 GPIO 有默认 IO MUX 功能（如 GPIO12 默认为 MTDI）
  - name: GPIO Matrix
    description: >
      通过矩阵交换将任意外设信号路由到任意 GPIO，灵活但引入轻微延迟。
      不同 ESP32 芯片对 GPIO Matrix 的支持程度不同（部分新型号简化或去掉了该功能），
      需在 STEP-2.0 从 TRM 确认当前芯片的 IO MUX / GPIO Matrix 模型。
  - name: RTC GPIO
    description: 能在深度睡眠中保持功能的部分 GPIO（用于唤醒源）
  - name: Strapping 引脚
    description: 上电瞬间电平决定芯片启动模式，绝对不能由外设在上电时强制拉高/拉低
required_fields:
  - field: GPIO
    type: string
  - field: IO MUX 默认功能
    type: string
  - field: 本项目功能
    type: string
  - field: 方向
    type: enum [IN, OUT, INOUT]
  - field: 上下拉
    type: string
  - field: 备注
    type: string
special_markings:
  - type: Strapping 引脚
    rule: >
      上电瞬间电平决定芯片启动模式，外设上电时不能给出确定电平。
      具体引脚因芯片型号而异——从 Data Sheet「Strapping Pins」章节提取完整列表写入本文档。
      典型标记：GPIO0（Download 模式）和 MTDI/MTDO 相关引脚。
    extraction_required: true
    source: "Data Sheet → Strapping Pins / System Startup 章节"
  - type: JTAG 默认引脚
    rule: >
      若项目需 JTAG 调试，禁止复用 Data Sheet 标注的 JTAG 默认引脚。
      具体引脚因芯片型号而异（ESP32 为 GPIO12-15，ESP32-S3 为 USB-Serial-JTAG 或专用引脚）。
      从 Data Sheet「JTAG」章节提取。
    extraction_required: true
  - type: ADC2 冲突
    rule: WiFi 开启时 ADC2 不可用，仅 ADC1 可用。从 Data Sheet 确认 ADC1/ADC2 对应的 GPIO 列表
  - type: 仅输入引脚
    rule: 部分 ESP32 芯片存在仅输入的 GPIO（如 ESP32 的 GPIO34-39）。从 Data Sheet 引脚定义表提取
  - type: Flash/PSRAM 占用
    rule: 模组内部 GPIO 通常被 Flash/PSRAM 占用——需查模组规格书确认不可引出使用的引脚列表
hardware_version_rule:
  constraint: "每次 PCB 改动导致引脚分配变化时，HARDWARE_REV 必须递增"
  code_check: "#define HARDWARE_REV 1  // 编译前 AI 必须检查此值与 docs/pinout.md 一致"
  on_mismatch: "抛出 [DECISION REQUIRED: HARDWARE_REV 不匹配]，禁止烧录"
references: [RULE-1.2, STEP-3.1]
```

### TPL-2.2 hardware_spec.md 硬件规格

```yaml
id: TPL-2.2
type: template
file_path: docs/hardware_spec.md
purpose: 让 AI 在内存与性能边界内思考
required_sections:
  - name: 芯片
    fields: [型号, 内核（Xtensa LX6/LX7 或 RISC-V）, 核数, 最大 CPU 频率（从 Data Sheet 提取）, 内部 ROM, 内部 SRAM（DRAM + IRAM 各自大小，从 Data Sheet 提取）, RTC FAST MEM, RTC SLOW MEM]
  - name: 模组
    fields: [模组型号（如 ESP32-WROOM-32E）, Flash 容量, PSRAM 容量（如有）, 天线类型]
  - name: 时钟
    fields: [XTAL 频率（从 Data Sheet 提取）, PLL 路径（从 TRM 时钟树章节提取）, CPU 频率档位, APB 总线频率]
  - name: 电源树
    fields: [3.3V 来源, VDD_SDIO 电压, 峰值电流需求（WiFi TX 时电流较大，从 Data Sheet/模组规格书提取）]
  - name: 外设上电时序
    description: 初始化顺序依赖此表
    schema:
      - field: 外设
        type: string
      - field: 供电来源
        type: string
      - field: 上电稳定时间
        type: string
      - field: 初始化条件
        type: string
      - field: 备注
        type: string
  - name: 分区表
    description: partitions.csv 定义的内容
    fields: [分区名, 类型, 子类型, 偏移, 大小, 标志]
  - name: NVS 存储
    description: 持久化键值存储（ESP32 无 EEPROM，用 NVS 替代）
    fields: [分区名（默认 nvs）, NVS 分区大小, 已规划命名空间列表, 各命名空间的预期键数量与值类型]
  - name: 总线频率限制
    fields: [I2C, SPI, UART, I2S 最大速率]
  - name: WiFi/BT 资源占用
    fields: [协议栈默认栈大小, 需预留的 IRAM/DRAM, ADC2 冲突说明]
references: [STEP-3.1, RULE-2.2c]
```

### TPL-2.3 sdk_version.md SDK 与工具链版本

```yaml
id: TPL-2.3
type: template
file_path: docs/sdk_version.md
purpose: 拦截 90% 的库函数版本冲突与环境问题
required_sections:
  - name: ESP-IDF
    fields: [版本（v5.2/v5.3/v4.4等）, commit hash（精确到版本）, 是否使用 master 分支]
  - name: 架构兼容性
    fields: [Migration Guide 是否已阅读（跨大版本升级时必读，v4→v5 API 破坏性变更较多）, 代码示例/参考项目所用的 ESP-IDF 版本是否与当前一致]
  - name: Python 环境
    fields: [Python 版本, venv 路径: C:\Espressif\tools\python\v6.0.2\venv, pip 包列表（idf-component-manager/esptool 等）]
  - name: 工具链
    fields: [工具链根目录: C:\Espressif\tools, 编译器（xtensa-esp32-elf / riscv32-esp-elf）, CMake 版本, Ninja 版本]
  - name: 环境激活
    fields: [激活脚本: C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1, 执行策略: Set-ExecutionPolicy -Scope Process Bypass]
  - name: 开发板
    fields: [开发板型号（如 ESP32-DevKitC V4）, USB-UART 芯片（CP210x/CH340/FTDI）, 自动下载电路（RTS/DTR 控制 EN+IO0）]
references: [RULE-1.1, STEP-3.3]
```

### TPL-2.4 datasheet_ref.md 寄存器与外设参考

```yaml
id: TPL-2.4
type: template
file_path: docs/datasheet_ref.md
purpose: 写驱动代码时无需反复翻阅 TRM PDF
required_sections:
  - name: 外设基地址表
    description: 每个外设的总线基地址（DPORT / APB）
  - name: IO MUX 寄存器
    description: 每个 GPIO 对应的 IO_MUX 寄存器地址与功能选择位域（FUN_IE / FUN_WPU / FUN_WPD / MCU_SEL）
  - name: GPIO Matrix 寄存器
    description: GPIO_FUNCx_OUT_SEL / GPIO_FUNCx_IN_SEL 等路由寄存器
  - name: 中断矩阵
    description: 外设中断源编号与 CPU 中断输入映射（INTERRUPT_CORE0_xxx / INTERRUPT_CORE1_xxx）
  - name: RTC 寄存器
    description: RTC_CNTL / RTC_IO 区域的关键寄存器（唤醒源/电源状态）
  - name: 定时器寄存器
    description: 硬件定时器（TIMG0/TIMG1）的配置寄存器与 Alarm 寄存器
  - name: DMA 通道
    description: GDMA（ESP32-S3/C3）或传统 DMA（ESP32）的外设请求通道映射
ai_usage_rules:
  - "写 BSP 驱动初始化时，先查外设基地址表，确认偏移无越界"
  - "配置引脚 IO MUX 时，查 IO MUX 寄存器表，确认 FUN_IE/FUN_WPU 配置正确"
  - "分配中断时，查中断矩阵表，确认中断源编号与 CPU 核匹配"
  - "任何手动寄存器操作，数值必须与本文档一致，不一致则抛出 [DECISION REQUIRED]"
references: [STEP-2.0, STEP-3.1]
```

---

## STEP-3.0 增量构建总原则

```yaml
id: STEP-3.0
type: process
name: 增量构建总原则
phase: 模块级迭代
trigger: 每个模块开发开始
constraint: 强制——每完成一个模块就走一遍 3.1→3.5 闭环，再进入下一个模块。禁止一次性写完所有模块再编译。
recommended_order:
  1: 可观测输出（GPIO/LED/串口 Hello World）
  2: 传感器输入（I2C/SPI/ADC）
  3: 交互输入（按键/触摸）
  4: 通信与升级（WiFi/BLE/MQTT/OTA）
cmake_note: >
  每个新模块（.c + .h）必须同步更新所属 component 的 CMakeLists.txt，
  通过 idf_component_register(SRCS "xxx.c" INCLUDE_DIRS "." REQUIRES ...) 注册。
  AI 添加源文件后必须检查 CMakeLists.txt，否则编译时新文件不会被纳入。
rationale: >
  一口气写 2500 行再编译 = 200 条错误一起涌出，信息过载。
  ESP-IDF 全量编译较慢（首次 2-5 分钟），增量编译快（5-30 秒），
  每次只加一个模块，错误不超过 5 条，定位迅速。
verification: "git log 显示每个模块单独 commit"
references: [RULE-3.1, RULE-3.2, RULE-3.3]
```

---

## STEP-3.1 强锁规则与上下文防御

```yaml
id: STEP-3.1
type: process
name: 强锁规则与上下文防御（环境初始化）
phase: 开发闭环 - 步骤 1
trigger: 每个模块开发开始
inputs:
  - docs/hardware_spec.md
  - docs/pinout.md
  - docs/datasheet_ref.md
  - sdkconfig（项目根目录，ESP-IDF 配置系统）
outputs:
  - .cursorrules（项目根目录）
  - 每个 .c 文件头 Anchor
actions:
  - order: 1
    description: 创建/更新 .cursorrules 项目级规则文件
    template: TPL-3.1.1
  - order: 2
    description: 在每个 .c 文件头写入 Anchor
    template: TPL-3.1.2
verification: ".cursorrules 存在且包含必要约束；每个 .c 文件头包含 @anchor 块"
references: [TPL-3.1.1, TPL-3.1.2, RULE-2.1, RULE-2.2a, RULE-2.2b, RULE-2.3a, RULE-2.3b]
```

### TPL-3.1.1 .cursorrules 项目级规则

```yaml
id: TPL-3.1.1
type: template
file_path: .cursorrules（项目根目录）
purpose: 锁定 AI 的行为边界，被 Workspace 自动注入上下文
sections:
  - name: 语言标准
    content: "→ 详见《嵌入式C项目规则.md》第二章 2.1 节（类型系统）"
  - name: 驱动专用
    content: "→ 详见《嵌入式C项目规则.md》第二章 2.1–2.4 节"
  - name: 中断与并发
    content: "→ 详见《嵌入式C项目规则.md》第二章 2.3 节。ESP32 特有：ISR 必须标记 IRAM_ATTR，且不可调用 printf（改用 ESP_DRAM_LOGI）；双核共享数据需 spinlock 或 atomic 操作"
  - name: 内存管理
    content: "→ 详见《嵌入式C项目规则.md》第二章 2.2 节。ESP32 特有：IRAM（指令 RAM）容量有限，ISR 函数必须标记 IRAM_ATTR；大数组/常量需放在 DRAM（数据 RAM）中避免挤占 IRAM，具体用法以 ESP-IDF 文档的 MEMORY 章节为准"
  - name: 初始化顺序
    severity: 强制
    constraints:
      - "写 main() 或 app_main() 前，必须先读取 docs/hardware_spec.md 的「外设上电时序」表"
      - "ESP-IDF 初始化入口为 app_main()，在此之前系统已初始化底层（flash、RTOS、NVS、WiFi/BT 栈如需）"
      - "依赖外部供电的模块初始化失败时必须重试至少 1 次"
    example: "外设传感器 = i2c_master_init() → delay_ms(100) → sensor_check_id() → 失败则再 delay_ms(500) 重试一次"
  - name: Kconfig 配置
    severity: 强制
    constraints:
      - "编译配置通过 `idf.py menuconfig` 管理，写入 sdkconfig 文件"
      - "禁止手动编辑 sdkconfig（由 menuconfig 生成）"
      - "项目级默认配置写入 sdkconfig.defaults（文本格式，可进 Git）"
      - "条件编译使用 CONFIG_xxx 宏，而非手动 #define"
    code_pattern: |
      #if CONFIG_LOG_DEFAULT_LEVEL >= 4  // ESP-IDF 日志级别
          ESP_LOGI(TAG, "...");
      #endif
  - name: 硬件版本管理
    severity: 强制
    constraints:
      - "固件代码中必须定义 #define HARDWARE_REV x"
      - "编译前 AI 必须检查此值与 docs/pinout.md 一致"
      - "版本不匹配 → 抛出 [DECISION REQUIRED: HARDWARE_REV 不匹配]，禁止烧录"
  - name: 构建变体
    severity: 强制
    constraints:
      - "所有项目必须维护 DEBUG 和 RELEASE 两个 sdkconfig 配置"
      - "DEBUG: 日志级别=VERBOSE, 断言=开启, 看门狗=关闭/超时延长, 核心转储=开启"
      - "RELEASE: 日志级别=ERROR/WARN, 断言=关闭(sdkconfig 中 silent), 看门狗=开启, 核心转储=关闭/FLASH最小"
      - "交付/量产前必须用 RELEASE 配置编译并验证"
      - "串口日志中禁止残留 DEBUG/VERBOSE 级别输出"
    kconfig_approach: |
      # sdkconfig.defaults — DEBUG 构建用
      CONFIG_LOG_DEFAULT_LEVEL_VERBOSE=y
      CONFIG_BOOTLOADER_LOG_LEVEL_VERBOSE=y
      CONFIG_ESP_TASK_WDT_EN=n

      # sdkconfig.defaults — RELEASE 构建用
      CONFIG_LOG_DEFAULT_LEVEL_WARN=y
      CONFIG_BOOTLOADER_LOG_LEVEL_WARN=y
      CONFIG_ESP_TASK_WDT_EN=y
      CONFIG_ESP_SYSTEM_PANIC_SILENT_REBOOT=y
  - name: 分区表管理
    severity: 推荐
    constraints:
      - "自定义分区表写入 partitions.csv，路径通过 menuconfig → Partition Table 指定"
      - "分区总大小不超过 docs/hardware_spec.md 中的 Flash 容量"
      - "OTA 场景至少需要 ota_0 + ota_1 + otadata 三个分区"
  - name: 第三方依赖快照
    severity: 推荐
    constraints:
      - "每个 ESP-IDF 组件（component）或第三方库必须记录到 docs/PROGRESS.md 的依赖快照表中"
      - "记录项：库名 / 来源 URL / commit hash / idf_component.yml 版本"
      - "ESP-IDF 组件通过 idf_component.yml 声明依赖，用 `idf.py reconfigure` 拉取"
  - name: AI 行为约束
    constraints:
      - "写外设驱动前必须先读取 docs/datasheet_ref.md"
      - "配置引脚前必须先读取 docs/pinout.md"
      - "设置时钟/电源参数前必须先读取 docs/hardware_spec.md"
      - "编译前必须检查 HARDWARE_REV 与 docs/pinout.md 一致"
      - "编译前必须确认 ESP-IDF 环境已激活（见 STEP-3.3 环境激活序列）"
      - "遇到硬件不确定性时必须抛出 [DECISION REQUIRED] 并停下等用户确认"
      - "修改用户已有代码前必须描述改动点，等确认"
      - "不重写、不「优化」用户未要求动的地方"
references: [RULE-2.1, RULE-2.2a, RULE-2.2b, RULE-2.3a, RULE-2.3b, STEP-3.5]
```

### TPL-3.1.2 文件头 Anchor 机制

```yaml
id: TPL-3.1.2
type: template
purpose: 在每个 .c 文件头写死该文件的强约束，让 AI 每次读取文件时都能看到
schema:
  - field: "@file"
    type: string
    description: 文件名
  - field: "@brief"
    type: string
    description: 简要说明
  - field: "@anchor"
    type: string
    value: ANCHOR_START
  - field: "@rules"
    type: string[]
    description: 该文件的强约束列表
  - field: "@hardware"
    type: string
    description: 关联的硬件引脚/外设
  - field: "@ends"
    type: string
    value: ANCHOR_END
example: |
  /**
   * @file    btn_driver.c
   * @brief   按键驱动状态机（ESP32）
   * @anchor  ANCHOR_START
   * @rules
   *   - 纯 C99，禁 malloc
   *   - ISR 必须 IRAM_ATTR，仅置标志位，状态机在任务中跑
   *   - 消抖窗口 20ms，长按阈值 500ms
   *   - 修改前必须确认 [DECISION REQUIRED]
   * @hardware GPIO0 / KEY1 / 上拉输入 / Strapping 引脚（启动后释放）
   * @ends    ANCHOR_END
   */
```

---

## STEP-3.2 分层架构与 Host-Native 模拟

```yaml
id: STEP-3.2
type: process
name: 分层架构与 Host-Native 模拟（解耦硬件依赖）
phase: 开发闭环 - 步骤 2
trigger: 每个模块开发
architecture:
  - layer: App
    responsibility: 业务逻辑（状态机、滤波、协议解析）
    dependency: 不依赖任何 ESP-IDF API，只调用 Middleware 提供的抽象接口
  - layer: Middleware
    responsibility: 中间件（状态机框架、环形缓冲区、CRC、协议帧）
    dependency: 纯 C，可在 PC 端用 GCC 编译
  - layer: BSP
    responsibility: 板级驱动（ESP-IDF API 封装）
    dependency: 唯一接触硬件的层，PC 端用 Mock 替换
esp32_host_native_note: >
  ESP-IDF 自带 Unity 测试框架，可在 PC 端模拟部分 API。
  但涉及 FreeRTOS / WiFi / NVS / 定时器的模块，Mock 复杂度高，
  建议参照下面的决策表判断。
outputs:
  - bsp_interface.h（App/Middleware 只看这个头文件）
  - bsp_esp32.c（真实硬件实现，调用 ESP-IDF API）
  - bsp_mock.c（PC Mock 实现）
  - tests/*.c（Unity 测试用例）
verification: "PC 端编译运行测试全 pass"
references: [RULE-5.1, RULE-5.2]
```

### Host-Native Mock 适用场景决策表

```yaml
id: DEC-3.2
type: decision
question: 该模块是否适合 Host-Native Mock？
conditions:
  - case: "纯逻辑（状态机、滤波、CRC、帧解析、环形缓冲区）"
    suitable: true
    priority: 非常适合——这是 Host-Native 的核心价值场景，优先覆盖
  - case: "单步外设函数（gpio_set_level、uart_write_bytes、i2c_master_write_read_device）"
    suitable: true
    note: Mock 成本低，一个函数 + 注入数据即可
  - case: "esp_timer 相关逻辑"
    suitable: true
    note: Mock esp_timer_get_time() 推进时间轴即可
  - case: "WiFi / BLE 协议栈态机"
    suitable: false
    note: 协议栈深度耦合 FreeRTOS + NVS + 射频校准，Mock 无意义。直接 HIL
  - case: "复杂外设初始化序列（I2S / SDMMC / USB）"
    suitable: false
    note: 数百行配置代码，Mock 毫无意义。直接 HIL
  - case: "IRAM_ATTR ISR / 临界区保护 / 双核同步"
    suitable: false
    note: 时序依赖真实硬件，PC 端无法模拟
decision_rule: "如果 Mock 实现代码量超过被测代码的 1.5 倍，放弃 Mock，标记为「直接 HIL 验证」"
```

---

## STEP-3.3 AI Agent 接入编译工具链

```yaml
id: STEP-3.3
type: process
name: AI Agent 接入编译工具链（自动编译与自愈）
phase: 开发闭环 - 步骤 3
trigger: 代码修改完成
priority: >
  本节是 ESP32 版本的核心差异点。
  所有 idf.py 命令的前置条件是 ESP-IDF 环境已激活。
  未激活环境直接执行 idf.py 会报 "idf.py: command not found"。
inputs:
  - docs/hardware_spec.md（获取芯片型号与目标）
  - docs/sdk_version.md（获取 ESP-IDF 版本与工具链路径）
outputs:
  - 编译日志（build/log 目录）
  - build/sdkconfig（配置快照）
  - build/flash_args（烧录地址与分区信息）
```

### 环境激活序列（强制前置）

```yaml
id: STEP-3.3-ENV
type: process
name: ESP-IDF 环境激活
phase: 编译前必须执行
constraint: 每次新的 PowerShell 会话必须重新激活。激活脚本设置 PATH、Python venv、工具链等所有环境变量。
platform: Windows PowerShell
steps:
  - order: 1
    description: 解除当前 PowerShell 进程的执行策略限制
    command: "Set-ExecutionPolicy -Scope Process Bypass"
    rationale: >
      Windows 默认 Restricted 执行策略阻止所有 .ps1 脚本。
      此命令仅对当前 PowerShell 进程生效（作用域 Scope Process），关闭窗口即恢复默认策略，无系统级副作用。
    verify: "Get-ExecutionPolicy -Scope Process 返回 Bypass"
  - order: 2
    description: 激活 ESP-IDF 环境（导入 PATH、Python venv、工具链等）
    command: "source C:\\Espressif\\tools\\Microsoft.v6.0.2.PowerShell_profile.ps1"
    bash_note: "若在 Git Bash 中使用，确认 PowerShell_profile.ps1 兼容——否则改用 PowerShell 终端"
    verify: "idf.py --version 返回版本号（如 ESP-IDF v5.3.1）"
    on_failure:
      - "idf.py 未找到 → 激活脚本未生效，检查脚本路径与内容"
      - "Python 找不到 → venv 路径变更，检查 C:\\Espressif\\tools\\python\\v6.0.2\\venv 是否存在"
    one_time_check: "若首次使用 ESP-IDF，可能还需在 ESP-IDF 目录运行 install.ps1 安装工具链依赖"
  - order: 3
    description: 确认目标芯片
    command: "idf.py set-target esp32"
    variants: [esp32s2, esp32s3, esp32c3, esp32c6, esp32h2]
    note: 首次执行即可，后续编译记住目标
```

### 编译与自愈闭环

```yaml
id: STEP-3.3-BUILD
type: process
name: 编译自愈闭环
toolchain_selection:
  chip_table:
    - chip: ESP32
      sdk: ESP-IDF
      target: esp32
      compiler: xtensa-esp32-elf-gcc
    - chip: ESP32-S2
      sdk: ESP-IDF
      target: esp32s2
      compiler: xtensa-esp32s2-elf-gcc
    - chip: ESP32-S3
      sdk: ESP-IDF
      target: esp32s3
      compiler: xtensa-esp32s3-elf-gcc
    - chip: ESP32-C3 / ESP32-C6 / ESP32-H2
      sdk: ESP-IDF
      target: esp32c3 / esp32c6 / esp32h2
      compiler: riscv32-esp-elf-gcc
build_commands:
  full_build: "idf.py build"
  incremental_build: "idf.py build  （ESP-IDF 自动增量编译）"
  clean_build: "idf.py fullclean build"
  fullclean_caveat: >
    警告：idf.py fullclean 会删除 build/ 目录，目标芯片设置随之丢失。
    fullclean 后必须重新执行 idf.py set-target esp32（或对应型号），否则编译报错 "Target is not set"。
  flash: "idf.py -p COM3 flash"
  monitor: "idf.py -p COM3 monitor"
  build_flash_monitor: "idf.py -p COM3 build flash monitor"
  size: "idf.py size"
  size_components: "idf.py size-components"
  size_files: "idf.py size-files"
self_healing_loop:
  description: 编译自愈闭环
  max_iterations: 5
  steps:
    - order: 1
      action: AI 修改完代码后，立即调用 idf.py build
    - order: 2
      action: 解析编译日志中的 Error 和 Warning（ESP-IDF 输出行号清晰：main/foo.c:42: error: ...）
    - order: 3
      condition: 有 Error
      action: 定位文件:行号，原位修复代码，重新编译
    - order: 4
      condition: 有 Warning
      action: 同样修复（规范要求 -Werror 级别，警告即错误）
    - order: 5
      condition: 编译通过
      action: 运行 idf.py size 检查 Flash/DRAM/IRAM 占用
    - order: 6
      condition: 超出 docs/hardware_spec.md 预算（如 DRAM 超过可用大小）
      action: AI 必须停下，抛出 [DECISION REQUIRED: 内存超限]
    - order: 7
      condition: 循环超过 5 轮仍未通过
      action: 停止自愈并报告根因
verification: "零 Error 零 Warning + Flash/DRAM 未超预算 + 分区表总大小不超 Flash 容量"
references: [RULE-3.1, RULE-3.2, RULE-9.3]
```

---

## STEP-3.4 串口与日志闭环（HIL 硬件实测与自愈）

```yaml
id: STEP-3.4
type: process
name: 串口与日志闭环（HIL 硬件实测与自愈）
phase: 开发闭环 - 步骤 4
trigger: 编译通过后
inputs:
  - 编译产物（build/flash_args / build/bootloader/bootloader.bin / build/partition_table/partition-table.bin / build/app-name.bin）
  - ESP32 开发板（USB 连接）
  - 串口号（如 COM3）
outputs:
  - 串口日志
  - Guru Meditation / Panic 分析报告（如有）
actions:
  - order: 1
    description: 烧录固件
    command: "idf.py -p COM3 flash"
    note: >
      ESP32 烧录通过 esptool.py（idf.py flash 内部调用）。
      若开发板有自动下载电路（RTS/DTR→EN+IO0），无需手动按键进入下载模式。
      若没有自动电路，需手动操作：按住 BOOT → 按一下 EN → 松开 BOOT。
    verify: "串口输出 'Hash of data verified' 确认烧录成功"
  - order: 2
    description: 启动串口监控（固化在 build→flash→monitor 一键流程中）
    command: "idf.py -p COM3 monitor"
    uart_flow:
      - "idf.py monitor 自动连接串口（115200bps 8N1）"
      - "实时输出 ESP_LOGI/ESP_LOGW/ESP_LOGE 日志"
      - "Ctrl+] 退出 monitor"
      - "正常日志 → 按分级（ERROR/WARN/INFO/DEBUG）汇总"
      - "异常日志 → 触发自愈链路"
  - order: 3
    description: 固件侧日志分级（ESP-IDF 内置）
    log_macros:
      - level: ERROR
        macro: 'ESP_LOGE(TAG, "format", ...)  // 红色，始终输出'
      - level: WARN
        macro: 'ESP_LOGW(TAG, "format", ...)  // 黄色'
      - level: INFO
        macro: 'ESP_LOGI(TAG, "format", ...)  // 默认级别'
      - level: DEBUG
        macro: 'ESP_LOGD(TAG, "format", ...)  // 需 CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y'
      - level: VERBOSE
        macro: 'ESP_LOGV(TAG, "format", ...)  // 需 CONFIG_LOG_DEFAULT_LEVEL_VERBOSE=y'
    ai_parsing: "通过 idf.py monitor 或 UART MCP 读取日志后，按 E/W/I/D/V 前缀自动分类，E 级别自动触发自愈分析"
  - order: 4
    description: Guru Meditation / Panic 崩溃定位
    condition: "日志中出现 'Guru Meditation Error' 或 'abort() was called' 或 Backtrace"
    esp32_panic_types:
      - type: "IllegalInstruction"
        cause: 跳转到非代码区 / IRAM 数据被当代码执行
      - type: "StoreProhibited / LoadProhibited"
        cause: 空指针解引用 / 访问不可写区域（最常见）
      - type: "Stack canary watchpoint triggered"
        cause: 任务栈溢出（栈金丝雀被覆盖）
      - type: "IntegerDivideByZero"
        cause: 除零异常
      - type: "abort() was called"
        cause: assert 失败（CONFIG_COMPILER_OPTIMIZATION_ASSERTION_LEVEL 不静默时）
    auto_recovery:
      1: "idf.py monitor 输出自动解码 Backtrace（需将 Panic 行为设为 PRINT_HALT 或 PRINT_REBOOT: CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT 或 CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT）"
      2: "若 monitor 未自动解码，手动执行: xtensa-esp32-elf-addr2line -pfiaC -e build/app-name.elf 0x400d1234 0x400d5678 ..."
      3: "读取崩溃位置的源代码"
      4: "分析根因（空指针解引用 / 栈溢出 / 数组越界 / 除零 / ISR 嵌套过深）"
      5: "提交 Patch，重新走 STEP-3.3 → STEP-3.4 验证"
  - order: 5
    description: FreeRTOS 栈溢出检测（ESP-IDF 内置）
    condition: "使用 FreeRTOS"
    esp32_mechanism: |
      // ESP-IDF 栈溢出检测配置: CONFIG_FREERTOS_CHECK_STACK_OVERFLOW
      // 菜单路径: （Top）→ Component config → FreeRTOS → Kernel → Enable stack overflow checking
      // 选项: "None"（关闭）/ "Normal"（OS 检查）/ "Comprehensive"（ISR 栈也检查，增加开销）
      // 栈溢出时触发 panic: "Stack canary watchpoint triggered"
  - order: 6
    description: 看门狗与复位原因记录
    esp32_mechanism:
      wdt: |
        // TWDT（Task Watchdog Timer）——由 ESP-IDF 自动管理
        // CONFIG_ESP_TASK_WDT_EN=y 时自动监控注册的任务
        // 任务超时未喂狗 → 打印 "Task watchdog got triggered" → panics
        // 自定义任务需 esp_task_wdt_add() / esp_task_wdt_reset()
      reset_reason: |
        #include "esp_system.h"
        void check_reset_reason(void) {
            esp_reset_reason_t reason = esp_reset_reason();
            switch (reason) {
                case ESP_RST_POWERON:   ESP_LOGI(TAG, "Reset: power-on"); break;
                case ESP_RST_EXT:       ESP_LOGI(TAG, "Reset: external pin"); break;
                case ESP_RST_SW:        ESP_LOGI(TAG, "Reset: software"); break;
                case ESP_RST_PANIC:     ESP_LOGE(TAG, "Reset: panic/exception"); break;
                case ESP_RST_INT_WDT:   ESP_LOGE(TAG, "Reset: interrupt watchdog"); break;
                case ESP_RST_TASK_WDT:  ESP_LOGE(TAG, "Reset: task watchdog"); break;
                case ESP_RST_WDT:       ESP_LOGE(TAG, "Reset: other watchdog"); break;
                case ESP_RST_DEEPSLEEP: ESP_LOGI(TAG, "Reset: deep sleep wake"); break;
                case ESP_RST_BROWNOUT:  ESP_LOGE(TAG, "Reset: brownout"); break;
                case ESP_RST_SDIO:      ESP_LOGI(TAG, "Reset: SDIO"); break;
                default:                ESP_LOGI(TAG, "Reset: unknown reason %d", reason);
            }
        }
verification: "串口日志正常输出 + 无 E 级别日志 + 复位原因记录正常"
references: [RULE-4.1, RULE-4.2, RULE-8.2, RULE-8.3, RULE-5.3, RULE-5.4]
```

### STEP-3.4.8 标准排查顺序

```yaml
id: STEP-3.4.8
type: process
name: 标准排查顺序
phase: HIL 实测异常排查
trigger: HIL 实测出现异常
constraint: 禁止一上来就怀疑硬件
steps:
  - order: 1
    name: 看日志
    operation: 串口是否有输出？日志级别是否正确？idf.py monitor 是否正常连接？
    pass_criteria: 有输出且分级正确→芯片正常，进入第 2 步
    example: "WiFi 连不上但 GPIO 闪烁正常→硬件没问题，检查 WiFi 配置"
  - order: 2
    name: 查初始化
    operation: 关键模块的 init 函数是否被调用？外设初始化顺序是否按 hardware_spec.md 上电时序执行？
    pass_criteria: 用 ESP_LOGI 包围可疑函数，确认每个 init 都被执行
    example: "i2c_master_init() 未被调用→加一行即修复"
  - order: 3
    name: 追数据流
    operation: 数据是否从源头走到了目的地？
    pass_criteria: 在队列 xQueueSend/xQueueReceive 两边加日志
    example: "传感器读数不更新→从 I2C 读函数往回追踪到引脚配置错误"
  - order: 4
    name: 量信号
    operation: 用逻辑分析仪/示波器看关键引脚波形、总线时序
    pass_criteria: 对比 TRM 时序图
    example: "I2C SCL 无波形→IO MUX 未正确配置为 I2C 功能"
  - order: 5
    name: 换硬件
    operation: 更换模块/开发板/检查供电
    pass_criteria: 确认前四步都排除后才执行
    esp32_note: "尤其检查 USB 线是否为数据线（非仅充电线），供电是否满足 WiFi 发射时的峰值电流需求（参考 hardware_spec.md）"
references: [RULE-4.1, RULE-4.2, RULE-4.3]
```

---

## STEP-3.5 状态快照落盘

```yaml
id: STEP-3.5
type: process
name: 状态快照落盘
phase: 开发闭环 - 步骤 5
trigger: 每完成一个模块（走完 3.1→3.4 并 git commit 后）
constraint: 强制——AI 必须更新 docs/PROGRESS.md
purpose: 项目的「运行时上下文」，新会话启动时直接读取，不需重新扫描代码
outputs:
  - docs/PROGRESS.md
update_rules:
  - trigger: 每个模块 git commit 后
    action: 更新已实现模块表
  - trigger: 引脚占用发生变化
    action: 更新引脚占用总表
  - trigger: 发现新 BUG
    action: 记录到遗留 BUG 表，标注严重级别（P0/P1/P2）
  - trigger: BUG 修复
    action: 状态改为已修复，标注关联 commit
  - trigger: 做出架构决策
    action: 追加到 ADR 表
  - trigger: ESP-IDF 组件（component）版本更新
    action: 追加到依赖快照表（含 idf_component.yml 版本 + 来源）
  - trigger: sdkconfig 配置变更
    action: 记录到配置变更表（仅记录手动修改的项，menuconfig 生成的不记录）
  - trigger: 每次更新
    action: 更新「最后更新」时间戳
new_session_load_order:
  1: docs/PROGRESS.md
  2: docs/pinout.md
  3: docs/datasheet_ref.md
  4: sdkconfig.defaults（项目级默认配置）
  5: .cursorrules
  6: 然后才是读具体代码
references: [TPL-3.5, RULE-10.5]
```

### TPL-3.5 PROGRESS.md 状态快照

```yaml
id: TPL-3.5
type: template
file_path: docs/PROGRESS.md
purpose: 项目进度快照
required_sections:
  - name: 已实现模块
    schema:
      - field: 模块
        type: string
      - field: 状态
        type: enum [✅完成, 🔄进行中, ⏳未开始]
      - field: 文件
        type: string
      - field: 最后更新
        type: date
      - field: 备注
        type: string
  - name: 引脚占用总表
    schema:
      - field: GPIO
        type: string
      - field: 功能
        type: string
      - field: 状态
        type: enum [已占用, 空闲]
      - field: Strapping/JTAG标记
        type: string
      - field: 备注
        type: string
  - name: 遗留 BUG
    schema:
      - field: ID
        type: string
      - field: 严重级别
        type: enum [P0-严重, P1-中等, P2-低]
      - field: 描述
        type: string
      - field: 复现条件
        type: string
      - field: 状态
        type: enum [🔴待修复, 🟡有降级方案, ✅已修复]
      - field: 关联commit
        type: string
  - name: 内存占用
    schema:
      - field: 段
        type: enum [Flash（app分区）, DRAM, IRAM, RTC_FAST, RTC_SLOW, SPIRAM]
      - field: 预算
        type: string
      - field: 实际占用
        type: string
      - field: 余量
        type: string
      - field: 状态
        type: enum [🟢正常, 🟡偏低, 🔴超限]
  - name: 分区表快照
    schema:
      - field: 分区名
        type: string
      - field: 类型
        type: string
      - field: 子类型
        type: string
      - field: 偏移
        type: string
      - field: 大小
        type: string
      - field: 状态
        type: enum [🟢正常, 🟡已修改, 🔴超限]
  - name: ESP-IDF 组件依赖快照
    schema:
      - field: 组件名
        type: string
      - field: 用途
        type: string
      - field: 版本
        type: string
      - field: 来源
        type: string
      - field: 最后验证
        type: date
  - name: 架构决策记录 (ADR)
    schema:
      - field: 日期
        type: date
      - field: 决策
        type: string
      - field: 原因
        type: string
      - field: 影响范围
        type: string
```

---

## STEP-3.6 项目级自动化：CI/CD 构建流水线

```yaml
id: STEP-3.6
type: process
name: CI/CD 构建流水线
phase: 项目级自动化
trigger: 所有模块稳定后
pipeline: "Git Push → 触发 CI → Docker 容器 espressif/idf 构建 → 静态分析 → 单元测试 → HIL 测试 → 生成固件包 → 归档"
stages:
  - stage: 容器化构建
    method: "Docker 镜像: espressif/idf（官方，含完整工具链 + Python venv）"
    goal: 消除「我这台机器能编译」问题
    example: "docker run --rm -v $PWD:/project -w /project espressif/idf:v5.3.1 idf.py build"
  - stage: 静态分析
    method: Cppcheck/Clang-Tidy 自动扫描
    goal: 不达标→拒绝 merge
  - stage: 单元测试
    method: 在宿主机上跑逻辑测试（参考 STEP-3.2 Host-Native）
    goal: 驱动层 Mock，业务层真实测试
  - stage: HIL 测试
    method: 真实 ESP32 板卡在机架上自动烧录+测试（参考 STEP-3.4）
    goal: 还原真实硬件环境
  - stage: 固件签名
    method: 用 espsecure.py sign_data 对固件签名
    goal: 私钥不进 CI 环境
minimum_viable_ci:
  description: 即使没有 HIL 机架，也可以做到的三步
  steps:
    - order: 1
      name: GitHub Actions 自动编译
      action: "每次 push 触发 espressif/idf Docker 容器内 idf.py build"
      fail_condition: 编译失败→PR 被标记为红色
    - order: 2
      name: Cppcheck 自动扫描
      action: 作为 CI 流水线的一个步骤
      fail_condition: 警告数 > 0 → 构建失败
    - order: 3
      name: 固件包自动上传
      action: 构建产物（.bin + partitions + bootloader）作为 GitHub Release Artifact 保存
      goal: 每个版本可追溯
verification: "CI 管线存在 + 最近一次 push 触发了构建"
references: [RULE-9.3, RULE-10.1, RULE-11.0]
```

---

## STEP-3.7 OTA 更新流程

```yaml
id: STEP-3.7
type: process
name: OTA（Over-The-Air）固件更新
phase: 项目级功能
trigger: 项目需要远程固件更新能力
preconditions:
  - 分区表已配置 ota_0 + ota_1 + otadata（见 TPL-1.2 分区表规划）
  - WiFi 联网功能已稳定
design_checklist:
  - step: 分区设计
    items:
      - "ota_0 和 ota_1 大小相同，各至少容纳 app 固件 + core dump"
      - "otadata 分区至少 8KB（2 个 sector），记录当前激活分区与启动计数"
      - "factory 分区保留出厂固件（可选但推荐），作为 OTA 失败的最后降级路径"
  - step: 固件下载
    items:
      - "通过 HTTPS 下载固件（禁止 HTTP）"
      - "下载前校验服务器证书（mbedTLS，ESP-IDF 内置）"
      - "校验固件完整性（SHA256 checksum 或签名）"
      - "下载期间不应阻塞核心任务，建议独立 task 或 event handler"
  - step: 固件写入
    flow: |
      esp_ota_get_next_update_partition()  // 获取 ota_1 分区句柄
        → esp_ota_begin()                  // 开始写入
        → esp_ota_write()                  // 分块写入（建议 4KB/块）
        → esp_ota_end()                    // 完成 + 校验
        → esp_ota_set_boot_partition()     // 切换启动分区
  - step: 回滚机制
    items:
      - "新固件启动后调用 esp_ota_mark_app_valid_cancel_rollback() 确认有效"
      - "若新固件持续重启（未调上述函数），bootloader 自动回滚到旧分区"
      - "回滚次数超限后回退到 factory 分区（如存在）"
      - "固件版本号通过 esp_app_desc_t 管理，版本号必须递增（新固件版本号 > 当前）"
  - step: 安全性
    items:
      - "量产固件必须启用 Secure Boot V2 + Flash Encryption"
      - "签名私钥不进固件、不进 CI 环境"
  - step: 测试要求
    items:
      - "正常 OTA 测试：旧版 → 下载 → 新固件启动成功"
      - "回滚测试：故意烧录坏固件 → 确认自动回滚 → 旧版正常工作"
      - "断网恢复测试：下载中途断开 WiFi → 重新连接 → 续传或重下"
      - "版本号降级拒绝测试：推送比当前版本低的固件 → 固件拒绝写入"
verification: "OTA 成功 + 回滚正常 + 断网恢复 + 版本号降级被拒绝"
references: [RULE-3.3, TPL-1.2, STEP-3.6]
```

---

## DEC-5 流程裁减指南

```yaml
id: DEC-5
type: decision
question: 新项目应该走哪个流程级别？
levels:
  - level: L1-原型验证
    scope: 点亮 LED、WiFi Scan 测试、传感器读数验证、快速概念证明（<500 行、单人、<1 天）
    required_steps: [.cursorrules 强锁, pinout.md 简化版（至少标注 Strapping 引脚）, sdkconfig.defaults]
    skippable: [RFC, datasheet_ref.md, Host-Native 测试, PROGRESS.md]
    flow: 激活环境 → idf.py set-target → 写代码 → idf.py build flash monitor → 看串口日志
  - level: L2-单板 Demo
    scope: 功能演示原型、课程设计、比赛作品（500-3000 行、含多外设和任务、WiFi/BLE 任一）
    required_steps: [RFC简化版, 全部黄金文档, .cursorrules, 编译自愈, 串口日志, PROGRESS.md, sdkconfig.defaults]
    skippable: [Host-Native 测试——仅对状态机和关键算法做，驱动层跳过]
  - level: L3-量产固件
    scope: 产品级固件、OTA/安全启动/低功耗/量产烧录、多人协作、长周期维护（>3000 行）
    required_steps: [全部——完整 RFC, Data Sheet 提取+降级规则, 全部黄金文档, .cursorrules, 分层架构+Host-Native 全覆盖, 编译自愈+HIL 闭环, PROGRESS.md, 24h 稳定性测试, OTA 回滚方案（见 STEP-3.7）, Secure Boot V2 签名, Flash Encryption]
    skippable: 无
ai_decision_logic:
  1: "首次接触项目时，先询问项目预期规模（代码量、维护周期、交付要求）"
  2: "用户说「试一下」「看看能不能亮」「快速验证」→ L1"
  3: "用户说「这个课设/比赛要交」「做个能演示的原型」→ L2"
  4: "用户说「产品」「量产」「要卖出去/交付客户」→ L3"
  5: "无法判断 → 默认 L2，但主动询问是否降级"
```

---

## CHK-6 开发流程检查清单（主清单）

```yaml
id: CHK-6
type: checklist
description: >
  编码规则层面的检查项见《嵌入式C项目规则_结构化.md》CHK-7。
  本清单侧重流程步骤，两者互补使用。
groups:
  - group: A. 项目启动
    level_note: "L3 全部；L2 前三项可简化；L1 仅需 pinout.md 简化版 + .cursorrules + sdkconfig.defaults"
    items:
      - id: A1
        description: "ESP-IDF 版本已精确确认，已阅读 Migration Guide（跨大版本升级时必读）"
      - id: A2
        description: "Data Sheet / Technical Reference Manual PDF 已提取"
        reference: STEP-2.0
      - id: A3
        description: "提取信息已校验（寄存器地址与 TRM Memory Map 交叉验证）"
        reference: STEP-2.0
      - id: A4
        description: "docs/datasheet_ref.md 完成"
        reference: TPL-2.4
      - id: A5
        description: "docs/pinout.md 完成（含 Strapping/JTAG/RTC GPIO 标注，含 HARDWARE_REV）"
        reference: TPL-2.1
      - id: A6
        description: "docs/hardware_spec.md 完成（含外设上电时序表 + 分区表规划 + NVS 存储规划）"
        reference: TPL-2.2
      - id: A7
        description: "docs/sdk_version.md 完成（含 ESP-IDF 版本 + Python venv 路径 + 环境激活命令）"
        reference: TPL-2.3
      - id: A8
        description: "第三方 ESP-IDF 组件可用性已验证"
      - id: A9
        description: "RFC 已编写（含分区表规划 + 任务核亲和性标注 + 配网方案【如需WiFi/BLE】）"
        reference: TPL-1.2
      - id: A10
        description: ".cursorrules 已放置项目根目录"
        reference: TPL-3.1.1
      - id: A11
        description: "ESP-IDF 环境激活已确认可用（idf.py --version 返回正常）"
        reference: STEP-3.3-ENV

  - group: B. 模块开发（每个模块走一遍）
    items:
      - id: B1
        description: "文件头 Anchor 已写（@rules / @hardware）"
        reference: TPL-3.1.2
      - id: B1.5
        description: "CMakeLists.txt 已注册新增源文件（idf_component_register SRCS）"
        reference: STEP-3.0
      - id: B2
        description: "App / Middleware 层不依赖 ESP-IDF API，可用 PC 编译"
        reference: STEP-3.2
      - id: B3
        description: "BSP 接口抽象完成，Mock 实现就绪"
        reference: STEP-3.2
      - id: B4
        description: "Host-Native 单元测试全 pass"
        reference: STEP-3.2
      - id: B5
        description: "idf.py build 零 Error 零 Warning（-Werror 级别）"
        reference: STEP-3.3
      - id: B6
        description: "idf.py size 确认 DRAM/IRAM/Flash（app分区）未超预算"
        reference: STEP-3.3
      - id: B7
        description: "idf.py monitor 串口日志正常输出"
        reference: STEP-3.4
      - id: B8
        description: "Guru Meditation / Panic Handler 已配置（backtrace 打印开启 + 核心转储）"
        reference: STEP-3.4
      - id: B9
        description: "复位原因记录已实现（esp_reset_reason()），看门狗已配置"
        reference: STEP-3.4
      - id: B10
        description: "外设初始化顺序按 hardware_spec.md 上电时序表执行，含稳定延迟"
        reference: TPL-3.1.1
      - id: B11
        description: "HARDWARE_REV 与 docs/pinout.md 一致"
        reference: TPL-2.1
      - id: B12
        description: "模块通过 git commit"
        reference: STEP-3.0
      - id: B13
        description: "docs/PROGRESS.md 已更新（含分区表快照）"
        reference: TPL-3.5

  - group: C. 编码规则自检
    items:
      - "所有类型使用 <stdint.h>，无魔数"
      - "指针解引用前 NULL 检查，数组传入时附带大小参数"
      - "switch 包含 default 分支，失败操作有安全默认值"
      - "禁止 malloc/sprintf/strcpy，静态分配所有缓冲区"
      - "ISR 函数标记 IRAM_ATTR，极短（<50us），仅置标志位"
      - "双核共享数据使用 spinlock 或 portENTER_CRITICAL，不用 volatile 裸变量"
      - "可变参数通过 Kconfig（menuconfig）管理，写入 sdkconfig.defaults"
      - "RELEASE 构建看门狗已开，WiFi/BT 栈栈空间已预留"

  - group: D. AI 协作纪律
    items:
      - id: D1
        description: "AI 遇硬件不确定性抛出 [DECISION REQUIRED]，未自行推进"
      - id: D2
        description: "AI 修改前已描述改动点并获确认"
      - id: D3
        description: "AI 未重写、未「优化」未要求动的地方"
      - id: D4
        description: "AI 编译自愈循环 ≤ 5 轮即收敛"
        reference: STEP-3.3

  - group: E. 硬件验证
    items:
      - id: E1
        description: "串口日志分级正确（ESP_LOGE/ESP_LOGW/ESP_LOGI/ESP_LOGD）"
        reference: RULE-4.2
      - id: E2
        description: "Guru Meditation 现场提取 + addr2line 反查链路打通"
        reference: STEP-3.4
      - id: E3
        description: "复位原因记录正常 + TWDT 看门狗配置生效"
        reference: STEP-3.4
      - id: E4
        description: "24 小时连续运行稳定性测试通过"
        reference: RULE-5.3
      - id: E5
        description: "RELEASE 配置编译验证通过（日志级别、看门狗、assert 等均正确）"
        reference: TPL-3.1.1
      - id: E6
        description: "分区表总大小不超 Flash 容量 + OTA 双分区充足"
        reference: RULE-3.3

  - group: F. 交付前
    items:
      - id: F1
        description: "ESP_LOGD/ESP_LOGV 日志已关闭或调整为 INFO 级别"
      - id: F2
        description: "固件版本号已递增（通过 Kconfig 或 app version），所有 TODO/FIXME 已处理"
      - id: F3
        description: "所有 ESP-IDF 组件依赖已记录到 PROGRESS.md 依赖快照表"
        reference: TPL-3.5
      - id: F4
        description: "SBOM（软件物料清单）已维护"

  - group: G. OTA 更新（如适用）
    items:
      - id: G1
        description: "分区表含 ota_0 + ota_1 + otadata，大小充足"
        reference: STEP-3.7
      - id: G2
        description: "固件下载使用 HTTPS + 证书校验"
        reference: STEP-3.7
      - id: G3
        description: "esp_ota_begin/write/end 完整链路 + 错误处理"
        reference: STEP-3.7
      - id: G4
        description: "回滚机制正常（新固件崩溃 → 自动回退 → 旧版正常启动）"
        reference: STEP-3.7
      - id: G5
        description: "版本号降级被拒绝（旧版本固件无法覆盖新版本）"
        reference: STEP-3.7
      - id: G6
        description: "Secure Boot V2 + Flash Encryption（量产项目）"
        reference: STEP-3.7
references: [CHK-7]
```

---

## 附录 A 工具链速查（ESP32 + ESP-IDF）

```yaml
id: REF-A
type: 参考数据表
description: >
  聚焦 ESP32 + ESP-IDF 场景。
  所有命令的前提是 ESP-IDF 环境已激活（见 A.1）。
platform: Windows PowerShell
sections:
  - name: A.0 环境激活（每次新建终端必须执行）
    steps:
      - order: 1
        name: 解除执行策略
        command: "Set-ExecutionPolicy -Scope Process Bypass"
        verify: "Get-ExecutionPolicy -Scope Process 返回 Bypass"
      - order: 2
        name: 激活 ESP-IDF
        command: "source C:\\Espressif\\tools\\Microsoft.v6.0.2.PowerShell_profile.ps1"
        verify: "idf.py --version 返回版本号"
      - order: 3
        name: （首次）安装工具链
        command: "cd $env:IDF_PATH; ./install.ps1"
        note: 仅首次或切换 ESP-IDF 版本时需要

  - name: A.1 编译
    commands:
      - cmd: idf.py set-target esp32
        use: 设置目标芯片（esp32/esp32s2/esp32s3/esp32c3/esp32c6/esp32h2）
        note: 首次执行即可，后续记住
      - cmd: idf.py build
        use: 增量编译（推荐日常使用）
      - cmd: idf.py fullclean build
        use: 全量重编译（sdkconfig 大改或编译缓存异常时）
      - cmd: idf.py reconfigure
        use: 重新生成 CMake 构建文件（组件依赖变更后）

  - name: A.2 烧录
    commands:
      - cmd: idf.py -p COM3 flash
        use: 烧录到指定串口
      - cmd: idf.py -p COM3 -b 921600 flash
        use: 高波特率烧录（默认 460800，支持 USB-Serial-JTAG 的芯片可达 921600+）
      - cmd: esptool.py -p COM3 write_flash 0x0 merged-firmware.bin
        use: 烧录合并固件（量产用）

  - name: A.3 串口监控与日志
    commands:
      - cmd: idf.py -p COM3 monitor
        use: 启动串口监控（115200bps 8N1，自动解码 backtrace）
      - cmd: idf.py -p COM3 build flash monitor
        use: 编译+烧录+监控 一键流程
      - cmd: idf.py -p COM3 monitor --print-filter="*:I"
        use: 过滤器（仅打印 INFO 及以上级别）
      - cmd: Ctrl+]
        use: 退出 monitor 模式

  - name: A.4 内存分析
    commands:
      - cmd: idf.py size
        use: 总览（DRAM/IRAM/Flash 各段占用，类似 arm-none-eabi-size）
      - cmd: idf.py size-components
        use: 按组件（每个 ESP-IDF 组件）分类显示占用
      - cmd: idf.py size-files
        use: 按 .o 文件分类显示占用
      - cmd: idf.py size-components --format json
        use: JSON 格式输出，方便 CI 解析

  - name: A.5 崩溃分析（Guru Meditation / Panic）
    commands:
      - cmd: "xtensa-esp32-elf-addr2line -pfiaC -e build/app-name.elf 0x400d1234 0x400d5678 ..."
        use: 手动反查 Backtrace 地址（ESP32/S2/S3 用 xtensa 前缀）
      - cmd: "riscv32-esp-elf-addr2line -pfiaC -e build/app-name.elf 0x42001234 ..."
        use: RISC-V 芯片（C3/C6/H2）用 riscv32 前缀
      - note: "idf.py monitor 已自动解码 Backtrace，多数情况不需手动调用 addr2line"
    coredump:
      - cmd: "idf.py coredump-info build/coredump.bin"
        use: 从 Flash 分区提取核心转储并解析

  - name: A.6 调试（JTAG / OpenOCD + GDB）
    commands:
      - cmd: openocd -f board/esp32s3-builtin.cfg
        use: 启动 OpenOCD（ESP32-S3 内置 USB-Serial-JTAG）
      - cmd: "xtensa-esp32s3-elf-gdb build/app-name.elf -ex 'target remote :3333'"
        use: 连接 GDB
      - cmd: openocd -f interface/ftdi/esp32_devkitj_v1.cfg -f target/esp32.cfg
        use: 启动 OpenOCD（ESP32 外置 JTAG 调试器）

  - name: A.7 配置系统（Kconfig / menuconfig）
    commands:
      - cmd: idf.py menuconfig
        use: 图形化配置（终端 GUI，生成 sdkconfig）
      - cmd: idf.py menuconfig --style monochrome
        use: 黑白模式（部分终端兼容性问题时用）
      - note: "sdkconfig 由 menuconfig 生成，不进 Git；sdkconfig.defaults 写默认配置，可进 Git"

  - name: A.8 分区表操作
    commands:
      - cmd: "idf.py partition-table"
        use: 生成并打印当前分区表

  - name: A.9 CI/CD（Docker）
    commands:
      - cmd: "docker run --rm -v $PWD:/project -w /project espressif/idf:v5.3.1 idf.py build"
        use: "Docker 容器编译（无需本地安装 ESP-IDF，固化版本）"
      - note: "官方镜像 espressif/idf 含完整工具链，支持 v4.4 / v5.2 / v5.3 等版本标签"

  - name: A.10 Host-Native 测试
    description: 在 PC 端编译运行业务逻辑（App + Middleware + Mock BSP），不依赖 ESP-IDF
    prerequisite: PC 安装 gcc、Unity 测试框架
    unity_install: "Unity 测试框架: https://github.com/ThrowTheSwitch/Unity（单头文件，放入 tests/unity/ 目录即可）"
    command: "gcc -IApp -IMiddleware -IMock tests/*.c App/*.c Middleware/*.c Mock/*.c unity/unity.c -o test_runner && ./test_runner"
    note: >
      App 和 Middleware 层必须手动 include bsp_interface.h 而非 ESP-IDF 头文件。
      Mock 层提供 bsp_mock.c（GPIO/ADC/定时器等的虚假实现），注入测试数据。
      详见 STEP-3.2 分层架构与 DEC-3.2 Mock 适用场景决策表。

references: [STEP-3.3, STEP-3.4, STEP-3.2, STEP-3.6]
```

---

## 交叉引用映射

```yaml
cross_references:
  # 流程 → 规则
  - from: STEP-1
    to: [RULE-2.2a, RULE-2.5]
    relation: references
  - from: STEP-2.0
    to: [RULE-1.1, RULE-1.2, RULE-1.3]
    relation: references
  - from: STEP-3.0
    to: [RULE-3.1, RULE-3.2, RULE-3.3]
    relation: references
  - from: STEP-3.1
    to: [RULE-2.1, RULE-2.2a, RULE-2.2b, RULE-2.3a, RULE-2.3b]
    relation: references
  - from: STEP-3.2
    to: [RULE-5.1, RULE-5.2]
    relation: references
  - from: STEP-3.3
    to: [RULE-3.1, RULE-3.2, RULE-9.3]
    relation: references
  - from: STEP-3.4
    to: [RULE-4.1, RULE-4.2, RULE-8.2, RULE-8.3, RULE-5.3, RULE-5.4]
    relation: references
  - from: STEP-3.4.8
    to: [RULE-4.1, RULE-4.2, RULE-4.3]
    relation: references
  - from: STEP-3.5
    to: [RULE-10.5]
    relation: references
  - from: STEP-3.6
    to: [RULE-9.3, RULE-10.1, RULE-11.0]
    relation: references
  - from: STEP-3.7
    to: [RULE-3.3, TPL-1.2, STEP-3.6]
    relation: references
  # 检查清单互引
  - from: CHK-6
    to: CHK-7
    relation: references
  # 模板 → 步骤
  - from: TPL-1.2
    to: STEP-1
    relation: belongs_to
  - from: TPL-2.1
    to: STEP-2.0
    relation: belongs_to
  - from: TPL-2.2
    to: STEP-2.0
    relation: belongs_to
  - from: TPL-2.3
    to: STEP-2.0
    relation: belongs_to
  - from: TPL-2.4
    to: STEP-2.0
    relation: belongs_to
  - from: TPL-3.1.1
    to: STEP-3.1
    relation: belongs_to
  - from: TPL-3.1.2
    to: STEP-3.1
    relation: belongs_to
  - from: TPL-3.5
    to: STEP-3.5
    relation: belongs_to
```

---

*本文档是《开发流程_结构化.md》的 ESP32+ESP-IDF 专版。操作步骤与执行顺序查本文档，编码约束与标准要求查《嵌入式C项目规则_结构化.md》。各有侧重，互补不重复。*
