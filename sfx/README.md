# 音效原料

`recipes/clips.json` 是批准纳入的原始音效列表；`recipes/footsteps.json` 是脚步合成配方；`samples/` 是已调整的共用短循环、脚步原子和钟声音色。不要用研究统计报告代替这些输入。

`gba/tools/sfx_assets.py` 读取它们及 `python_workspace/audio_manifest.json` 对应的原始 WAV，调用 `tools/trim_sfx_silence.py` 去掉尾部数字静音，生成 `gba/audio/sfx/`、C++ 目录和播放 manifest。日常 ROM 构建直接使用准备好的产物。

```sh
uv sync --project sfx
make -C gba sfx-assets
```

可选声音处理脚本保留在 `tools/`，试听输出在 `gba/build-generated/sfx/`；批准后手工更新 `samples/`，再生成 GBA 资产。详细两阶段边界见 [管线](../docs/pipeline.md)。
