# 智绘教课堂批注版（Inkeys Classroom Annotation）

面向 **希沃一体机 / 触控大屏课堂** 的桌面屏幕批注工具，基于开源软件 **智绘教 Inkeys** 二次开发，新增了一套独立的课堂批注层：左右双工具栏、11 种可编辑几何图形、套索选中、整体缩放/旋转、选中删除等。

> 本项目是 [Alan-CRL/Inkeys](https://github.com/Alan-CRL/Inkeys) 的修改版（fork），遵循 **GPL-3.0** 许可证发布。原项目版权归其作者所有。

---

## 功能特性

### 批注工具层（本项目新增）

- **独立批注层**：与希沃白板自带批注工具互不干扰，整屏透明覆盖层，不遮挡、不改变原课件。
- **左右双工具栏**：分别贴靠屏幕左右边缘、位于屏幕下 1/3 处；两栏状态实时同步；可拖动、可收起/展开；收起态保留一个最简图标，点击即可展开。
- **工具按钮**：选择、笔、橡皮、选中（套索）、图形、清屏、收起。
- **笔**：9 种颜色、多档粗细、多档透明度；中点贝塞尔平滑，书写顺滑；切换到「选择」后笔迹保留。
- **橡皮**：圆形 30 / 70 / 140 三档 + 超大方形橡皮；擦除范围有 **黑描边、乳白填充** 的轮廓预览；支持多指快捷（可关闭）：2 指=小、3 指=中、4 指及以上=超大。
- **选择模式**：整窗鼠标穿透，可直接操作下层课件、网页、视频。
- **右击切换选择**：在笔模式下右键单击即可切回「选择」。

### 几何图形（11 种）

正方形、长方形、三角形、平行四边形、梯形、圆、直线、坐标轴、椭圆、双曲线、抛物线。

- 「图形」为独立弹出菜单，所有图形均带矢量预览图标。
- 坐标轴：红色、带原点、密集实心坐标点、细线条，标注 x/y 轴。
- 椭圆、双曲线、抛物线会标注焦点；双曲线/抛物线可通过拖动顶点与焦点改变形状。
- 1–7、9 号图形可拖顶点改变形状，但不会改变图形本质属性（正方形例外：四顶点可自由拖动）。

### 选中与变换

- **套索选中**：红色短虚线轨迹，手写笔迹与几何图形可 **混合选中**，并整体拖动。
- **选中框**：水平长方形虚线框，略大于图形；四角有小圆圈手柄：
  - 手柄到图形中心的 **距离 → 缩放**；
  - 手柄与水平线的 **夹角 → 旋转**。
- **手写笔迹**选中后只保留缩放，不提供顶点编辑、不旋转。
- **删除按钮**：每个选中框右上角外侧有红色圆形 ×，点按即删除该笔迹/图形。
- **画完即选中**：绘制任意几何图形后，自动切换到「选中」工具，方便立刻调整。

### 系统集成

- 常驻后台，托盘图标控制；单实例运行。
- 开机自启（注册表 `HKCU\...\Run`，可在配置中关闭）。
- 配置持久化到 `opt\Anno.ini`。

---

## 快捷键

| 快捷键 | 功能 |
| --- | --- |
| `Ctrl + Alt + A` | 显示 / 隐藏批注层 |
| `Ctrl + Alt + B` | 展开 / 收起左右工具栏 |
| 鼠标右键（笔模式） | 切换到「选择」模式（可配置） |
| 2 / 3 / 4 指触摸 | 小 / 中 / 超大橡皮（可配置） |

---

## 构建

### 获取源码

```powershell
git clone https://github.com/chaojicitou/Inkeys-ClassroomAnno.git
cd Inkeys-ClassroomAnno
```

### 环境要求

- Windows 10 / 11（x64）
- Visual Studio 2022 或更高版本（含「使用 C++ 的桌面开发」工作负载）
  - 本项目在 **Visual Studio 2026（v145 工具集）+ Windows SDK 10.0.26100** 上验证通过
- 源码自带 OpenSSL 静态库、imgui、HiEasyX 等依赖，无需额外下载

### 编译命令

```powershell
# 使用 VS 2026（v145）
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "智绘教\智绘教.vcxproj" /t:Build /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v145

# 使用 VS 2022（v143，工程默认）
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" `
  "智绘教\智绘教.vcxproj" /t:Build /p:Configuration=Release /p:Platform=x64
```

产物：`智绘教\x64\Release\Inkeys.exe`。

> 若使用较新的 MSVC 编译时报 `C2014`（预处理命令必须作为第一个非空白字符启动），请检查源文件头部是否存在重复的 UTF-8 BOM。本仓库已修复相关文件。

### 运行

直接运行 `Inkeys.exe` 即可。首次运行会在同目录生成 `opt\` 配置目录与运行日志。

---

## 目录结构

```
Inkeys-main/
├─ 智绘教.sln
├─ 智绘教/
│  ├─ Anno/                  ★ 本项目新增的课堂批注模块
│  │  ├─ AnnoCore.h          对象模型、文档与工具声明
│  │  ├─ AnnoDoc.cpp         对象存储、套索选择、擦除
│  │  ├─ AnnoShapes.cpp      11 种图形生成、命中、顶点编辑、渲染
│  │  ├─ AnnoUI.cpp          批注层窗口、工具栏、弹窗、选中框、输入处理
│  │  └─ AnnoApp.cpp         托盘、配置、开机自启、热键
│  ├─ src/                   皮肤、图标、字体、i18n 等资源
│  ├─ additional/            imgui 等第三方源码
│  ├─ binarypackage/         OpenSSL 等静态库
│  ├─ HiEasyX/               绘图基础库
│  └─ Idt*.cpp / *.h         上游 Inkeys 主程序源码
└─ PptCOM/                  PPT 联动组件
```

---

## 配置项（opt\Anno.ini）

| 键 | 说明 |
| --- | --- |
| `[pen] color / width / alpha` | 笔的颜色索引 / 粗细档 / 透明度档 |
| `[eraser] size` | 橡皮档位（0–3，3 为超大方形） |
| `[opt] multiFinger` | 是否启用多指快捷橡皮（1/0） |
| `[opt] rightClickSelect` | 是否启用右键切换选择（1/0） |
| `[opt] collapsed` | 工具栏是否处于收起状态（1/0） |
| `[opt] autoStart` | 是否开机自启（1/0） |
| `[bar] leftX / leftY / rightX / rightY` | 左右工具栏位置（-1 为默认） |

---

## 与上游 Inkeys 的差异

- 新增 `Anno/` 课堂批注模块（工具栏、画笔、橡皮、套索、图形、选中框）。
- 启动时默认开启开机自启。
- 修复了部分源文件头部重复 UTF-8 BOM 导致新编译器报错的问题。

---

## 许可证与致谢

- 本项目基于 [Alan-CRL/Inkeys](https://github.com/Alan-CRL/Inkeys) 修改，遵循 **GNU General Public License v3.0**，详见 [LICENSE](LICENSE)。
- 感谢 Inkeys 作者及所有上游贡献者。
- 使用了 EasyX / HiEasyX、Dear ImGui、OpenSSL 等开源组件，版权归各自作者所有。

> 本项目仅用于课堂教学场景的辅助批注，请遵守所在学校与地区的相关使用规定。
