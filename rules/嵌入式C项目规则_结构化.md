# 嵌入式C项目开发规则（结构化版）

> 本文档是《嵌入式C项目规则.md》的结构化转换版本，专为 AI 模型解析设计。
> 每条规则使用统一字段定义，表格保留原始结构并附带元数据，代码示例标注类型。
> 与《开发流程_结构化.md》配套，通过 `references` 字段交叉引用。

---

## 文档元数据

```yaml
doc_id: RULES
version: 3.1
date: 2026-07-30
scope: [STM32, ESP32, nRF, GD32, MM32]
doc_type: 编码约束与标准要求
companion_doc: 开发流程_结构化.md
principles:
  - 先对齐再动手
  - 增量验证不攒 Bug
  - 安全默认不死机
  - 简洁代码不炫技
```

---

## 规则索引

| 规则 ID     | 类别         | 严重级别 | 标题               |
| ----------- | ------------ | -------- | ------------------ |
| RULE-1.1    | 项目启动     | 强制     | SDK 版本对齐       |
| RULE-1.2    | 项目启动     | 强制     | 硬件管脚对齐       |
| RULE-1.3    | 项目启动     | 强制     | 第三方库可用性预检 |
| RULE-2.1    | 编码规范     | 强制     | 类型系统           |
| RULE-2.2a   | 编码规范     | 强制     | 禁止动态内存       |
| RULE-2.2b   | 编码规范     | 强制     | 缓冲区安全传递     |
| RULE-2.2c   | 编码规范     | 建议     | const 数据放置     |
| RULE-2.3a   | 编码规范     | 强制     | ISR 约束           |
| RULE-2.3b   | 编码规范     | 强制     | 共享变量 volatile  |
| RULE-2.4    | 编码规范     | 强制     | 位操作与硬件访问   |
| RULE-2.5    | 编码规范     | 强制     | 状态机与 switch    |
| RULE-2.6    | 编码规范     | 强制     | 可配置化           |
| RULE-3.1    | 构建管理     | 强制     | 增量构建           |
| RULE-3.2    | 构建管理     | 强制     | CMake 依赖管理     |
| RULE-3.3    | 构建管理     | 强制     | 分区表与 OTA       |
| RULE-4.1    | 调试排错     | 强制     | 标准排查顺序       |
| RULE-4.2    | 调试排错     | 建议     | 日志策略           |
| RULE-4.3    | 调试排错     | 建议     | 过度设计识别       |
| RULE-5.1    | 测试验证     | 强制     | 隐式逻辑清单       |
| RULE-5.2    | 测试验证     | 强制     | 状态切换边界测试   |
| RULE-5.3    | 测试验证     | 强制     | 长时间稳定性测试   |
| RULE-5.4    | 测试验证     | 强制     | FreeRTOS 任务审查  |
| RULE-6.1    | 架构设计     | 强制     | 安全默认值原则     |
| RULE-6.2    | 架构设计     | 建议     | 单芯片优先原则     |
| RULE-6.3    | 架构设计     | 建议     | 半成品模块替换     |
| RULE-6.4    | 架构设计     | 建议     | 移植时不重构       |
| RULE-6.5    | 架构设计     | 建议     | 循环依赖处理       |
| RULE-8.2    | 看门狗       | 强制     | 喂狗策略           |
| RULE-8.3    | 看门狗       | 强制     | 复位原因记录       |
| RULE-9.3    | 静态分析     | 建议     | 集成到构建流程     |
| RULE-10.1   | 固件安全     | 强制     | 安全启动           |
| RULE-10.3   | 固件安全     | 建议     | Flash 加密         |
| RULE-10.5   | 固件安全     | 建议     | SBOM 物料清单      |
| RULE-11.0   | CI/CD        | 强制     | 自动化构建流水线   |
| RULE-12.3   | 低功耗       | 建议     | 低功耗设计原则     |
| RULE-13.2   | 通信协议     | 强制     | 帧结构设计         |
| RULE-13.3   | 通信协议     | 强制     | 超时与重传         |
| RULE-13.4   | 通信协议     | 强制     | 心跳保活           |
| RULE-13.5   | 通信协议     | 强制     | 序列号防重放       |

---

## 第一章 项目启动规范

### RULE-1.1 SDK 版本对齐

```yaml
id: RULE-1.1
severity: 强制
category: 项目启动
title: SDK 版本对齐
constraint: >
  开始编码前，必须精确对齐目标 SDK 的版本号（到小版本号），而非仅确认 SDK 名称。
rationale: >
  不同大版本的 SDK 之间 API 差异巨大。以 ESP-IDF 为例，v5.x→v6.x 的
  组件依赖树、初始化函数、驱动 API 全面变更。错误对齐导致首次编译 13 轮迭代。
verification: "编译通过 + 无 deprecated 警告"
operational_steps:
  1: 追问精确版本号（如 v5.2.1 / v6.0.2 / v5.4）
  2: 阅读 Migration Guide，列出所有 deprecated/removed API
  3: 对照示例代码的版本标签
  4: 编译时不忽略 deprecated 警告
references: [STEP-2.0, TPL-2.3]
```

