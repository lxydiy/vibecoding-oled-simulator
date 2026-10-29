# OLED 字体生成器

用 Python 将 YAML 文案和 TTF/OTF/TTC 字体转换为可直接编译的单色 C 字库。生成结果包含字体、Unicode 索引和字符串宏，适合加入源代码管理。

## 快速开始

在项目根目录安装生成器依赖（Python 3.10 或更新版本）：

```powershell
python -m pip install -r requirements.txt
```

最小配置如下。将字体文件放在 YAML 旁边的 `fonts` 子目录；`source` 相对 **YAML 所在目录** 解析。

```yaml
namespace: ui
fonts:
  simsun_16:
    source: fonts/simsun.ttf
    pixel_size: 16
pages:
  home:
    - font: simsun_16
      texts:
        title: "你好，OLED"
```

假设保存为 `resources.yml`，手动运行：

```powershell
python generate.py resources.yml
```

默认写到**命令当前工作目录**的 `out` 中，而不是 YAML 旁边：

```text
out/
  ui_font_simsun_16.c
  ui_resources.h
```

指定项目使用的输出位置：

```powershell
python generate.py resources.yml --output-dir src/ui/generated
```

`-o` 是 `--output-dir` 的简写。相对输出路径以命令当前工作目录为准。

### 运行本项目示例

`example.yml` 是独立示例；当前模拟器使用 `src/ui/strings.yml`，字体路径相对该配置解析。

项目配置使用 `src/ui/fonts/simsun.ttc` 的第一个字体。修改项目文案后，在项目根目录重新生成：

```powershell
python generate.py src/ui/strings.yml -o src/ui/generated
cmake -S . -B build
cmake --build build --config Release
.\build\Release\oled_ui.exe
```

模拟器显示 16 像素标题、格式化温度，以及同屏的 32 像素“确定”和 16 像素“取消”。在控制台按 Enter 退出；关闭 OLED 窗口不会代替宿主结束进程。

## YAML 文件格式

不写 `version`。下面展示全部已定义字段；使用自己的字体和字典文件路径。未知字段会警告并忽略，不会传给栅格化器。

```yaml
namespace: ui

fonts:
  simsun_16:
    source: fonts/simsun.ttf
    pixel_size: 16
    bpp: 1                       # OLED 后端忽略；每次转换最多警告一次
    extra_ranges:                # 闭区间，可以重叠；自动去重
      - from: 0x20
        to: 0x7E                 # 可打印 ASCII；0x7F 是 DEL
    extra_chars:
      - "℃"
      - "°±—abcabc"               # 拆成字符后去重
      - 0x4E2D                   # 整数：Unicode 码点 U+4E2D
      - "0x1234"                 # 字符串：字符 0、x、1、2、3、4
    extra_chars_files:
      - dict/common.txt          # UTF-8，无 BOM，纯文本
    other_tool_option: true      # 未知字段：警告并忽略

  simsun_32:
    source: fonts/simsun.ttf
    pixel_size: 32
    bpp: 2                       # 仍生成 1 bpp 数据

pages:
  home:
    - font: simsun_16
      texts:
        confirm: "确定"
        Confirm: "确定"           # 同身份、同内容：合并
        Cancel: "取消"
        multiline: "第一行\n第二行"
    - font: simsun_32
      texts:
        Confirm: "确定"           # 同一个宏，同时为两种字体生成字形
        cancel: "取消"
        unicode_escape: "\u6D4B\u8BD5"  # 与直接写“测试”等价

  heater:
    - font: simsun_16
      texts:
        title: "感应加热"
        target_temp: "目标温度"
        temp_format: "%d℃"       # 原样保存，不解析 printf 格式
        running: "加热中"
        confirm: "确定加热"        # 页面不同，宏不同

  settings:
    - font: simsun_16
      texts:
        title: "设置"
```

### namespace 和标识符

