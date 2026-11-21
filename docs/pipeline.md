# 资产与构建管线

## 两个边界

```text
正版原作 + 提取代码 + 中文校订 / UI / 选色 / 保护区 / 音效调整
    │  make -C gba prepare-assets
    ▼
准备好的 GBA 资产（src/*_data.cpp、字库、assets/*.bin、必要目录）
    + 外部批准音乐 gba/audio/*.xm
    │  make -C gba release
    ▼
32 MiB 以内的正式 ROM
```

报告和设计截图只写到 `gba/build-generated/`，模拟器输出写到指定测试目录；两者都不是原料。`assets/graphics-cache/` 是按内容寻址的加速缓存，不纳入版本控制，也不是第二阶段的依赖。冷准备可重新生成；热准备复用相同源数据／参数／算法的结果。

## A：原资产到 GBA 资产

只有改原作素材、剧本或转换规则时才运行这一阶段。将游戏安装放在 `original_game/`，初始化 `manosaba-web` 子模块取得当前 UI 源图。Python 环境由 uv 管理：

```sh
uv sync --project python_workspace
uv sync --project sfx
git submodule update --init manosaba-web
```

`manosaba-web` 作为固定提交的 Git 子模块保留，本次使用 `da31228ac5531e85afa298e58f4a49224074c289`。原作提取器和拼合代码保留在 `python_workspace/`、`character_composition_demo/compose.py`；IL2CPP 命令元数据保留在 `python_workspace/il2cpp_output/dump.cs`。

原版或译文发生变化时，按需要执行提取（不要无故覆盖已校订剧本）：

```sh
uv run --project python_workspace python python_workspace/export_naninovel.py
uv run --project python_workspace python python_workspace/export_story.py
uv run --project python_workspace python python_workspace/export_images.py
uv run --project python_workspace python python_workspace/export_audio.py
make -C gba prepare-assets
```

`export_images.py` 的 `original_sprites/manifest.csv` 与 `export_audio.py` 的 `python_workspace/audio_manifest.json` 是实际素材映射目录；它们不是统计报告。舞台和角色导入按 Unity prefab／compositionMap 重建。法庭由 `court_render.py` 离线渲染，最终背景与人物仍由 GBA 播放器分层显示。

`prepare-assets` 先导入和链接全章图形，再生成法庭、UI、图鉴、剧本和裁剪字库。选择的审判姿态会补入同一图像归档。输出的 C/C++ 描述、汇编 `.incbin` 和二进制共同构成准备好的 GBA 资产，需一起保留；无需把大量原始 PNG 再交给 ROM 构建。

### 人工调整在哪里

| 输入 | 用途 |
| --- | --- |
| `original_scripts/zh-Hans/`、`story/zh-Hans/` | 按文本 ID 同步校字，参考 typo-corrections.md |
| `assets/graphical-story.json` | 全章选择、图像与 ROM 预算 |
| `assets/text-layout.json` | 对话区域、拆行策略及强调短句宽度阈值 |
| `assets/nameplates.json`、`trial-ui-palette.json` | 姓名、代表色与 UI 色槽 |
| `assets/cg-detail-protection.json` | 已批准的嘴／眼等小区域，编码时精确保留 |
| `assets/reenact-layout.json` | 小剧场统一裁剪、定姿与移动降级 |
| `assets/ending-roll-titles-zh.json` | 片尾标题／角色名汉化 |
| `sfx/recipes/clips.json`、`footsteps.json` | 明确纳入的原始音效和批准脚步配方 |
| `sfx/samples/` | 已批准、已调整的短循环与共用音色原料 |

表内 `assets/` 均相对 `gba/`。音效生成器直接裁切数字静音，不读取裁切报告。可选音效研究工具只产出试听文件，试听批准后才将所选样本更新到 `sfx/samples/`。

### 准备产物在哪里

- `gba/src/`：剧情字节码、裁剪字库、图形／舞台／法庭／UI 描述。
- `gba/assets/{graphics,trial,reenact,home,...}/`：打包二进制与必要的逻辑目录。
- `gba/audio/sfx/`：已转为 GBA 播放规格的音效样本。
- `gba/assets/graphics/catalog.json`：正式资源目录及容量信息。
- `gba/assets/source-metadata/`：提取后的台词时长、角色／图鉴主数据及地图逻辑，不需要重新打开 Unity。
- `gba/assets/trial/{poses,camera}.json`：姿态映射与镜位目录，供编译器及对应行为测试使用。

`make -C gba prepared-assets` 只检查这些准备产物是否齐全，不访问原作或缓存。

## B：准备资产到游戏

需要 GNU Make、Python 3、宿主 C 编译器及 GBA C/C++ 工具链。当前固定 Butano 21.10.0；`setup_butano.py` 检查固定提交，`setup_mmutil.py` 构建固定版本转换器。Wonderful 在 `/opt/wonderful` 时自动识别；也可设置 `DEVKITARM` 或 `WONDERFUL_TOOLCHAIN`，外部 Butano 用 `BUTANO=/path/to/butano/butano`。

```sh
make -C gba setup
make -C gba music-tools
make -C gba release -j12
```

`release` 使用 `DEBUG_MENU=0`，输出 `gba/manosaba-release.gba/.elf`；开发版直接 `make -C gba -j12`。两者分别使用 `build-release/` 和 `build-butano/`，不会复用不同配置的音频数组。`make clean` 只清编译产物，保留准备好的 GBA 资产。构建完成检查真实 ROM 以及图像、音频预留预算。

普通 `make` 不自动进入 A 阶段。更新剧本或原图后明确执行 `prepare-assets`，避免发布构建偷偷依赖本机的原作或历史报告。

## 音乐是外部输入

批准的 XM 直接放在 `gba/audio/`。本仓库不从 MIDI、原 OST、交接包或音乐工作树重新编曲。保留的单独 Intro 和合并前奏模块同样是批准输入，不在游戏构建时拼装。

- `assets/music/runtime.json`：资源名、逻辑别名／版本、音量、首播与循环入口和跨编号前奏映射。
- `assets/music/catalog.json`：调试试听目录及当前版本说明。
- `assets/music/room-titles.tsv`：用户编辑的正式音乐馆标题；047 隐藏。
- `tools/music_cues.py`：读取上述配置和 XM 头，生成 C++ 映射；可选 `--audit DIR` 额外输出源剧本覆盖报告。

mmutil 统一打包并去重音乐与音效采样，不能并发运行多个共用临时目录的转换器。音乐不保存播放坐标；读档从逻辑曲目入口起播。

## 可丢弃的目录

`gba/build*/`、`__pycache__/`、各 Python 虚拟环境、试听和测试输出都可重建。`assets/graphics-cache/` 可删，但会让下次 A 阶段重做图像处理。已淘汰的原型、交接文档及历史输出先隔离，经过资产重生成、独立目录编译和测试验证后统一删除。

