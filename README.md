# Heater OLED Simulator

## 快速开始

CMake + C11 的 128×64 OLED 模拟器，支持 Windows（Win32）和 Linux（X11）。`include/oled_fb.h` 原样保留 CH32X-InductionHeater 的 OLED 接口；额外提供四键状态读取和日志。

**运行本仓库示例。** Windows 安装 CMake 3.20+、Visual Studio 2022 的“使用 C++ 的桌面开发”，在仓库目录执行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\oled_simulator.exe
ctest --test-dir build -C Release --output-on-failure
```

示例顺序为 `oled_fb_init()` → `demo_init()`（只执行一次）→ 循环 `demo_update()`。`examples/demo.c` 中的初始化函数重置应用状态、打印初始化日志、绘制首帧；循环更新按键和画面。窗口显示文本、线条、空心/实心矩形；聚焦后按主键盘 **1–4** 可看到对应开关的 `DOWN/UP` 和变化日志，支持组合键，失焦自动复位。关闭窗口或 Esc 退出示例。

Linux 安装 CMake、C 编译器和 X11 开发库后，使用 `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`、`cmake --build build`、`./build/oled_simulator`；需要 X11 或 XWayland 图形会话。当前仅验证 Windows。

**作为其它 CMake 项目的子模块。** 在宿主仓库执行：

```sh
git submodule add https://github.com/lxydiy/vibecoding-oled-simulator.git third_party/oled-simulator
# 后续克隆宿主仓库时：git submodule update --init --recursive
```

宿主 `CMakeLists.txt` 添加：

```cmake
add_subdirectory(third_party/oled-simulator)
add_executable(my_app main.c) # 若已有应用 target，保留原定义即可
target_link_libraries(my_app PRIVATE oled::sim)
```

作为子项目时默认只构建静态库，不构建示例/测试，不覆盖父项目的 C 标准或测试选项。头文件目录、系统库和 C11 要求随 `oled::sim` 传递，Windows 无需另外复制 DLL。可在 `add_subdirectory()` 前设置 `OLED_SIM_BUILD_EXAMPLE`、`OLED_SIM_BUILD_TESTS`；若设置 `OLED_SIM_BUILD_SHARED=ON` 使用动态库，Windows 还需把 `oled_sim.dll` 复制到宿主 EXE 旁。

在宿主的 `main()` 中先初始化一次模拟器，再执行应用自己的单次初始化和循环。例如，把以下流程接入已有程序（`app_init/app_update/app_running` 是宿主自己的业务函数）：

```c
#include "oled_fb.h"
#include "oled_switch.h"
#include "ui_log.h"

void app_init(void);
void app_update(void);   /* 保留应用自己的延时/调度 */
bool app_running(void); /* 宿主自己的退出条件 */

int main(void)
{
    oled_fb_init();      /* 调用一次即自动弹窗，事件由内部线程处理 */
    app_init();         /* 模拟固件 while (1) 前的单次初始化 */
    UI_LOGI("app", "initialized");
    while (app_running()) {
        app_update();
    }
    return 0;
}
```

应用中通过 `oled_switch_is_pressed(1)` 读取开关 1 的保持状态（编号 1–4，`true` 按下）；绘图后调用 `oled_fb_send_buffer()` 提交画面。OLED 函数应由同一应用线程调用。作为库时关闭窗口不会替宿主退出，宿主仍控制自己的循环；一个进程一个窗口，关闭后不重新打开。

**日志与编译期裁剪。** 模拟器使用 `include/ui_log.h` + `src/ui_log.c`，由库统一编译，无需应用再添加 `.c`。输出到 `stderr`，参考 [ESP-IDF 日志格式](https://docs.espressif.com/projects/esp-idf/en/v5.0/esp32/api-reference/system/log.html)：

```text
I (123456) demo: init: OLED 128x64, switches 1-4
I (123472) keys: SW1 DOWN
```

括号内是系统单调时钟毫秒数，不是应用启动后的相对时间。日志默认包含 ERROR/WARN/INFO，自动补换行；单条使用 1024 字节缓冲区，过长截断。使用 E/W/I/D/V 宏或通用函数调用形式：

```c
#define UI_LOG_LEVEL UI_LOG_DEBUG       /* 全局上限，也可用编译定义 */
#define UI_LOG_MIN_LEVEL UI_LOG_WARN    /* 下限：此文件仅输出 W/I/D */
#include "ui_log.h"

void example(void)
{
    UI_LOGI("app", "value=%d", 42); /* TAG 可直接使用字符串 */
    ui_log_write(UI_LOG_DEBUG, "app", "debug message");
}
```

级别为 NONE=0、ERROR=1、WARN=2、INFO=3、DEBUG=4、VERBOSE=5。所有配置宏须在包含头文件前定义；`UI_LOG_LOCAL_LEVEL` 可进一步降低当前文件上限。应用全局配置可用 `target_compile_definitions(my_app PRIVATE UI_LOG_LEVEL=UI_LOG_DEBUG)`。日志宏至少需要 TAG、格式字符串两个参数，例如 `#define TAG "app"` 后使用 `UI_LOGI(TAG, "ready")`，或直接 `UI_LOGI("app", "ready")`。关闭的日志通过 `sizeof` 的不求值语义保留普通 C 函数调用检查；参数数量和类型由编译器按函数原型诊断，没有额外断言或类型检查宏。所有参数表达式仍须符合 C 语法及声明要求，但不会被求值，也不会输出日志。

**实机生产环境完全关闭日志。** 只复制 `examples/no_logging/ui_log.h` 到实机工程的头文件目录，继续使用相同的 `#include "ui_log.h"` 和日志调用，**不加入模拟器或 `src/ui_log.c`**。该替代头文件只声明日志函数，使用 `sizeof` 保留编译诊断而不执行调用；不需要函数实现，不求值参数，也不输出。确保替代头文件被实际选中（不要让源码旁残留同名的模拟器头文件）；若通过 include 路径切换，可将它的目录放在最前。
