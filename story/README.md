# 中文剧本与编译器

`zh-Hans/` 保留按原作文本 ID 合并的 440 份中文剧本。故事、坏结局、审判及所需系统后续由 `gba/tools/story.py` 编译；部分系统子程序在导入时展开。

普通 ROM 构建直接使用已准备的字节码，不重新提取／合并中文，也不打开 Unity。原资产或翻译改变时：

```sh
uv run --project python_workspace python python_workspace/export_story.py
make -C gba prepare-assets
make -C gba release -j12
```

重新提取会覆盖合并 JSON。校字必须按文本 ID 同步原中文源文、JSON translations 和 events 中的值，参考 [校字记录](typo-corrections.md)。实际输入／输出边界见 [管线](../docs/pipeline.md)。

## 可读源格式 v1

每个文件使用 UTF-8，包含 `format: "manosaba.story"`、`version: 1`、`language: "zh-Hans"`、原始 `path`、有序 `events` 和完整 `translations`。

```json
{
  "format": "manosaba.story",
  "version": 1,
  "language": "zh-Hans",
  "path": "Act01_Chapter01/Act01_Chapter01_Adv03",
  "events": [
    {
      "op": "PrintTextModified",
      "line": 43,
      "indent": 0,
      "params": {"Text": "等、等我一下，希罗！", "AuthorId": "Ema"},
      "text_ids": {"Text": ["0101Adv03_Ema001"]}
    }
  ],
  "translations": {"0101Adv03_Ema001": "等、等我一下，希罗！"}
}
```

`op` 使用原命令类名；`line` 是原始行号，`indent` 保留 Naninovel 的块结构。标签保存为 `op: "label"` 和 `name`，不再与注释混淆。只写具有值的参数；命名路径转成 `脚本.标签`，数组保持数组，表达式保留为 `{"expression": "{变量}"}`。`text_ids` 保留每个本地化参数的原始 ID，因此重复台词和选择文本仍可追溯。

该层保留人物完整差分、音效、动画、等待、系统调用及审判命令的参数，方便后续实现。注释和空行不占事件。ROM 编译阶段再裁剪本阶段未使用的数据。

## ROM 格式 v1

格式声明在 `gba/src/story_format.h`，生成器为 `gba/tools/story.py`。ROM 直接链接只读目录和压缩字节数组，不加载 JSON，不在 GBA 上解析表达式。可选编译统计写到 `gba/build-generated/reports/story.json`，不作为输入。

- 脚本目录：原路径、调试标题、解压后字节数、首块编号、块数。GBA 上每项 16 字节。
- 块目录：`u32 offset, u16 stored_size, u16 codec`，每项 8 字节。
- 每份脚本按解压后的逻辑位置划成 4096 字节独立块，最后一块长度由脚本大小推导。只缓存当前块，跨块读取文本和指令也有效。
- `codec=0` 直接复制原始块；`codec=1` 使用 LZSS。压缩后更大时自动回退为原始块。将来可增加 codec 而不改变分支地址。
- LZSS 每组 8 个 token 由一个标志字节控制，先读最高位；0 为一个字面字节，1 为大端双字节 `(length-3)<<12 | (distance-1)`。长度 3–18，距离 1–4096，允许重叠复制。块之间无字典依赖。
- 指令为小端 TLV：`u8 opcode, u16 payload_bytes, payload`。未知 opcode 可按长度跳过。跳转用 `u16 script_id, u32 logical_pc`，与压缩位置无关，支持随机选段和局部跳转。
- 文本为以 NUL 结束的 UTF-8，保留中文原字；仅当原行超过 GBA 行宽时拆掉后续软换行，再按 GBA 文本框重排；短行保留原换行。播放器按 UTF-8 字符渐显；原作彩色片段另存紧凑样式表；注音未移植。

| Opcode | 指令 | Payload |
| --- | --- | --- |
| 1 | say | `u8 speaker_id, text` |
| 2 | jump | `u16 script_id, u32 pc` |
| 3 | branch | `u8 condition, u16 operand, u32 false_pc` |
| 4 | menu | `u8 kind, u8 count` 后接选项 |
| 5 | flags | `u16 area_flags` |
| 6 | flag | `u8 bit`，设置已访问地点 |
| 7 / 8 / 9 | backdrop / character / music | 占位资源名 |
| 10 / 11 | pause / end | 提示文本 |
| 12 | visual | `u16 frame_id`，Adv04 的纯图形画面编号；台词与分页继续使用 say |