**常见 SDK 大版本差异类型（参考数据表）**：

| 差异类型       | 典型表现                              | 应对措施                                         |
| -------------- | ------------------------------------- | ------------------------------------------------ |
| 组件/驱动拆分  | driver 被拆为 esp_driver_gpio 等      | CMakeLists.txt 中 REQUIRES 字段需逐个列出子组件  |
| 初始化API重命名 | sntp_init()→esp_netif_sntp_init()    | 用 grep 查头文件，不背名字                       |
| ADC 驱动换代   | 旧 adc1_config→新 adc_oneshot_new_unit | 驱动框架换代时旧 API 可能完全移除，需完整重写    |
| 蓝牙协议栈合并 | nimble 从独立组件并入 bt 组件          | 依赖声明以当前 SDK 官方 CMakeLists 示例为准      |
| 分区表格式变更 | 硬编码偏移可能越界                     | 优先用自动布局，写完用 idf.py partition_table 验证 |

---

### RULE-1.2 硬件管脚对齐

```yaml
id: RULE-1.2
severity: 强制
category: 项目启动
title: 硬件管脚对齐
constraint: >
  移植或新建项目前，制作引脚映射表，标注所有特殊功能引脚、仅输入引脚、冲突引脚。
rationale: >
  ESP32 的 GPIO2 是 Strapping 管脚；GPIO34-39 只能输入。
  STM32 的 PA13/PA14 是 SWD 调试口。特殊引脚不能随意分配。
verification: "引脚映射表完成，无冲突"
references: [TPL-2.1, STEP-2.0]
```

**特殊引脚类型约束表**：

| 引脚类型          | 特征                                    | 设计约束                                         |
| ----------------- | --------------------------------------- | ------------------------------------------------ |
| Strapping/Boot    | 上电时电平决定启动模式                  | 绝不在 Strapping 管脚上接上电时有确定电平的外设  |
| 仅输入/无上下拉   | GPIO34-39(ESP32) 不能设为输出，无上下拉 | ADC 输入优选此类引脚，外部电路需自行提供偏置     |
| 调试/编程口       | SWCLK/SWDIO(ARM)、U0TXD/U0RXD(ESP32)    | 如必须复用，需确认能通过二次映射恢复调试功能     |

---

### RULE-1.3 第三方库可用性预检

```yaml
id: RULE-1.3
severity: 强制
category: 项目启动
title: 第三方库可用性预检
constraint: >
  不要假设组件仓库、第三方库、教程中的依赖在目标环境中一定可用。写代码前先验证。
rationale: >
  espressif/dht 组件仓库返回 HTTP 403（已下架）。
  等到集成时才发现不可用，需要整体重写驱动。
verification: "组件拉取返回 HTTP 200；grep 确认函数存在且签名一致"
operational_steps:
  1: "在 IDE 中尝试拉取组件，确认返回 HTTP 200 而非 403/404"
  2: "在 SDK 安装目录下 grep -r '函数名' components/，确认函数存在且签名一致"
  3: "查看 GitHub 第三方库最近一次 commit 时间和 issue 活跃度。超过 2 年未更新，做好自行维护准备"
references: [STEP-2.0]
```

---

## 第二章 编码规范

### RULE-2.1 类型系统

```yaml
id: RULE-2.1
severity: 强制
category: 编码规范
title: 类型系统
constraint: >
  1. 统一使用 <stdint.h> 定义的显式位宽类型，禁止裸 C 类型；
  2. 禁止魔数，所有硬件常量用 #define/enum 命名；
  3. 禁止浮点运算，用定点数或整数位移。
rationale: 显式位宽避免跨平台 int 大小不一致；魔数降低可读性；浮点在 MCU 上性能差且精度不确定。
verification: "代码审查 + 编译器 -Wconversion 警告清零"
examples:
  - type: correct
    code: "uint8_t flag = 0U; #define LED_PIN 13U"
  - type: incorrect
    code: "int flag = 0; 13"
references: []
```

---

### RULE-2.2 内存管理

```yaml
id: RULE-2.2a
severity: 强制
category: 编码规范
title: 禁止动态内存
constraint: 禁止使用 malloc/calloc/realloc/free。所有缓冲区和结构体在编译期静态分配。
rationale: 堆碎片化导致长时间运行后分配失败；嵌入式系统无虚拟内存。
verification: "链接后 .bss/.data 段大小可控；grep -r 'malloc\\|calloc\\|realloc' 返回空"
references: []
```

```yaml
id: RULE-2.2b
severity: 强制
category: 编码规范
title: 缓冲区安全传递
constraint: >
  1. 数组/指针传入函数时，必须同时传入缓冲区大小参数；
  2. 禁止 sprintf/strcpy，必须使用 snprintf/strncpy。
rationale: 防止缓冲区溢出。
verification: "代码审查"
examples:
  - type: correct
    code: "void parse(const uint8_t *buf, uint16_t len);"
  - type: incorrect
    code: "void parse(const uint8_t *buf);"
references: []
```

```yaml
id: RULE-2.2c
severity: 建议
category: 编码规范
title: const 数据放置
constraint: const 修饰的查表、配置结构、只读数据让编译器放入 .rodata 段（Flash），而非 .data 段（RAM）。
rationale: 节省 RAM 空间。
verification: "arm-none-eabi-size 检查 .data 段不包含只读数据"
references: []
```

