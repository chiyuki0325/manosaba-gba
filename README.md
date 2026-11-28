# 魔法少女的魔女审判 GBA 移植

基于 Steam 简体中文版进行移植。不包含 NS 版新增内容。

<img src="output/packaging/final/product-mockup-v2.webp" width="100%">

<img src="docs/screenshots/title.png" width="49%"> <img src="docs/screenshots/adv04-not-evil.png" width="49%">

<img src="docs/screenshots/trial-sleep.png" width="49%"> <img src="docs/screenshots/trial-discussion.png" width="49%">

## 操作

| 画面 | 操作 |
| --- | --- |
| 普通对话 | A 推进剧情；L 对话历史回顾 |
| 故事／审判 | Select 魔女图鉴；Start 打开菜单 |
| 审判讨论 | A 推进当前句；上／左／右 进行异议 |
| 快进 | 故事、审判对话及讨论中长按 R 即时显示，每 0.8 秒前进一句 |
| 自动播放 | 故事、审判对话中，0.8 秒内按下并松开 R 两次；再次按 R、手动打开菜单或进入讨论时取消 |

自动播放时右上角显示单箭头，每 1.6 秒翻页或推进。同一句对话的第二页如果只有一行，只停留 0.8 秒。

## 编译

```sh
make -C gba setup            # 配置 Butano
make -C gba music-tools      # 首次安装本地 mmutil
make -C gba release -j12     # 正式版：gba/manosaba-release.gba
make -C gba -j12             # 开发版：gba/manosaba.gba
make -C gba release SAVE_BACKEND=sram -j12  # SRAM兼容正式版：gba/manosaba-release-sram.gba
```

需要 Python 3、Make、宿主 C/C++ 编译器与 Wonderful GBA Toolchain 或 devkitARM。

开发版从暂停菜单的设置选项里进入调试菜单。

仓库中包含转换好的一切资产，如果需要从原作提取资产，见[资产管线](docs/pipeline.md)。

## 使用的 AI

开发：GPT-6.1 Sol（共使用了 ~2200M tokens，订阅成本 ~175 元）

逆向：DeepSeek V4.1 Flash（成本 ~20 元）

扒谱：[MuScriptor](https://muscriptor.kyutai.org)

## 版权声明

一切原作素材权利归 Acacia / Re,AER、CIRCLE LINE GAMES 等权利人所有，请支持正版游戏。

部分音乐基于网上的琴谱制作。来自 bilibili [wzd啊](https://space.bilibili.com/1489276507) 和 MuseScore [AlexChen233](https://musescore.com/user/33942334)。

## 维护文档

在仓库根目录运行以下命令格式化手写 C/C++ 源码与测试（需安装 `uv`）：

```sh
git ls-files -z --cached --others --exclude-standard -- '*.c' '*.cpp' '*.h' '*.hpp' |
  xargs -0 -r uv tool run --from clang-format==21.1.8 clang-format -i
```

只检查格式时，将 `-i` 换成 `--dry-run --Werror`。生成资源和固定数据表由 `.clang-format-ignore` 排除。

- [管线、准备资产与外部音乐输入](docs/pipeline.md)
- [架构、状态和存档](docs/architecture.md)
- [测试入口与精简审计](docs/testing.md)
- [已知问题与最近一次全量验证](docs/known-issues.md)
- [中文剧本格式](story/README.md) · [校字记录](story/typo-corrections.md)

1.1.5 发布 Flash ROM 为 30,044,796 B，距离 32 MiB 上限还剩 3,509,636 B。原作安装、完整解包媒体、开发环境、缓存和测试输出不纳入版本控制。

Flash版提供27个手动槽，SRAM兼容版提供8个手动槽；两版另有自动档和重试。正文固定1254B，详见[存档容量](docs/save-capacity.md)。使用[存档转换工具](https://imgsrc.chyk.ink/manosaba-gba-save-converter.html)在两版之间迁移，支持电脑／手机及.sav／.srm。