`branch` 条件 0 为常量、1 为地图标志相等、2 为地点 ID 相等、3 为调试直接进入。编译器将原版故事中现有的条件和缩进块展开为跳转；语言分支按简体中文处理。普通选项包含 `u8 reserved, u16 script_id, u32 pc, text`；地图选项包含 `u8 flag_bit, u8 area_id, text`，选择后由后续分支设置访问标志并跳转，返回地图时隐藏已访问地点。当前上限为 8 项、每项 160 字节，台词上限 384 字节，资源名上限 96 字节，编译时校验，不在有限的 GBA 上重复检查可信的内置数据。

当前逻辑流和压缩字节数见 `gba/story_report.json`；除台词重排结果外还包含 Adv04 的 244 个 visual 指令。另外有目录和调试名字。解释器及播放状态约 6 KiB EWRAM，其中解压缓存 4 KiB；不需要整份剧本缓冲或动态分配。TLV、UTF-8 与 codec 字段为后续新增命令、字形编号文本和更强压缩留出空间。

## 播放与边界

台词先由 `gba/tools/story_layout.py` 合并原作按大屏宽度设置的软换行，再按 GBA 文本框宽度排版，保留真正的空段落，并处理中文标点和英文单词边界。`gba/assets/text-layout.json` 同时驱动 ROM 文本编译和运行时布局，普通 `make` 会自动重排并更新字库；`make -C gba reflow` 生成带原始文本 ID 的可读预览，不改写中文源 JSON。使用方式见 [重排说明](../gba/README.md#剧本重排)。

调试菜单进入“剧本”，上下选择、左右翻页、A 开始播放。台词逐字渐显，渐显中 A 只补全本页；松开后再按 A 才翻页或继续。长按不连跳，加载期间输入不排队。上下选择、A 确认选项；Select 返回剧本列表。Start/B 打开调试菜单并暂停，返回时保留当前页、渐显位置和选项；在菜单重新进入“剧本”则重置选择器。L/R 沿用调试画面的前后跳转。

支持旁白、人物名、自动换行和完整分页、本段/跨脚本跳转、普通选择、坏结局，以及地图调查、标志记录和回访过滤。故事页面 `story_scene` 与调试菜单独立，图形层接收 VM 的 visual 编号，不另存台词或执行一套播放流程。

`Act01_Chapter01_Adv04` 的 244 条台词已接入真实背景、CG、角色差分和原作风格 UI，可从故事列表或“图像与动画”入口播放；详见 [GBA demo](../gba/README.md#adv04-图形-demo)。切换脚本时清除 visual 状态并回到现有文字页面。其他脚本仍使用人物/背景/BGM 资源名占位；当前声音和原版完整动画时序尚未接入，背包/档案更新、进度记录、QuickSave 等系统命令暂不执行。

处刑按钮等 `Stop` 事件、动画和片尾用按 A 继续的占位提示接续；坏结局显示编号。故事尝试进入审判脚本时显示“审判部分尚未实现”及目标脚本，A 返回列表，不执行审判。尚无审判后的自动续播，需要从调试列表选择相应故事。正式 UI、存档和原版完整演出均留在后续阶段。

## 验证

`make -C gba story-test` 用独立的 JSON 源解释器产生期望事件序列，和实际 C++ 解压器/VM 的结果逐流校验。覆盖 230 个入口，分别优先首项和末项选择、正反地图访问顺序，共 460 条完整流程；主机运行启用 AddressSanitizer 与 UBSan。编译时还逐块验证压缩往返一致，并拒绝未支持的故事控制命令、未解析跳转或超长文本。

`make -C gba smoke` 在 mGBA 中运行实际 ROM，检查原调试菜单、剧本列表翻页/循环、中文推进、返回恢复、跨脚本播放、普通选择和八地点地图的选项分页，截图位于 `gba/build-butano/smoke/`。