---

### RULE-2.3 中断与并发

```yaml
id: RULE-2.3a
severity: 强制
category: 编码规范
title: ISR 约束
constraint: >
  ISR 必须极短（<50us），禁止在 ISR 中调用阻塞函数或执行 I/O 操作。
  只做：置标志位 / 发信号量 / 发任务通知。
rationale: ISR 占用 CPU 时间过长会阻塞其他中断和任务调度。
verification: "逻辑分析仪测量 ISR 执行时间 < 50us"
references: []
```

```yaml
id: RULE-2.3b
severity: 强制
category: 编码规范
title: 共享变量 volatile
constraint: >
  1. ISR 和普通任务共享的全局变量必须加 volatile 修饰；
  2. 微秒级 GPIO 时序操作必须用临界区保护（__disable_irq() / __enable_irq()）。
rationale: 编译器优化可能导致 ISR 和主循环读取到缓存的寄存器值。
verification: "代码审查 + -O2 优化级别下功能正确"
references: []
```

---

### RULE-2.4 位操作与硬件访问

```yaml
id: RULE-2.4
severity: 强制
category: 编码规范
title: 位操作与硬件访问
constraint: >
  1. 位移操作必须使用显式位宽字面量（1UL<<31 而非 1<<31）；
  2. MMIO 指针必须声明为 volatile uint32_t *const。
rationale: 1<<31 在 32 位系统上是未定义行为（符号位溢出）。
verification: "代码审查"
examples:
  - type: correct
    code: "#define BIT_FLAG (1UL << 31)"
  - type: incorrect
    code: "#define BIT_FLAG (1 << 31)"
references: []
```

---

### RULE-2.5 状态机与 switch

```yaml
id: RULE-2.5
severity: 强制
category: 编码规范
title: 状态机与 switch
constraint: >
  1. 所有 switch-case 必须包含 default 分支；
  2. 状态切换时先保存旧状态再赋值新状态。
rationale: default 分支捕获非法状态，防止状态机卡死。
verification: "代码审查"
references: [STEP-1.2]
```

---

### RULE-2.6 可配置化

```yaml
id: RULE-2.6
severity: 强制
category: 编码规范
title: 可配置化
constraint: >
  1. WiFi 密码、服务器 URL、API 密钥等可变参数必须通过 Kconfig/sdkconfig 管理，禁止硬编码；
  2. 任何可能失败的操作必须有安全默认值。
rationale: 硬编码密钥导致固件泄露；安全默认值确保设备不因配置错误而变砖。
verification: "grep -r '密码\\|password\\|secret' src/ 返回空"
references: [RULE-6.1]
```

---

## 第三章 构建与工程管理

### RULE-3.1 增量构建

```yaml
id: RULE-3.1
severity: 强制
category: 构建管理
title: 增量构建
constraint: >
  写完一个模块→编译→烧录→验证→git commit→再写下一个模块。
  禁止一次性写完所有模块再编译。
rationale: 一口气写 2500 行再编译 = 200 条错误一起涌出，信息过载。每次只加一个模块，错误最多 5 条。
verification: "git log 显示每个模块单独 commit"
references: [STEP-3.0]
```

---

### RULE-3.2 CMake 依赖管理

```yaml
id: RULE-3.2
severity: 强制
category: 构建管理
title: CMake 依赖管理
constraint: >
  CMakeLists.txt 中 REQUIRES 字段只列出实际用到的组件。
  组件命名避免与系统头文件/官方组件重名。
rationale: 多余的 REQUIRES 增加编译时间和固件大小；重名导致依赖解析混乱。
verification: "idf.py reconfigure 无警告"
references: []
```

---

### RULE-3.3 分区表与 OTA

```yaml
id: RULE-3.3
severity: 强制
category: 构建管理
title: 分区表与 OTA
constraint: >
  1. 分区表优先使用自动布局，确保所有分区总大小不超过 Flash 容量；
  2. OTA 首要原则是「不能变砖」，所有失败环节均有降级策略。
rationale: 分区越界导致启动失败；OTA 变砖需要返厂。
verification: "idf.py partition_table 验证 + OTA 回滚测试"
references: [RULE-6.1]
```

---

## 第四章 调试与排错方法论

### RULE-4.1 标准排查顺序

```yaml
id: RULE-4.1
severity: 强制
category: 调试排错
title: 标准排查顺序
constraint: >
  排查顺序必须是：日志输出→初始化流程→数据流路径→信号/时序→最后才是硬件。
  禁止一上来就怀疑硬件。
rationale: 80% 的问题是软件问题，硬件故障概率最低。
verification: "排查记录显示按顺序执行"
references: [STEP-3.4.8]
```

---

### RULE-4.2 日志策略

```yaml
id: RULE-4.2
severity: 建议
category: 调试排错
title: 日志策略
constraint: >
  每个组件在 init 和关键路径上打印日志。
  分级：ERROR(致命)、WARN(可恢复)、INFO(关键状态)、DEBUG(仅开发阶段)。
rationale: 无日志的嵌入式系统等于盲飞。
verification: "串口日志覆盖所有 init 函数和关键状态切换"
references: [STEP-3.4.3]
```