- `namespace` 为必填字符串，先转为大写，再检查结果是否符合 ASCII 标识符格式；输出文本宏使用该结果，输出字体符号再转为小写。`ui`、`Ui`、`UI` 的输出相同。
- 页面名、文本键匹配 `[A-Za-z_][A-Za-z0-9_]*`，输出宏时大写。不转换标点或拼音。
- 命名空间不得以下划线开头，以免输出全局 C 保留标识符。
- 字体名只能包含小写 ASCII 字母、数字、下划线，至少一个字符。由于有固定前缀，字体名本身允许数字开头。大写字体名直接报错。
- 字体变量为 `<小写namespace>_font_<字体名>`，例如 `ui_font_simsun_16`。对应文件名为 `ui_font_simsun_16.c`。
- 文本宏为 `<大写namespace>_TEXT_<大写页面名>_<大写文本键>`。
- 生成器检查其全部公开符号与运行时类型/函数的冲突；例如 `namespace: ui` 下的字体 `t` 会与类型 `ui_font_t` 冲突，因此拒绝。
- 一个输出目录只放一个资源配置包，头文件固定为 `ui_resources.h`。应用不要自行定义这些资源前缀下的宏或变量。

### fonts

`fonts` 是字体名到字体配置的映射。所有声明的字体都会输出一个 C 文件，即使没有页面使用它。

| 字段 | 类型 / 必填 | 行为 |
| --- | --- | --- |
| `source` | 非空字符串，必填 | 字体路径，相对 YAML 所在目录；也允许绝对路径。支持带 Unicode cmap 的 TTF/OTF/TTC，TTC 固定使用第 0 个字体。 |
| `pixel_size` | 整数，必填 | 请求的栅格化尺寸，1–256 像素；布尔值不算整数。实际行高、基线、字形包围盒由字体度量计算，不强制裁成正方形。 |
| `bpp` | 可选，内容不解释 | OLED 后端一律生成 1 bpp。只要配置出现此字段，一次成功转换最多输出一条忽略警告。 |
| `extra_ranges` | 可选列表 | 每项包含整数 `from` 和 `to`，两端包含，重叠自动合并。 |
| `extra_chars` | 可选字符串或列表 | 字符串拆成 Unicode 码点；列表元素允许字符串或整数，其他类型报错。 |
| `extra_chars_files` | 可选路径列表 | 字典路径相对 YAML 所在目录；读取 UTF-8 纯文本，无 BOM。 |

Unicode 整数允许十进制或 `0x` 十六进制，限 `0–0x10FFFF`，排除代理项 `0xD800–0xDFFF`。反向区间、穿过代理项的区间报错；单个区间以及每种字体请求的唯一字符数均最多 65,535，避免误配置生成过大资源。

`extra_chars: "123123"` 等价于额外请求 `1`、`2`、`3`。`extra_chars: [0x1234]` 请求 U+1234；`extra_chars: ["0x1234"]` 不解析数值。`true`、`null`、浮点数等不会自动转换。

每种字体的字符集合为：

```text
分配给该字体的全部页面文案字符
  ∪ extra_ranges
  ∪ extra_chars
  ∪ extra_chars_files 中字体确实支持的字符
```

不会默认添加全部 ASCII。需要动态数字、英文或 `%d` 格式化结果时，请显式加入可打印 ASCII 或需要的范围。

### 字典文件

字典是 UTF-8 无 BOM 纯文本，无注释、转义或码点列表语法。文件内的 `\u4E2D` 是六个实际字符，不代表“中”；`U+4E2D` 同样作为普通字符读取。文件名 `gb2312.txt` 不改变编码要求。

CR/LF 和 TAB 不加入字形集合；普通空格保留。绘制端只解释 LF，不处理 CR/TAB 的排版。缺少文件、非法 UTF-8、BOM 会报错。其余不支持的控制字符会报错。

字典请求的字符在字体中不存在：输出警告并跳过，按字体汇总并最多列出前 16 个缺失码点。页面文案、`extra_chars`、`extra_ranges` 请求的字符不存在：报错。某个字同时由文案和字典请求时，按必需字符处理，不能静默跳过。

字典只能提供码点，不能提供字形。给宋体添加 Font Awesome 的码点字典不会自动合并 Font Awesome 字体。

### pages、font 和 texts

`pages` 是页面名到分组列表的映射；每组必须有 `font` 和 `texts`。分组不命名，也不参与输出符号。`font` 必须与字体声明完全匹配，区分大小写。

`texts` 的值只能是字符串；空字符串允许。`true`、`123`、`null`、列表和映射都报错，请需要时加引号。没有多语言翻译、格式字符串分析或运行时字符集推断。

