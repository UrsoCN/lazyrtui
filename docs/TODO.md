# LazyRTUI - ROS 2 Terminal User Interface 设计方案与规划

`lazyrtui` 是一个专注于 ROS 2 环境的高效、模块化终端用户界面 (TUI) 工具。主要目标是在无图形界面或 SSH/旧版 Terminal 环境下，提供键盘优先、易于使用、性能优异的 ROS 2 操作与监控体验。

---

## 一、 TUI 框架选型对比

针对 Python 生态中主流的 TUI 框架，结合 ROS 2 的异步（`asyncio`/`rclpy`）及交互需求，进行如下评估：

| TUI 框架 | 优点 | 缺点 | 适用度评估 |
| :--- | :--- | :--- | :--- |
| **Textual** *(推荐)* | 1. 基于 `asyncio` 原生异步，与 ROS 2 `rclpy` 异步 Spinning 完美结合；<br>2. UI 与样式隔离（类似 HTML/CSS TCSS）；<br>3. 内置丰富的组件（TabbedContent, DataTable, Tree, Input, Header/Footer）；<br>4. 强大的键盘快捷键与焦点管理能力；<br>5. 插件化扩充容易，支持 rich / plotext 等渲染绘制折线图。 | 需要 Python 3.8+（当前环境为 Python 3.12，无影响）。 | ⭐⭐⭐⭐⭐ **(首选)** |
| **prompt_toolkit** | 1. 擅长交互式命令行、补全、Prompt 编辑器；<br>2. 低层控制力极强。 | 编写多 Tab 复杂 Dashboard 布局的 Boilerplate 代码极其繁琐，缺乏高层 UI 组件库。 | ⭐⭐⭐ |
| **urwid** | 1. 经典的 Python TUI 框架；<br>2. 支持 asyncio 事件循环。 | 语法相对冗长老旧，组件样式修饰较复杂，缺乏响应式状态管理和 CSS 样式分离。 | ⭐⭐ |
| **asciimatics** | 1. 擅长复杂字符动画与特效。 | 缺乏构建现代化 Tab/Dashboard UI 的结构化控件。 | ⭐ |

### 选型结论：选择 **Textual**
* **理由**：Textual 拥有极高的开发效率、现代化 CSS 样式解耦设计、原生 `asyncio` 支持以及完善的键盘焦点控制机制，是目前 Python 生态构建复杂 TUI 应用程序的首选框架。

---

## 二、 系统架构与模块设计

### 1. 整体架构解耦设计
为满足“布局与处理逻辑隔离”、“支持自定义插件/配置”的要求，采用分层解耦架构：

```
+-------------------------------------------------------------------+
|                        UI 视图层 (Textual)                          |
|  [TabbedContent: Node | Topic | Service | Action | Interface | Bag] |
|             - Widgets (DataTable, Tree, Modal, Plotter)           |
|             - TCSS 样式规则与按键绑定 (Bindings)                    |
+---------------------------------+---------------------------------+
                                  | (Events / Reactive State)
+---------------------------------v---------------------------------+
|                       业务逻辑层 (Controller / App)                |
|             - State Management & View Switching                   |
|             - Plugin Registry & Config Loader                     |
+---------------------------------+---------------------------------+
                                  | (Async Calls / Callbacks)
+---------------------------------v---------------------------------+
|                    ROS 2 核心包装层 (ROS2Manager)                   |
|  - 保持单一长期运行 Node (LazyRTUINode)                               |
|  - 避免创建临时 Node (无 ~/.ros/log 日志垃圾)                        |
|  - Dynamic Subscription / Service Client / Action Client          |
+-------------------------------------------------------------------+
```

---

## 三、 核心功能与 Tab 页设计

### Tab 1: Node 页面 (节点管理)
* **功能**：
  * 左侧节点列表（支持 `/` 关键字搜索过滤，`r` 手动刷新）。
  * 右侧节点详情（显示该节点 Publisher/Subscriber 列表、Service 列表、Action 列表）。
  * 支持节点 Parameter 查看与在线修改。

### Tab 2: Topic 页面 (话题监控与数据绘制)
* **功能**：
  * 话题列表：展示 Topic 名称、消息类型、发布频次 (Hz)、订阅数。
  * 话题 Echo：即时格式化输出 JSON/YAML 格式消息文本。
  * **自定义渲染/图表绘制**：针对数值型 Topic（如 `/cmd_vel` 或传感器数据），调用插件（如 `plotext`）直接在终端中实时绘制折线图/仪表盘。
  * 话题发布：输入 JSON/YAML 载荷进行测试发布。

### Tab 3: Service 页面 (服务调用)
* **功能**：
  * 服务列表：名称、类型、提供节点。
  * 请求构建器 (Request Builder)：自动解析服务 Req 结构。
  * **预设参数插件**：支持从配置文件载入默认值/候选参数列表，按下快捷键即可一键填充配置。
  * 异步调用服务并展示响应结果及耗时。

### Tab 4: Action 页面 (动作交互)
* **功能**：
  * Action 列表：Goal / Feedback / Result 类型查看。
  * 动作发送：支持 Goal 取消、Feedback 实时日志流展示以及 Final Result 格式化输出。

### Tab 5: Interface 页面 (接口定义查询)
* **功能**：
  * Tree 结构展示所有已知 `msg`, `srv`, `action` 的字段类型定义与嵌套结构（类似 `ros2 interface show`）。

### Tab 6: Bag / Log 页面 (录制与日志)
* **功能**：
  * Log 过滤查看。
  * 简易 ROS Bag 录制与回放控制。

### Tab 7: TF Tree 页面 (坐标变换树)
* **功能**：
  * 实时监听与解构 `/tf` 和 `/tf_static` 坐标关系。
  * 树形（Tree Widget）展示 Parent -> Child 坐标系链路及 Translation / Rotation 数据。

