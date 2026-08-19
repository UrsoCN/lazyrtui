# LazyRTUI - 代码评审问题清单与解决记录

本文件记录代码评审发现的问题及 deepwork 会话的解决情况。原 TODO 的 **12 项问题已全部解决并提交**（每项含 `fix:` commit，审查发现的实质问题以 `fix-review:` commit 整改）；解决方案摘要见下表，未决事项见文末"后续跟进项"。

---

## 已解决问题总览

| # | 问题 | 级别 | 涉及文件 | 解决方案摘要 | 状态（commit） |
|---|------|:---:|-----------|--------------|:---:|
| 4 | 信号处理异步安全（handler 内 stop/join/exit） | P0 | `src/main.cpp` | handler 改为纯异步安全：仅重挂 `SIG_DFL` + 原子标志；全部清理回主线程；`g_ros_mgr` 全局变量本地化 | ✅ `fbdbb41` + `5bd8214` |
| 3A | 刷新定时器不可中断（`sleep_for` 硬编码 2s） | P0 | `src/app.cpp`, `app.hpp` | `condition_variable::wait_for` 按 `config.ui.auto_refresh_interval_ms` 休眠，stop 时置位+notify 立即唤醒 | ✅ `b297966` |
| 3B | Render() 持 `data_mutex_`/图查询导致渲染卡顿 | P0 | `src/app.cpp`, `app.hpp` | `UiSnapshot` 不可变快照原子发布 + `SnapshotStringList`（`ConstStringListRef::Adapter`）绑定 Menu；Render 零锁零图查询（图查询移至刷新线程缓存） | ✅ `d6437dd` + `04230b8` |
| 1 | Python C API 无 GIL 保护（订阅/定时线程崩溃） | P0 | `src/python_plugin_engine.cpp` | RAII `GilGuard`（`PyGILState_Ensure/Release`）包裹全部公开入口与析构的 `Py_XDECREF` | ✅ `935abf0` |
| 2 | `popen("ros2 ...")` 命令注入 + 子进程违规 | P0 | `src/ros_manager.cpp`, `CMakeLists.txt` | 原生 rcl C API 客户端：introspection 结构体构建/读取（JSON↔结构体）+ `rcl_send_request/take_response`；接口列表/详情改 `ament_index_cpp`+filesystem。*注：Humble 无 `rclcpp::GenericClient`（已核实头文件），采用 rclpy 同款结构体方案* | ✅ `f831d16` + `535c4c1` |
| 5 | `dlopen` 句柄泄漏 + 裸 `.detach()` 线程 | P0 | `src/ros_manager.cpp` | `cleanup_typesupport_caches()`（析构时 dlclose）；`call_service_async` 改为 Impl 内任务队列 + 单 worker（stop 时 join，含安全网） | ✅ `d73f39f` + `ca09446` + `69bfa98` |
| 8 | Service/Action 请求 JSON stub + TF 时间戳硬编码 0 | P1 | `src/ros_manager.cpp`, `src/tf_tree.cpp` | introspection 生成默认 JSON 模板（service request / action goal）；`update_transform` 传入真实 `header.stamp` | ✅ `3509338` + `4079206` |
| 7 | TF 重亲和后旧父节点残留子帧 | P1 | `src/tf_tree.cpp` | `update_transform` 重亲和时先擦除旧父节点 `children` 中的子帧（含空 parent_id/父帧缺失守卫） | ✅ `6d97260` |
| 6 | Service/Action/Interface/TF 选项卡 mock 数据 | P1 | `src/app.cpp`, `app.hpp` | 全部接入真实 ROS 2：service 调用（模板预填+异步回调）、action 发目标（复用动态客户端经 send_goal 服务）、interface 树/详情（三栏）、TF 实时树（`TFTree::snapshot()` 深拷贝渲染） | ✅ `06c3003` + `d435c42` |
| 9 | 事件循环硬编码按键 | P2 | `src/app.cpp`, `config_loader.hpp` | 按键改读 `config_.keybindings`（`key_is` 匹配，空绑定禁用）；帮助/底部栏/About 显示实际绑定；修复 3-pane 焦点切换；清除 'c'/'g' 残留 mock | ✅ `005a017` + `e3a538c` |
| 10 | CDR 解析假设主机字节序 | P2 | `src/ros_manager.cpp`, `src/cdr_utils.hpp` | 校验 4 字节封装头 byte1 endian 标志，流序与主机不一致时逐字段字节交换，修正有效负载 4 字节对齐 | ✅ `2a82aec` + `67cb9e1` |
| 11 | JSON 输入框多行换行支持 | P2 | `src/app.cpp` | 拦截 `Alt+Enter` / `Ctrl+Enter` 并在光标位置插入换行符 `\n`，`Enter` 保留用于直接提交 | ✅ `2b05009` |
| 12 | Action 类型符号规范化 | P2 | `src/ros_manager.cpp` | 规范化 action 类型名称解析，解决 `_SendGoal_Goal` typesupport 加载失败问题 | ✅ `88d2d1c` |
| 13 | 快捷键精简与层级式导航 | P2 | `src/app.cpp`, `config_loader.hpp` | 移除 `w/r/e/c/g/j/k/q/?` 默认单字符快捷键；支持 Top Bar 与内容区垂直上下导航；支持 Esc 逐级回退及 Top Bar 退出确认弹窗 | ✅ `41a1f67` + `8cb38be` + `e1a261b` + `ef89ba1` |
| 14 | CatchEvent 隔离 Event::Custom 重绘 | P1 | `src/app.cpp` | 在 `CatchEvent` 最顶端放行 `Event::Custom`，防止高频 Topic 刷新意外关闭退出弹窗 | ✅ `3b17cb8` |
| 15 | FTXUIConverter paragraph CJK 自动换行 | P1 | `src/ftxui_converter.cpp` | 基于 flexbox(gap=0) 实现 CJK/全角汉字级别换行与 ASCII 单词边界换行，解决长语音字幕溢出 | ✅ `cc1a00d` |
| 16 | 插件分类目录体系与递归发现 | P2 | `src/python_plugin_engine.cpp` | `load_plugins_from_dir` 升级为 `recursive_directory_iterator`；建立 `speech/`、`teleop/`、`diagnostics/` 分类目录及 PEP 257 规范 | ✅ `833eaed` |
| 新增 | 自动化测试套件扩展 | P2 | `CMakeLists.txt`, `test/` | 6 个 `ament_add_gtest` 目标（TFTree / ConfigLoader / FTXUIConverter / cdr_utils / python_plugin_engine / app），23 用例，`ctest` 100% 通过 | ✅ `4043544` + `72b8bbf` + `663a8c0` + `340421b` |