---

### RULE-4.3 过度设计识别

```yaml
id: RULE-4.3
severity: 建议
category: 调试排错
title: 过度设计识别
constraint: >
  不为单次使用建抽象层，不加未被要求的灵活性。
  自检：「高级工程师会觉得这个过度复杂吗？」
rationale: 过度设计增加维护成本和 Bug 面积。
verification: "代码审查"
references: []
```

---

## 第五章 测试与验证

### RULE-5.1 移植项目隐式逻辑清单

```yaml
id: RULE-5.1
severity: 强制
category: 测试验证
title: 移植项目隐式逻辑清单
constraint: >
  移植时做一张「隐式逻辑清单」：原代码中所有 ISR/HAL回调/DMA中断中的逻辑，
  在新平台必须显式重建。
rationale: 移植时最容易遗漏的是隐含在中断和回调中的时序逻辑。
verification: "隐式逻辑清单完成 + 逐项核对"
references: []
```

---

### RULE-5.2 状态切换边界测试

```yaml
id: RULE-5.2
severity: 强制
category: 测试验证
title: 状态切换边界测试
constraint: 必须测试以下场景：同模式参数调节、连续快速切换、异常状态恢复、传感器读取失败。
rationale: 状态机 Bug 通常只在边界条件下触发。
verification: "边界测试用例全 pass"
test_matrix:
  - scenario: 同模式参数调节
    operation: "COLOR 模式下调亮度"
    expected: 亮度变但呼吸动画不重置
    verify: g_mode=old_mode 时不走初始化
  - scenario: 连续快速切换
    operation: 连续按 MODE 键 10 次
    expected: 每种模式都能正确进入
    verify: 状态机不丢失事件
  - scenario: 异常状态恢复
    operation: 给状态变量赋非法值 (mode=99)
    expected: default 分支捕获并回到安全状态
    verify: switch 必须有 default
  - scenario: 传感器读取失败
    operation: 拔掉 DHT11 或用手遮挡
    expected: 失败后复用上次缓存值
    verify: 失败不崩溃，显示不跳变
references: [RULE-2.5]
```

---

### RULE-5.3 长时间稳定性测试

```yaml
id: RULE-5.3
severity: 强制
category: 测试验证
title: 长时间稳定性测试
constraint: >
  所有嵌入式项目在交付前必须通过至少 24 小时连续运行压力测试，
  期间监控任务栈水位和可用堆内存。
rationale: 内存泄漏、栈溢出、看门狗复位通常在长时间运行后才暴露。
verification: "24h 运行无 HardFault + 栈水位 > 20% 余量"
references: [RULE-5.4, STEP-3.4.5]
```

---

### RULE-5.4 FreeRTOS 任务审查清单

```yaml
id: RULE-5.4
severity: 强制
category: 测试验证
title: FreeRTOS 任务审查清单
constraint: 每次新增或修改任务后，必须回答以下 5 个问题。
checklist:
  1: 各任务优先级是否合理？高优先级任务是否会饿死低优先级任务？
  2: 同优先级任务是否会产生竞态条件？
  3: 是否存在优先级反转场景？
  4: 是否有任务缺少阻塞点（while(1) 中无 vTaskDelay/队列等待）？
  5: 各任务栈大小是否合理？通过 uxTaskGetStackHighWaterMark() 实测后再调整。
verification: "5 个问题全部回答 + 有实测数据"
references: [STEP-3.4.5]
```

---

## 第六章 架构设计原则

### RULE-6.1 安全默认值原则

```yaml
id: RULE-6.1
severity: 强制
category: 架构设计
title: 安全默认值原则
constraint: >
  任何可能失败的操作，默认行为必须是「安全降级」，而非「阻塞重试」或「崩溃」。
rationale: 嵌入式设备的第一要务是「可用」，第二才是「最新/最优」。
examples:
  - case: 网络请求超时
    action: 用缓存值
  - case: OTA 检查失败
    action: 跳过升级
  - case: 传感器读取失败
    action: 用上次有效值
references: [RULE-2.6, STEP-1.2]
```

---

### RULE-6.2 单芯片优先原则

```yaml
id: RULE-6.2
severity: 建议
category: 架构设计
title: 单芯片优先原则
constraint: 如果目标芯片已集成所需外设（WiFi/BLE/USB/CAN），优先使用单芯片方案。
rationale: 多芯片方案增加 BOM 成本、PCB 面积、通信延迟和故障点。
verification: "架构评审"
references: []
```

---

### RULE-6.3 半成品模块果断替换

```yaml
id: RULE-6.3
severity: 建议
category: 架构设计
title: 半成品模块果断替换
constraint: >
  如果原方案的某个模块代码不完整、或依赖不稳定的外部服务，
  移植/重写时应果断替换而非补全。
rationale: 补全别人的半成品比从头写更耗时。
verification: "架构评审"
references: []
```

---

### RULE-6.4 移植时不顺手重构

