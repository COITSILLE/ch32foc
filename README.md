# ch32foc

CH32M030（RISC-V）无刷电机 FOC 控制工程，由三部分组成：

| 部分 | 平台 | 职责 |
| --- | --- | --- |
| 电机固件 | CH32M030（青稞 RISC-V） | 三相 FOC 电流/速度环、磁编码器采样、故障保护、二进制命令协议 |
| 桥接固件 | STM32F103 + FreeRTOS | PC（ASCII 文本命令）与电机总线（`0xA5` 二进制帧）之间的网关，支持多电机寻址 |
| 上位机 UI | Vite + TypeScript（Web Serial） | 波形监视、参数整定、调试控制台 |

```
PC (Web UI / Web Serial)
   │  ASCII 文本行: "CMD ARG\r\n"        "OK ..." / "ERR CMD|ARG|LINE|TIMEOUT|FRAME|FAULT"
   ▼
STM32F103 (Host)  ── FreeRTOS: PcLink ↔ Motor2204 ×N
   │  二进制帧: 0xA5 | addr<<5|cmd | len | data[len] | crc
   ▼
CH32M030 (本仓库根目录固件)  ← MT6701 SSI 编码器 / 三相逆变桥
```

## 目录结构

```
foc/
├─ User/                     主固件（应用层，业务代码都在这里）
│  ├─ main.cpp               程序入口、初始化与主循环
│  ├─ peripheral.c/.h        外设初始化封装
│  ├─ ch32m030_it.c/.h       中断服务函数
│  ├─ ch32m030_conf.h        标准外设库裁剪配置
│  ├─ system_ch32m030.c/.h   时钟/系统初始化
│  └─ app/                   控制算法与协议
│     ├─ foc_driver.cpp/.h         FOC 核心环路（Clarke/Park、SVPWM、电流环、速度环）
│     ├─ three_phase_inverter.h    逆变桥驱动抽象
│     ├─ foc_math.h                控制相关的数学辅助
│     ├─ motor_params.h            电机/控制参数定义
│     ├─ protocol.cpp/.h           上位机二进制帧协议（0xA5 帧、命令与应答类型）
│     └─ flash_save_data.h         参数掉电保存
├─ lib/                      可复用、与芯片弱耦合的组件
│  ├─ device/                mt6701_ssi.cpp/.h（磁编码器 SSI 读取）
│  ├─ drivers/               uart_halfduplex、simu_ssi（仿真编码器）、sys_interface.h、
│  │                         rtos_handler.h、delay.h
│  └─ utils/                 smath（数学）、iqmath（定点 Q 格式运算）、
│                            sstrtotype（字符串工具）、timestamp/systick_time（时基）
├─ Peripheral/               CH32M030 标准外设库（官方 SDK，inc/ + src/）
├─ Core/                     core_riscv.c/.h（RISC-V 内核支持）
├─ Startup/                  startup_ch32m030.S（启动汇编）
├─ Debug/                    debug.c/.h（调试输出）
├─ Ld/Link.ld                链接脚本
├─ obj/                      构建产物（makefile、*.elf/.hex/.lst/.map，已忽略）
├─ records/                  上位机采集的波形数据（*.json）与分析脚本
│                            （analyze_sine.py / analyze_step.py，已忽略）
├─ Host/                     上位机桥接固件 + Web UI（见下节）
├─ foc.wvproj / foc.launch   MounRiver Studio / Eclipse 工程与调试配置
├─ compile_commands.json     clangd 索引用的编译数据库
└─ .mrs/                     MounRiver 工作区状态（快照文件）
```

### Host/（STM32F103 桥接固件 + 上位机）

