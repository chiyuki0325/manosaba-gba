# 魔法少女的魔女审判 · GBA 同人移植

默认Flash版需要支持32 MiB卡带和Flash128K存档的GBA模拟器或烧录卡；另有32 KiB SRAM兼容版。RTC可选，没有RTC仍可存读档。

mGBA 0.10.5 默认不会为本游戏启用RTC。需要在游戏覆盖（Game Overrides）中启用实时时钟（Realtime clock），应用并重新载入ROM后再保存。存档卡片显示保存时的现实日期和时间；没有有效时间的记录显示“未记录时间”，不以游玩时长代替。

A补全／推进，Select图鉴，L对话记录，Start暂停与存读档。审判讨论使用上／左／右进入异议点，气泡内上下选择、A确认。

已知限制：部分资源切换会有长帧，图鉴四项交互仍略超响应预算。复杂演出按GBA机能降级。

本作为《魔法少女的魔女审判》非官方同人移植，与原作方无关。文字、图像和音乐等素材的相关权利归Acacia / Re,AER、CIRCLE LINE GAMES等权利人所有。本移植仅供研究GBA开发和学习交流使用。请支持并购买正版游戏。


离线解锁全部CG和音乐馆（Python 3，无额外依赖）：

```sh
python3 gba/tools/unlock_collections.py manosaba.sav
```

输出`manosaba.unlocked.sav`，原文件不变；也可用`-o 输出.sav`指定新路径。关闭模拟器后，将输出文件改成游戏对应的存档文件名使用。支持Flash128K及其附加RTC数据；只修改收集标志与校验和，保留剧情槽位、设置和时间戳。


SRAM兼容版：8个手动槽＋自动档＋重试档；Flash版为27个手动槽。两版存档容量不同，换版前备份原文件。打开[存档转换工具](https://imgsrc.chyk.ink/manosaba-gba-save-converter.html)，也可将[单文件HTML](../gba/tools/save-converter.html)保存到本机后用浏览器打开，选择 `.sav/.srm`，自动识别来源，再选择输出类型。转SRAM时选定8个手动槽；转Flash时保留原有8槽，其余槽为空。转换页不联网、不覆盖原文件，自动档、重试档、收集和日期时间一起迁移。支持电脑／手机，输出保持输入扩展名；关闭模拟器后将结果改名为目标ROM所用存档名。解锁工具也支持SRAM存档及libretro的128 KiB填充形式。

SRAM版已在mGBA和桌面gpSP核心核对新建保存及冷读档；PSP版gpSP和实际烧录卡尚未实测，不能据此承诺所有设备通过。