```yaml
id: RULE-6.4
severity: 建议
category: 架构设计
title: 移植时不顺手重构
constraint: 移植和重构是两个独立的变更。混在一起 = 同时引入两类变量，定位困难。
rationale: 一次只做一件事。
verification: "git diff 显示移植 commit 无重构改动"
references: []
```

---

### RULE-6.5 循环依赖处理

```yaml
id: RULE-6.5
severity: 建议
category: 架构设计
title: 循环依赖处理
constraint: 用 extern 声明替代 #include 避免循环依赖。依赖关系单向流动。
rationale: 循环依赖导致编译错误和模块耦合。
verification: "模块依赖图无环"
references: []
```

---

## 第七章 检查清单

> 完整检查清单（含流程步骤）见《开发流程_结构化.md》CHK-6。以下仅列出编码层面的规则自检项。

```yaml
id: CHK-7
type: 规则自检清单
items:
  - group: 编码规则自检
    checks:
      - "所有类型使用 <stdint.h>"
      - "无魔数"
      - "指针解引用前 NULL 检查"
      - "数组传入时附带大小参数"
      - "switch 包含 default"
      - "位移用显式位宽"
      - "ISR 极短（<50us）"
      - "微秒时序用临界区"
      - "const 数据已标注"
      - "可变参数通过 Kconfig 管理"
      - "失败操作有安全默认值"
      - "禁止 malloc/sprintf/strcpy"
      - "编译警告全部清零"
      - "CMakeLists 依赖与 #include 一致"
      - "分区表总大小不超 Flash 容量"
  - group: 测试与交付前
    checks:
      - "状态切换边界测试"
      - "异常恢复测试"
      - "任务优先级审查"
      - "长时间稳定性测试（>24h）"
      - "OTA 降级策略验证"
      - "DEBUG 日志已调整为 INFO"
      - "固件版本号已递增"
      - "所有 TODO/FIXME 已处理"
      - "看门狗已启用（RELEASE 构建）"
      - "第三方依赖已记录 SBOM"
references: [CHK-6]
```

---

## 第八章 看门狗与故障恢复

### 看门狗类型选型参考表

```yaml
id: REF-8.1
type: 参考数据表
description: 看门狗类型与选型
data:
  - type: 独立看门狗(IWDT)
    feature: 内置于 RC 振荡器，独立于 CPU 时钟，即使主时钟停振仍然计时
    scope: 所有嵌入式项目默认使用，是最基本保障
  - type: 窗口看门狗(WWDG)
    feature: 只在特定时间窗口内允许喂狗，提前或过晚均复位
    scope: 安全关键系统，防止代码跳转到错误位置后仍触发喂狗
  - type: 软件看门狗
    feature: 用 FreeRTOS 定时器实现，监控各任务是否响应
    scope: 被动采用，硬件 WDT 已用于其他目的时的备选
```

### RULE-8.2 喂狗策略

```yaml
id: RULE-8.2
severity: 强制
category: 看门狗
title: 喂狗策略
constraint: >
  喂狗只在主循环/主任务的末尾执行一次，绝不在 ISR、定时器回调或多个位置分散喂狗。
rationale: >
  分散喂狗是看门狗设计的头号常见错误。
  如果在 ISR 和主循环各喂一次，主循环卡死时 ISR 仍然在喂狗——看门狗形同虚设。
verification: "代码审查：grep 'watchdog_feed\\|IWDG_ReloadCounter' 只有一个调用点"
code_pattern:
  correct: |
    // 每个任务设置自己的健康标志位
    volatile uint8_t task_healthy[TASK_COUNT] = {0};
    // 主循环中
    while(1) {
        if (all_tasks_healthy()) {
            watchdog_feed();  // 所有任务都健康才喂狗
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
references: []
```

---

### RULE-8.3 复位原因记录

```yaml
id: RULE-8.3
severity: 强制
category: 看门狗
title: 复位原因记录
constraint: >
  每次启动时检查复位原因寄存器，记录是上电、看门狗复位、还是软件复位。
  ESP32 读 RTC_CNTL_RESET_CAUSE，STM32 读 RCC_CSR。
rationale: >
  没有复位原因记录，看门狗频繁复位时永远不知道是哪个任务卡死。
verification: "启动日志包含 'Reset cause: xxx'"
references: [STEP-3.4.6]
```

---

### 故障分级处理参考表

```yaml
id: REF-8.4
type: 参考数据表
description: 故障分级处理策略
data:
  - level: L1-可恢复
    strategy: 重试、用缓存值、降级运行
    example: 传感器读取超时→复用上次值，WARN 日志
  - level: L2-部分降级
    strategy: 停用故障模块，其他模块正常运行
    example: WiFi 连接失败→关闭网络功能，本地模式继续工作
  - level: L3-不可恢复
    strategy: 记录复位原因→看门狗复位→快速恢复
    example: HardFault/StackOverflow/WDT 超时→重启后进入安全模式
references: [RULE-6.1]
```

---

## 第九章 静态分析与自动化检查

### 静态分析工具链参考表

