# GBA 工程

从准备好的资产编译：`make release -j12`；开发版：`make -j12`。首次使用先运行 `make setup music-tools`，配置 GBA 工具链。

- [完整管线与可编辑输入](../docs/pipeline.md)
- [播放器、图形、音频和 Flash 状态](../docs/architecture.md)
- [测试与已知限制](../docs/testing.md)

生成的 C/C++／字库、`.incbin` 汇编与 `assets` 二进制需一起保留。普通构建不打开原作、Unity、图形缓存或诊断报告。需要改原始素材时，显式执行 `make prepare-assets`。
