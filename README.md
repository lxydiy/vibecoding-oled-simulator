# Heater OLED Simulator

C11 + CMake 的 128×64 单色 OLED 桌面模拟器。Windows 使用 Win32/GDI，Linux 使用 X11/pthreads；Windows 无需 SDL、Qt 等额外依赖。

## Windows 构建与运行

安装 CMake 3.20+、Visual Studio 2022 的“使用 C++ 的桌面开发”（提供 C 编译器及 Windows SDK），在本目录执行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\oled_simulator.exe
ctest --test-dir build -C Release --output-on-failure
```

示例包含 6×8 文本、交叉线、空心矩形、实心矩形和四个开关的 `UP` / `DOWN` 状态。聚焦模拟器窗口后，按住主键盘 **1、2、3、4**，分别模拟开关 1–4；支持同时按住多个键。松开立即复位，失去焦点或关闭窗口也会复位。Esc 或关闭窗口退出示例。窗口可缩放，OLED 始终保持 128×64 逻辑分辨率、居中整数倍放大。

`oled_simulator --smoke` 运行约一秒后自动退出，适合构建验证。

## Linux 构建

需要 CMake、C11 编译器、pthread、X11 开发库及可用的图形会话（Wayland 会话需要 XWayland）。例如 Debian/Ubuntu 的依赖为 `build-essential cmake libx11-dev`：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/oled_simulator
```

本次只验证了 Windows；Linux 后端已实现，尚未在 Linux 编译或运行。Linux 的 CTest 默认只运行不依赖图形会话的缓冲区测试。

## 应用层接口

`include/oled_fb.h` 从以下文件逐字节复制，未增加或修改 OLED 接口：

```text
C:\Users\lxy\Desktop\CH32X-InductionHeater\User\hal\oled\oled_fb.h
```

OLED 只暴露原有 10 个 `oled_fb_*` 函数。按键需求单独增加 `include/oled_switch.h`，只有一个接口：

```c
bool oled_switch_is_pressed(uint8_t switch_id); /* 1..4，true 按下，false 抬起 */
```

无效编号返回 `false`。读取的是当前保持状态，不是翻转开关、边沿事件或事件队列；重复按键消息不会翻转状态，极短的按下/松开可能在应用两次轮询之间完成。按键状态查询可跨线程调用。只映射主键盘 1–4，不映射数字小键盘。

最小应用代码：

```c
#include "oled_fb.h"
#include "oled_switch.h"

void app_init(void)
{
    oled_fb_init();
}

void app_update(void)
{
    oled_fb_clear_buffer();
    if (oled_switch_is_pressed(1)) {
        oled_fb_draw_dot(64, 32);
    }
    oled_fb_send_buffer();
}
```

应用自行决定轮询周期。示例宿主约每 16 ms 调用一次 `examples/demo.c` 的 `demo_update()`；该示例的绘图代码仅包含上述两个公共头文件和私有字体。文本、线条、矩形辅助函数均为示例中的 `static` 函数，不属于模拟器对应用暴露的接口。

在其他 CMake 工程中使用：

```cmake
add_subdirectory(path/to/Heater-OLED-simulator oled-simulator)
add_executable(my_app main.c)
target_link_libraries(my_app PRIVATE oled::sim)
```

Windows 运行时须将生成的 `oled_sim.dll` 放在应用 EXE 同目录或 DLL 搜索路径内。公共头文件不包含 Win32/X11 类型；`src/` 是私有实现，不要加入应用的头文件搜索路径。Windows DLL 仅导出 10 个 OLED 函数及 1 个按键查询函数；Linux 使用隐藏符号可见性限制内部实现。

## 行为约定

- 缓冲区为 1024 字节，`index = (y / 8) * 128 + x`，字节最低位对应页内最上面的像素。
- `draw_image()` 的图片采用相同分页方式，数据长度至少为 `w * ((h + 7) / 8)`；每页含 `w` 字节。
- 绘制点、图片采用按位 OR 叠加，不擦除已有像素。`invert` 只反转图片有效区域的源像素。需要擦除时先清屏再重画。
- `clear_buffer()` / `fill_buffer()` / 绘图只修改绘图缓冲区，调用 `send_buffer()` 才提交到屏幕。窗口线程按约 60 Hz 显示最新提交帧。
- `enable(false)` 关闭显示但保留已提交内容，重新开启后恢复。开关屏和淡入淡出不会意外提交尚未发送的绘图缓冲区。
- 初始化时清屏并开启显示，对比度为参考实现的 `0xCF`。淡入/淡出沿用参考对比度序列，`step=0` 按 1 处理，淡出最终关屏，淡入最终恢复 `0xCF`。模拟器为每一步增加 4 ms 延迟以便观察；调用会阻塞应用线程，窗口线程仍可处理事件。
- 相比参考 `draw_image()`，修正了非 8 倍数高度、底边裁剪、8 位坐标加法回绕；正常页格式和 OR 叠加语义保留。空图片指针及屏幕外坐标安全忽略。
- OLED 函数由一个应用线程顺序调用；不支持多个线程同时绘图。窗口和键盘处理在内部独立线程，应用无需新增事件泵接口。
- 一个进程一个模拟器窗口。先调用 `oled_fb_init()`，再使用显示接口；运行中再次初始化会清屏复位。窗口关闭后不支持重新打开。作为库使用时，关闭窗口不会强制退出宿主进程；示例宿主使用私有生命周期逻辑退出。退出时自动回收窗口线程。不支持运行期间主动卸载共享库。
- 因原有初始化接口没有错误返回值，无法创建窗口时输出错误并以非零状态结束进程。Linux 无图形会话时会提示检查 `DISPLAY`。

## 参考与验证

字体从参考目录 `ssd1306_font_static.h` 的 `F6x8` 提取，保留前 91 个条目（字符编号 32–122）的原始字模，包括参考文件的自定义字形；没有假设参考表完整覆盖所有可打印 ASCII。文本及 Bresenham 线条绘制参考 `User/hal/oled.c`。

Windows 验证包括：

- 缓冲区延迟提交、开关屏内容保留、页格式、OR/反色、500 组确定性图片裁剪对照测试、全部 256 个淡入淡出步长。
- 通过实际 DLL 创建 Win32 窗口，读取 GDI 渲染像素验证四角定位、关屏/恢复及缩放；注入 Win32 按键消息检查按下/松开、重复、组合、失焦及关闭复位。
- 示例程序启动、运行和退出的 smoke test。
- 宿主未关闭 DLL 窗口而直接从 `main()` 返回时的退出清理。

可额外运行示例画面验证脚本，保存默认画面及开关 1、3 按下的 PNG，并验证松开后画面恢复：

```powershell
powershell -ExecutionPolicy Bypass -File tests/verify_demo.ps1 -Executable build/Release/oled_simulator.exe -OutputDirectory build/screenshots
```

这些是软件窗口与合成输入验证，不是实体 OLED、电气时序或真实开关硬件验证。