```yaml
id: REF-9.1
type: 参考数据表
description: 静态分析工具推荐
data:
  - tool: Cppcheck
    use: 静态分析——内存泄漏、越界、未初始化变量
    feature: 开源、轻量、快速、支持 MISRA 规则子集
    cost: 免费
  - tool: Clang-Tidy
    use: 静态分析 + 代码风格检查
    feature: 集成在 clang 编译器中，规则丰富
    cost: 免费
  - tool: PC-lint Plus
    use: 严格的 MISRA C 检查
    feature: 商业级，规则最全，支持自定义
    cost: 付费
  - tool: SonarQube
    use: 代码质量持续监控
    feature: Web UI + 趋势图表，适合团队
    cost: 社区版免费
  - tool: Coverity
    use: 深度静态分析，漏洞扫描
    feature: 企业级，误报率极低，适合安全关键
    cost: 付费
```

### MISRA C 关键规则参考表

```yaml
id: REF-9.2
type: 参考数据表
description: 必查的 MISRA C 关键规则
data:
  - rule_id: "1.2"
    rule: 不能依赖未定义行为
    typical_violation: "i = i++ + ++i"
    correction: 分解为多个独立语句
  - rule_id: "8.4"
    rule: 禁止递归调用
    typical_violation: "void f(){ f(); }"
    correction: 改为循环或状态机
  - rule_id: "12.1"
    rule: 表达式不能含副作用
    typical_violation: "if(a++ && b--)"
    correction: 副作用提到表达式外部
  - rule_id: "13.2"
    rule: 表达式值必须被使用
    typical_violation: "(void)func_ret; 丢弃返回值"
    correction: 显式 (void) 强制转换
  - rule_id: "15.1"
    rule: 禁止 goto 语句
    typical_violation: "goto error;"
    correction: 改为 if-else 或状态机
  - rule_id: "17.2"
    rule: 循环计数器不能被修改
    typical_violation: "for(i=0;i<10;i++){i=5;}"
    correction: 用 while 替代或用临时变量
  - rule_id: "20.3"
    rule: 禁止 #undef
    typical_violation: "#undef MAX_VALUE"
    correction: 不要重定义宏，用不同名字
  - rule_id: "21.6"
    rule: 禁止使用标准库输入输出
    typical_violation: "printf/scanf 在嵌入式中禁用"
    correction: 用串口日志替代
```

### RULE-9.3 集成到构建流程

```yaml
id: RULE-9.3
severity: 建议
category: 静态分析
title: 集成到构建流程
constraint: >
  编译警告全部当作错误处理（-Werror）。
  预 commit 检查脚本：make + cppcheck，有警告或错误则拒绝 commit。
rationale: 今天的警告就是明天的 Bug。
verification: "pre-commit hook 执行 cppcheck 返回 0"
code_pattern:
  pre_commit: |
    # .git/hooks/pre-commit
    make -j$(nproc)  # 确保编译通过
    cppcheck --enable=all --inconclusive --suppress=missingIncludeSystem src/ 2>&1
references: [STEP-3.6]
```

---

## 第十章 固件安全基础

### RULE-10.1 安全启动

```yaml
id: RULE-10.1
severity: 强制
category: 固件安全
title: 安全启动 (Secure Boot)
constraint: 启用芯片的 Secure Boot 功能，确保只有签名合法的固件才能被执行。
rationale: 防止恶意固件刷入设备。
verification: "menuconfig 中 Secure Boot 已启用 + 未签名固件无法启动"
references: [RULE-10.2]
```

### 固件签名算法参考

```yaml
id: REF-10.2
type: 参考数据表
description: 推荐签名算法
data:
  - algorithm: ECDSA (NIST P-256)
    signature_size: 64 字节
    speed: 快
    note: 安全性等价 256 位 RSA
  - algorithm: Ed25519
    signature_size: 64 字节
    speed: 比 ECDSA 更快
    note: 安全性相当
  - algorithm: RSA-2048
    signature_size: 256 字节
    speed: 较慢
    note: 仅兼容性场景
ota_signing_flow:
  1: 服务器用私钥签名固件包
  2: 设备用公钥验签
  3: 验签通过才写入 Flash
references: [RULE-10.1]
```

### RULE-10.3 Flash 加密

```yaml
id: RULE-10.3
severity: 建议
category: 固件安全
title: Flash 加密
constraint: >
  如果芯片支持，启用 Flash Encryption 防止固件被读出或逆向。
  注意：启用后无法关闭，且无法用 JTAG 调试。仅在量产固件中启用。
verification: "menuconfig 中 Flash Encryption 已启用"
references: []
```

### 通信安全等级参考表

```yaml
id: REF-10.4
type: 参考数据表
description: 通信安全推荐等级
data:
  - protocol: BLE
    recommended_level: Security Mode 1 Level 4 (LESC 配对)
    note: 仅在配对时使用，之后连接通过加密链路通信
  - protocol: WiFi
    recommended_level: WPA2-Enterprise / WPA3
    note: WPA2-Personal 仅在家居场景可接受
  - protocol: TCP/云端
    recommended_level: TLS 1.2+ (检查服务器证书)
    note: 不使用 HTTP 明文传输，数据上行必须 SSL 加密
  - protocol: OTA 下载
    recommended_level: HTTPS + 固件签名验证
    note: 即使 HTTPS 加密了传输，仍需额外固件签名，防止服务器被攻破
```