```
Host/
├─ CMakeLists.txt            用户可编辑的顶层构建脚本（目标名 host）
├─ CMakePresets.json         Debug / Release 预设（Ninja + arm-none-eabi 工具链）
├─ host.ioc                  STM32CubeMX 工程（改外设请从这里重新生成）
├─ STM32F103XX_FLASH.ld      链接脚本
├─ startup_stm32f103xb.s     启动汇编
├─ cmake/                    工具链文件（gcc-arm-none-eabi / starm-clang）与 CubeMX 汇总脚本
├─ cmake_manager_gen/        CubeMX 生成的源码清单（ManagedLists.cmake）
├─ Core/                     CubeMX 生成的 HAL 初始化：Inc/ + Src/
│                            （main.c、gpio.c、usart.c、freertos.c、it.c、syscalls…）
├─ Drivers/                  CMSIS + STM32F1xx HAL（第三方，勿改）
├─ Middlewares/              FreeRTOS + CMSIS-RTOS2（第三方，勿改）
├─ User/                     桥接应用层
│  ├─ app/app_main.cpp/.h       主任务：命令解析、状态机、应答
│  ├─ app/pc_link.cpp/.h        与 PC 的串口链路（按行收发，CR/LF 分帧）
│  ├─ device/motor2204.cpp/.h   电机侧 0xA5 帧协议客户端（超时、重发、CRC）
│  └─ driver/sys_interface.h    平台接口（与 lib/drivers/sys_interface.h 同名的移植点）
├─ build/                    CMake 输出（已忽略）
└─ html/                     Web 上位机（见下节）
```

### Host/html/（Web 上位机）

```
Host/html/
├─ package.json              scripts：dev / build / preview / test / e2e / lint / typecheck
├─ index.html                Vite 入口
├─ start.cmd                 Windows 一键启动（自动 npm install + 起 dev server，端口 5180）
├─ vite.config.ts            构建配置
├─ vitest.config.ts          单元测试配置
├─ playwright.config.ts      E2E 测试配置
├─ tsconfig.json / eslint.config.js
├─ public/icons/             favicon 等静态资源
├─ src/
│  ├─ main.ts                应用装配入口
│  ├─ core/                  纯领域逻辑，不依赖浏览器
│  │  ├─ domain/             设备、故障、指标、会话、单位等模型
│  │  ├─ protocol/           命令表、帧格式、解析、限值
│  │  ├─ hub.ts              设备聚合与转发
│  │  ├─ scheduler.ts        轮询/采样调度
│  │  └─ app-error.ts
│  ├─ ports/                 端口接口（clock / logger / storage / transport）
│  ├─ adapters/              端口实现（web-serial、replay 回放、local 存储、console 日志、system 时钟）
│  ├─ app/                   状态与动作（store / state / actions / selectors / errors）
│  ├─ shared/                assert、fmt、result、subscribe 等通用工具
│  ├─ ui/
│  │  ├─ shell/              布局、路由、状态栏
│  │  ├─ components/         card、gauge、plot、tabs、toast、number-field、switch、log-list…
│  │  ├─ views/monitor/      监视页（图表、控制面板、设备列表、KPI、摘要）
│  │  ├─ views/debug/        调试页（参数、寄存器、标定、控制台日志）
│  │  └─ theme/              tokens.css / base.css
│  └─ env.d.ts
├─ tests/
│  ├─ unit/                  protocol、scheduler、session、faults、metrics 等单元测试
│  ├─ e2e/                   Playwright 监视流程用例
│  └─ fixtures/              回放数据（replies.txt、session-scenarios.ts）
├─ node_modules/             依赖（已忽略）
└─ dist/                     构建产物（已忽略）
```


## 构建与运行

### 电机固件（CH32M030）
用 MounRiver Studio 打开 [foc.wvproj](foc.wvproj)，构建输出到 `obj/`（`foc.elf` / `foc.hex` / `foc.map`）。

### 桥接固件（STM32F103）
```powershell
cd Host
cmake --preset Debug
cmake --build --preset Debug
```
产物：`Host/build/Debug/host.elf`。外设/引脚改动请改 `host.ioc` 后用 CubeMX 重新生成。

### 上位机 UI
```powershell
cd Host/html
npm install
npm run dev        # http://localhost:5180/
```
或直接双击 `start.cmd`。其他脚本：

| 命令 | 作用 |
| --- | --- |
| `npm run build` | `tsc --noEmit` + Vite 打包到 `dist/` |
| `npm run test` / `test:watch` | Vitest 单元测试 |
| `npm run e2e` | Playwright 端到端测试 |
| `npm run lint` / `typecheck` | ESLint（零警告）/ 类型检查 |