**验证基线**：`make -C build lazyrtui` 零警告；`ctest` 6/6 目标（23 用例）通过；工作树干净；每 commit 均经独立审查（`fix-review:` 承载整改）。

---

## 关键实现决策（备注）

- **Issue #2 原生客户端**：Humble 无 `rclcpp::GenericClient`、无序列化服务客户端（`rcl_send_request` 接收类型擦除结构体，已核实）→ introspection 驱动结构体构建（dlopen introspection typesupport；`init_function(ALL)` 构造、`fini_function` 清理、序列经 `resize/get/assign/fetch` 函数指针，含 `std::vector<bool>` 空指针回退）。
- **Issue #3B 快照机制**：写线程在 `data_mutex_` 下发布不可变 `UiSnapshot`（`std::atomic_store`）；Menu 经 `ConstStringListRef::Adapter` 读快照（string_view 生命周期经 scratch 拷贝解决）。
- **Issue #8 action 内省**：Humble 无 action 内省类型 → 复用 `pkg/action/Name_Goal` 消息；typesupport 符号 kind 感知（`__action__` 段 + `_Service` 后缀剥离，nm 验证）。
- **Issue #10 字节序**：swap 逻辑提取至 `src/cdr_utils.hpp` 内部头，可单元测试。
- **依赖评估**：曾评估官方 dynmsg 替代手写 introspection —— 原仓库已消失（404），继任者 `rosidl_dynamic_typesupport` 仅 Jazzy+/Rolling 可用（Humble rosdistro 无此包、无 humble 分支），且机制不同（FastRTPS 动态类型）；Humble 的 rosbag2 亦采用 introspection typesupport —— **维持现状**。

---

## 后续跟进项（未解决，按优先级）

- [ ] **全 parser 级 CDR 字节序往返测试**：链接真实生成消息（如 `std_msgs/msg/Int32MultiArray`），翻转封装头字节后断言输出一致（Issue #10 审查建议，可同时覆盖有界序列回归）
- [ ] **action 反馈流式显示**：当前 Send Goal 仅展示 accepted 状态；反馈/结果需订阅 feedback topic / 调用 GetResult 服务
- [ ] **interfaces 树发布粒度**：`publish_snapshot` 每消息回调全量拷贝 interfaces 树（Issue #3B 备注），可改为仅在刷新周期发布
- [ ] **`update_transform` 环检测**：当前允许 A→B→A 成环（Issue #7 审查备注）
- [ ] **`loaded_plugins()` getter 保护**：无 GIL/互斥，跨线程读取有竞态风险（Issue #1 备注）
- [ ] **`""` 根帧显示二义性**：TF 根帧以 `header.frame_id == ""` 发布时可能在树中重复显示（Issue #12 备注）
- [ ] **迁移 Jazzy+ 时评估 `rosidl_dynamic_typesupport`**：届时可替代手写 introspection（dynmsg 继任者，Jazzy/Rolling 已发布）--低优先级