### RULE-10.5 SBOM 物料清单

```yaml
id: RULE-10.5
severity: 建议
category: 固件安全
title: SBOM (软件物料清单)
constraint: >
  维护一份所有依赖库及其版本的清单，以便在漏洞披露时快速定位受影响的设备。
  建议用 JSON 格式记录，每次发布时附带 SBOM 文件。
rationale: 欧盟 Cyber Resilience Act 要求所有联网设备具备安全开发实践。
example_format: |
  { "component": "mbedtls", "version": "3.4.1", "license": "Apache-2.0" }
references: [STEP-3.5]
```

---

## 第十一章 CI/CD 自动化构建

### RULE-11.0 自动化构建流水线

```yaml
id: RULE-11.0
severity: 强制
category: CI/CD
title: 自动化构建流水线
constraint: >
  项目必须配置自动化构建流水线，每次 push 触发编译+静态分析，不达标→拒绝 merge。
rationale: 手动编译迟早出错；CI 保证每次 commit 的编译可重复性。
verification: "CI 管线存在 + 最近一次 push 触发了构建"
references: [STEP-3.6, REF-9.1]
```

### CI/CD vs 传统手动开发对比表

```yaml
id: REF-11.1
type: 参考数据表
description: CI/CD vs 传统手动开发
data:
  - dimension: 编译可重复性
    traditional: 依赖开发者本地环境
    cicd: Docker 容器保证一致性
  - dimension: 错误发现时间
    traditional: 可能等到集成阶段
    cicd: 每次 commit 立即发现
  - dimension: 回溯能力
    traditional: "「上次还能编译的」"
    cicd: CI 记录了每次构建的产物和日志
  - dimension: 团队协作
    traditional: "「我这里跑的正常啊」"
    cicd: 红色 PR = 客观证据需要修复
```

---

## 第十二章 低功耗设计

### 睡眠模式分级参考表

```yaml
id: REF-12.1
type: 参考数据表
description: 睡眠模式分级
data:
  - mode: Active
    current: "~50mA+"
    wake_time: 即时
    wake_source: "-"
    retained: 全部正常
  - mode: IDLE
    current: "~10mA"
    wake_time: "<1us"
    wake_source: 任意中断
    retained: CPU 停振，外设继续
  - mode: LIGHT_SLEEP
    current: "~0.8mA"
    wake_time: "<1ms"
    wake_source: GPIO/Timer/UART
    retained: RAM 保持，CPU 停振，WDT 可选
  - mode: DEEP_SLEEP
    current: "~5uA"
    wake_time: "<10ms"
    wake_source: RTC Timer/GPIO
    retained: RTC 内存保持，其余全部断电
  - mode: HIBERNATE
    current: "~1uA"
    wake_time: 重新启动
    wake_source: RTC Timer/GPIO
    retained: 无保持，相当于重新上电
```

### 功耗预算公式参考

```yaml
id: REF-12.2
type: 参考公式
description: 电池续航估算
formulas:
  - name: 续航时间
    expression: "续航时间 = 电池容量(mAh) / 平均电流(mA)"
  - name: 平均电流
    expression: "平均电流 = (Active电流 × Active占比 + Sleep电流 × Sleep占比)"
example: >
  2000mAh 电池，Active 50mA 占每分 1s、Sleep 0.8mA 占每分 59s，
  平均电流 = 50×(1/60) + 0.8×(59/60) ≈ 1.62mA，续航 ≈ 1234 小时。
```

### RULE-12.3 低功耗设计原则

```yaml
id: RULE-12.3
severity: 建议
category: 低功耗
title: 低功耗设计原则
constraint: >
  1. 吹灯外设：进入睡眠前关闭所有不需要的外设时钟；
  2. 降频而非睡眠：负载轻时降低 CPU 主频比频繁进出睡眠更省电；
  3. 用 RTC 定时器替代 vTaskDelay：DEEP_SLEEP 期间只有 RTC 定时器能唤醒；
  4. 避免忙等：用中断/事件驱动替代轮询；
  5. WiFi/BLE 功耗优化：减少广播间隔、调整射频功率、不用时关闭 Radio。
rationale: 仅电池供电的设备必读。交流供电的设备可简单了解。
verification: "功耗测量符合预算"
references: []
```

---

## 第十三章 通信协议健壮性

### CRC 算法选型参考表

```yaml
id: REF-13.1
type: 参考数据表
description: 帧完整性校验算法对比
data:
  - algorithm: CRC-8
    polynomial: "x^8+x^2+x+1"
    strength: 低
    code_lines: "~10"
    scope: 短帧(<8字节)，I2C/SMBus
  - algorithm: CRC-16
    polynomial: "x^16+x^15+x^2+1"
    strength: 中
    code_lines: "~15"
    scope: 通用串口、Modbus 标准
  - algorithm: CRC-32
    polynomial: "x^32+x^26+...+x^2+x+1"
    strength: 高
    code_lines: "~25"
    scope: 固件包、文件校验、存储完整性
  - algorithm: 简单和校验
    polynomial: "sum & 0xFF"
    strength: 极低
    code_lines: "~3"
    scope: 仅用于非关键数据，不推荐严肃场景
```