### Tab 8: About & Settings 页面 (关于与设置)
* **功能**：
  * 展示软件版本、当前 ROS 2 分布版本环境状态。
  * 展示配置文件路径及快捷键参考指南。
  * 提供配置选项开关与修改。

---

## 四、 键盘快捷键与焦点控制设计

为了在无鼠标的旧终端环境下依然顺畅操作，设计全键盘导航体系：

1. **Tab 页面切换**：
   * `1` ~ `6`：快捷切换指定 Tab 页。
   * `Tab` / `Shift+Tab` 或 `Ctrl+Left` / `Ctrl+Right`：按顺序切换页签。
2. **面板焦点切换**：
   * `w` 或 `Ctrl+w`：在“左侧列表面板”与“右侧详情面板”之间循环切换焦点。
3. **列表/项操作**：
   * `j` / `k` 或 `Down` / `Up`：上下移动选择。
   * `g` / `G`：跳转至列表顶部 / 底部。
   * `/`：激活当前面板的快速搜索框。
   * `Enter`：展开详情 / 执行选中操作 / 弹窗二次确认。
4. **功能快捷键**：
   * `e`：开启/停止当前 Topic 的 Echo。
   * `p`：开启/停止数据绘制 Plot 或发布 Topic。
   * `c`：调用选中的 Service / 发送 Action Goal。
   * `r`：强制刷新 ROS 拓扑关系。
   * `?`：打开全局快捷键帮助弹窗。
   * `q`：安全退出程序。

---

## 五、 解决 ROS 日志垃圾 (Avoiding `~/.ros/log` Spam)

### 问题分析
标准的 `ros2 service call` 或 `ros2 topic echo` 命令行工具在每次执行时都会在后台 spawn 一个临时的 ROS 2 节点，生成大量的日志文件积攒在 `~/.ros/log` 中。

### 解决方案
1. **单一常驻 Node**：
   * 在应用程序启动时仅初始化一个全局单例 ROS 节点（`LazyRTUINode`）。
2. **动态 Executor 与 Client/Subscription 管理**：
   * 拓扑查询直接调用 `LazyRTUINode` 的内置 API（`get_topic_names_and_types()`等）。
   * 话题 Echo 时，通过 `rclpy.create_subscription()` 动态创建订阅者，关闭 Echo 时调用 `destroy_subscription()` 销毁订阅。
   * 服务调用时，通过 `create_client()` 动态创建 Client，调用完毕后调用 `destroy_client()`。
3. **日志输出控制**：
   * 在 `rclpy.init()` 时传入 `--ros-args --log-level WARN`，并将节点日志打印定向至内存日志缓冲区，关闭写入磁盘日志文件，确保 `~/.ros/log` 保持干净。

---

## 六、 配置文件与用户自定义插件系统

### 1. 配置文件定义 (`~/.config/lazyrtui/config.toml` 或 `./lazyrtui.yaml`)
支持用户定制：
* 快捷键映射 (Keybindings Overrides)
* 主题与配色方案 (Theme / Color Palette)
* 话题格式化/绘图规则设置：
  ```yaml
  topics:
    "/turtle1/pose":
      plot_fields: ["x", "y"]
      plot_type: "line"
  services:
    "/spawn":
      preset_params:
        x: 2.0
        y: 2.0
        theta: 0.0
        name: "turtle2"
  ```

### 2. 插件架构 (Plugins)
* 动态加载 Python 模块（如 `lazyrtui/plugins/` 目录下的用户自定义扩展）。
* 允许用户注册自定义 Topic 解析与 Widget 渲染类。

---

## 七、 开发路线图 (TODO / Progress)

- [x] **阶段 1：项目基础设施搭设**
  - [x] 安装依赖 (`textual`, `rclpy`, `plotext`, `pyyaml`) 到 `.venv`。
  - [x] 搭建项目目录结构 (`src/lazyrtui/...`)。
- [x] **阶段 2：ROS 2 核心抽象层 (ROS2Manager)**
  - [x] 实现 `LazyRTUINode` 单例包装。
  - [x] 实现后台 Thread / Asyncio Task 进行 `rclpy.spin()`。
  - [x] 实现 Node / Topic / Service / Action 拓扑发现接口。
  - [x] 实现 TF 监听与 Tree 提取（解构 `/tf` 与 `/tf_static` 坐标链路）。
  - [x] 实现无日志垃圾的动态 Subscription / Service Client。
- [x] **阶段 3：Textual UI 主框架构建**
  - [x] 实现全局 App 主界面、Header、Footer、Help Modal（`?` 键弹窗）。
  - [x] 实现基于键盘快捷键的 TabbedContent 与 Panel 焦点切换系统。
  - [x] 实现 Node/Topic/Service/Action/TF 选中行高亮事件监听与右侧 Details 真实数据同步联动。
- [x] **阶段 4：各个功能 Tab 页深度功能实现**
  - [x] Node Tab (节点拓扑与参数细节查看)。
  - [x] Topic Tab (右侧内嵌 `plotext` 实时 ASCII 波形折线图表与数据拓扑)。
  - [x] Service Tab (右侧内嵌 JSON 请求输入框、`Call Service` 按钮与异步返回 Markdown 结果显示)。
  - [x] Action Tab (右侧内嵌动作 Goal 查看与状态面板)。
  - [x] Interface Tab (基于 Tree Widget 展现 ROS 2 常见 msg/srv/action 类型层次包结构)。
  - [x] Bag Tab (ROS Bag 状态与记录管理)。
- [ ] **阶段 5：配置与插件系统**
  - [ ] 实现 `config.yaml` 配置加载。
  - [ ] 实现 Service 参数预设与 Topic 绘图插件。
