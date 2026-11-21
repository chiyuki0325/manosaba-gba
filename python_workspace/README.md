# Naninovel extraction workspace

The Python environment is managed by uv (`unitypy==1.25.4`). Generated IL2CPP metadata is in `il2cpp_output/`; it supplies command aliases, parameter aliases, and defaults needed when rebuilding the Japanese scripts.

Regenerate both available languages from the default game path:

```sh
uv run --project python_workspace python -X faulthandler python_workspace/export_naninovel.py
```

Output goes to `../original_scripts/`:

- `ja/`: executable `.nani` scenarios reconstructed from Naninovel Script assets.
- `zh-Hans/`: the game's original Naninovel localization documents. They contain Chinese translations, not standalone executable scenarios.

This game version ships only Japanese and Simplified Chinese. Optional extraction diagnostics go to `../gba/build-generated/reports/extraction/`; they are not build inputs.

中文剧本合并：`uv run python export_story.py` 从原版命令元数据与中文翻译按文本 ID 生成 `../story/zh-Hans/` 的 440 份中文 JSON。准备阶段读取 JSON；普通 ROM 构建直接读取已生成字节码。格式与播放器说明见 [story/README.md](../story/README.md)。