### RULE-13.2 帧结构设计

```yaml
id: RULE-13.2
severity: 强制
category: 通信协议
title: 帧结构设计
constraint: 使用以下最小通用帧格式
frame_schema:
  - field: HEAD
    offset: 0
    size: 1B
    value: 0xAA
    description: 固定帧头，用于帧同步。如果帧头字节可能出现在数据段，需要转义（0xAA→0xBB 0x55）
  - field: ADDR
    offset: 1
    size: 1B
    description: 设备地址，支持多设备总线（RS485/CAN）
  - field: LENGTH
    offset: 2
    size: 1B
    description: DATA 段长度，接收方据此判断帧结束位置
  - field: CMD
    offset: 3
    size: 1B
    description: 命令码，区分读/写/响应
  - field: DATA
    offset: 4
    size: 0-255B
    description: 数据载荷
  - field: CRC
    offset: variable
    size: 2B
    description: CRC-16 校验
references: [REF-13.1]
```

---

### RULE-13.3 超时与重传

```yaml
id: RULE-13.3
severity: 强制
category: 通信协议
title: 超时与重传
constraint: >
  通信超时后采用「指数退避 + 上限重试」策略，而非固定间隔无限重试。
  重试 3 次后报通信失败，进入降级状态。
rationale: 指数退避避免多设备同时重试导致信道拥堵。
verification: "通信测试：断开对端，确认 3 次重试后进入降级"
references: [RULE-6.1]
```

---

### RULE-13.4 心跳保活

```yaml
id: RULE-13.4
severity: 强制
category: 通信协议
title: 心跳保活
constraint: >
  长连接（BLE/TCP/CAN）场景必须实现心跳机制。
  发送方每隔 T 发送 PING，接收方超过 2T 未收到则断开连接。
rationale: 心跳间隔取决于连接重要性：BLE 建议 5-10s，TCP 建议 30-60s。
verification: "断开对端后 2T 时间内检测到断连"
references: []
```

---

### RULE-13.5 序列号防重放

```yaml
id: RULE-13.5
severity: 强制
category: 通信协议
title: 序列号防重放
constraint: >
  每个上行数据帧带有自增序列号（seq），接收方丢弃序列号 ≤ 上次有效序列号的帧。
  SEQ 用 1 字节（0-255 循环）足够。
rationale: 防止重复帧（ACK 丢失导致发送方重传）被处理两次。
verification: "重放测试：发送旧 seq 帧，确认被丢弃"
references: []
```

---

## 附录 A 命名约定速查表

```yaml
id: REF-A
type: 参考数据表
description: 命名约定速查
data:
  - category: 全局变量
    rule: g_ 前缀
    example: g_mode, g_sensor_queue
    code: "uint8_t g_mode;"
  - category: 静态变量
    rule: s_ 前缀
    example: s_rmt_mutex
    code: "static SemaphoreHandle_t s_mutex;"
  - category: 常量/宏
    rule: 全大写_下划线
    example: DHT11_GPIO, LAMP_MODE_COUNT
    code: "#define DHT11_GPIO 26U"
  - category: 枚举值
    rule: 全大写_前缀_值
    example: LAMP_MODE_NORMAL
    code: "enum { LAMP_MODE_NORMAL = 0U };"
  - category: 函数名
    rule: 小写_下划线
    example: led_set_auto_color()
    code: "void sensor_task_init(void);"
  - category: 组件名
    rule: 项目_前缀
    example: lamp_core, lamp_led
    code: "idf_component_register(SRCS ...)"
  - category: 配置宏
    rule: CONFIG_项目_键
    example: CONFIG_SMARTLAMP_WIFI_SSID
    code: Kconfig.projbuild 中定义
```

---

## 交叉引用映射

```yaml
cross_references:
  - from: RULE-1.1
    to: STEP-2.0
    relation: references
  - from: RULE-1.2
    to: TPL-2.1
    relation: references
  - from: RULE-1.3
    to: STEP-2.0
    relation: references
  - from: RULE-2.5
    to: STEP-1.2
    relation: references
  - from: RULE-2.6
    to: RULE-6.1
    relation: references
  - from: RULE-3.1
    to: STEP-3.0
    relation: references
  - from: RULE-4.1
    to: STEP-3.4.8
    relation: references
  - from: RULE-4.2
    to: STEP-3.4.3
    relation: references
  - from: RULE-5.3
    to: STEP-3.4.5
    relation: references
  - from: RULE-5.4
    to: STEP-3.4.5
    relation: references
  - from: RULE-6.1
    to: STEP-1.2
    relation: references
  - from: RULE-8.3
    to: STEP-3.4.6
    relation: references
  - from: RULE-9.3
    to: STEP-3.6
    relation: references
  - from: RULE-10.5
    to: STEP-3.5
    relation: references
  - from: RULE-11.0
    to: STEP-3.6
    relation: references
  - from: CHK-7
    to: CHK-6
    relation: references
```

---

*本文档是《嵌入式C项目规则.md》的结构化转换版本。编码约束查本文档，操作步骤查《开发流程_结构化.md》。*