字符串解析后的内容精确比较：不去除空白、不改变大小写、不做 Unicode 规范化。`"\u6D4B\u8BD5"` 与 `"测试"` 的解析结果相同；实际换行与两个字符 `\`、`n` 不相同。

#### 名称冲突规则

1. 完全相同的 YAML 映射键，在加载阶段报错，不能先覆盖再检查。此规则适用于所有映射，包括未知配置字段中的映射。
2. 页面名仅大小写不同，如 `home` / `HOME`，报错，不合并。
3. 文案身份为 `(页面名大写, 文本键大写)`。同一身份、相同内容，合并宏，并为所有关联字体收集字符。
4. 同一身份、不同内容，报错，并显示两处声明的位置。
5. 不同身份拼出相同宏名，始终报错，即使内容相同。例如 `home_menu.confirm` 与 `home.menu_confirm`。
6. 不同宏允许相同内容；字形仍然按字体自动复用。

以下是错误示例，不能放进正常生成配置：

```yaml
texts:
  Cancel: "取消"
  CANCEL: "cancel"  # 同一身份、内容不同
  boolean: true    # 非字符串
  number: 123      # 非字符串
```

#### C 字符串转义

保留 YAML 中每个字符的原始写法，生成的头文件以 UTF-8 保存，便于 IDE 悬停阅读中文宏：

- 直接输入的中文和其他字符原样输出，不因为含有非 ASCII 字符而把整条字符串转义。
- YAML 双引号字符串中的显式 `\uXXXX`、`\UXXXXXXXX`（以及 `\xHH`）按对应字符的 UTF-8 字节输出 C `\xHH` 转义；同一字符串中的其他明文字符仍保留。
- 引号、反斜杠、换行、回车、制表符和问号按 C 字符串规则转义。单引号 YAML 或双反斜杠写出的 `\u` 是普通文本，不作为 Unicode 转义。

```c
/* YAML: title: "中A" */
#define UI_TEXT_HOME_TITLE "中A"

/* YAML: title: "中\u6D4BA" */
#define UI_TEXT_HOME_TITLE "中\xE6\xB5\x8B""A"

/* YAML: title: "line1\nline2" */
#define UI_TEXT_HOME_TITLE "line1\nline2"
```

字节转义后紧跟十六进制字符时，自动用相邻字符串字面量隔开，避免 C 的 `\x` 吞入后续字符。重复声明内容相同时，每个位置优先保留明文写法，输出不受声明顺序影响。生成的宏可用于 `snprintf` 和相邻字面量拼接。

顶层 CMake 仅在 `MSVC` 下为应用添加 `/utf-8`，统一源文件和执行字符集，使中文字符串及日志文本以 UTF-8 编译。

NUL 和除 TAB/LF/CR 外的 C0/C1 控制字符拒绝生成。TAB/LF/CR 保留在字符串中，但不取字模。YAML 别名及 `<<` 合并键暂不支持；使用直接声明可以确保重复键和来源检查明确。

## 生成文件和可重复性

每个字体输出一个 `.c`，头文件 `ui_resources.h` 包含 `#include "ui/draw/text.h"`、所有字体的 `extern` 声明和字符串宏。编译时添加 `src` 为 include 路径，并编译 `text.c` 和使用的字体 `.c`。文本宏直接输出 `#define`，不生成逐宏的 `#ifdef/#error` 检查；YAML 中的名称冲突仍在生成阶段检查。

字体文件内只有只读数据：

- Unicode 数组：`uint32_t codepoints[]` 按码点严格递增，数组下标就是字形下标，供二分查找；不额外存储索引。
- 字形表：点阵偏移、宽高、相对基线偏移、前进距离。
- 点阵池：1 bpp，按页排列，每字节纵向存储 8 个像素，最低位在上。
- 字体对象：表指针、字符数、行高、基线。

同一个字体内，重复出现的同一码点只存一份。每个不同码点都有独立的字形描述，即使描述相同也复制一份，以保证 `codepoints[i]` 对应 `glyphs[i]`。相同尺寸、相同像素的点阵仍可共享点阵池中的字节。不同字体分别存储，避免额外的跨文件依赖。

生成器固定排序字体、宏和码点，不写时间戳、机器名、输入绝对路径。输出使用 UTF-8 无 BOM、LF 换行。内容未变化时不改写文件，也不改变其修改时间。

生成时不计算字体 SHA-256，也不输出工具版本、字号、位深或字形数量注释。相同配置、相同字体字节和相同工具链产生相同输出；不同字体版本或栅格化器版本可能改变字形，不能仅凭字号保证跨环境逐字节相同。Python 依赖固定在 `requirements.txt`。

所有配置和字体先校验并完成内存生成，再写文件；配置错误或缺字不会覆盖旧结果。每个文件通过临时文件替换，但多个文件不是文件系统级事务，写入中断后请重新生成。不要并发写同一输出目录。

生成器不会删除目录中旧的 C 文件或用户文件。删除或重命名字体后，请手动清理对应旧输出，并更新 `CMakeLists.txt` 的 `UI_FONT_SOURCES`。生成文件应和 YAML 改动一起纳入版本管理。

## C 端绘制接口

绘制函数**每次显式传入字体**，没有全局当前字体或字体上下文：

```c
#include "oled_fb.h"
#include "ui/draw/text.h"
#include "ui/generated/ui_resources.h"

oled_fb_init();
ui_text_draw(0, 0, &ui_font_simsun_16, UI_TEXT_HOME_CONFIRM);
ui_text_draw(0, 22, &ui_font_simsun_32, UI_TEXT_HOME_CONFIRM);
oled_fb_send_buffer();
```

```c
void ui_text_draw(int16_t x, int16_t y,
                  const ui_font_t *font, const char *utf8);
ui_text_size_t ui_text_measure(const ui_font_t *font, const char *utf8);
const ui_glyph_t *ui_font_find_glyph(const ui_font_t *font, uint32_t codepoint);
```

- `x/y` 是第一行行框左上角，基线由字体对象提供。
- `draw_glyph()` 直接将原始点阵指针交给新版 `oled_fb_draw_image()`，没有逐点绘制或额外缓冲区。坐标和尺寸不再截断为 8 位，支持负坐标及超过 255 像素的字形；宽高须不超过 `INT16_MAX`，超出该 API 范围的字形跳过绘制。
- 透明绘制，仅点亮前景像素；调用者负责清屏或清除旧区域，以及 `oled_fb_send_buffer()` 提交。
- 仅 LF 换行并回到本次绘制的起始 x；没有 CR/TAB 的专用处理。
- `ui_text_measure` 返回最大行前进宽度和总行高；不包含字形水平外伸。空字符串返回 `{0, 0}`，尾部换行保留一个空行。
- 坐标和测量结果使用 `int16_t`。不使用饱和加法，行宽或总行高即将超出 `INT16_MAX` 时停止处理；字形原点、基线和偏移统一用 `int32_t` 计算，确认可见后再传入上游有符号坐标 API 裁剪。
- 无自动折行、字距调整、复杂文字整形或彩色 Emoji。普通中文、ASCII、符号按 Unicode 码点绘制，支持四字节 UTF-8。
- 查不到字形直接跳过，不绘制替代符号，也不增加前进距离。非法 UTF-8 每次消耗一个错误字节并跳过，不转换为 U+FFFD。
- 可以为同一个宏传不同字体，但必须在 YAML 中把该文案分配给相应字体，或以额外字符覆盖它；生成器不假设所有字体覆盖所有文本。
- 所有绘图调用遵循 `oled_fb` 的单应用线程要求。传入生成器提供的有效字体对象和 NUL 结尾的 UTF-8 字符串。

格式化由调用者完成：

```c
char buffer[32];
snprintf(buffer, sizeof buffer, UI_TEXT_HEATER_TEMP_FORMAT, 180);
ui_text_draw(0, 0, &ui_font_simsun_16, buffer);
```

生成器不解释 `%d`、`%s`，也不校验格式参数。动态字符必须在配置中显式覆盖。

## 测试

在项目根目录执行：

```powershell
python -m unittest discover -s tests -p test_generate.py -v
cmake -S . -B build
cmake --build build --config Release
```

Python 测试自行构造小型字体，不依赖本机宋体；覆盖重复键、宏冲突、字体分组并集、严格类型、字典警告、UTF-8 转义、码点与字形下标对应、点阵复用、重复生成一致性及失败时保留旧输出。CMake 独立构建模拟器，可运行模拟器检查实际显示效果。

生成器成功返回 0；配置、字体或文件错误返回 1，并向 stderr 输出诊断。警告不会使转换失败。

## 实现参考

- [Pillow ImageFont](https://pillow.readthedocs.io/en/stable/reference/ImageFont.html)：使用 BASIC 布局、单色 `getmask2` 和基线锚点提取点阵与度量。
- [fontTools TTFont](https://fonttools.readthedocs.io/en/stable/ttLib/ttFont.html)：读取 Unicode cmap，并在栅格化前检查字符是否存在。

