# 角色拼合 Demo

直接从 Unity bundle 重建 Naninovel `LayeredCharacter` 的静态外观，不依赖原游戏运行时。

## 用法

```sh
/home/chiyuki/manosaba-gba/python_workspace/.venv/bin/python \
  character_composition_demo/compose.py hiro --ppu 50
```

输出：

| 文件 | 说明 |
| --- | --- |
| `output/hiro_default_white.png` | 完整拼合示例图（白底） |
| `output/hiro_default.png` | 完整拼合结果（透明底，已裁切到内容边界） |
| `output/hiro_default.json` | 画布、裁切范围、实际绘制图层及材质信息 |


其它参数：

```sh
# 列出所有分层角色及其默认外观
... compose.py --list

# 查看某个角色的 compositionMap
... compose.py --list-appearances hiro

# 指定外观，例如换头型和表情
... compose.py hiro --appearance 'Arms1,Smile2'

# 保留完整 15x30 画布，不裁切
... compose.py hiro --keep-canvas --ppu 50
```

`--ppu` 是渲染分辨率（每世界单位多少像素）。角色 sprite 的 `pixelsPerUnit` 为 100，因此
`--ppu 50` 相当于原画的一半分辨率，Hiro 画布为 750x1500，裁切后为 444x1188。

## 组合语法

角色 bundle 里的 `LayeredActorBehaviour.compositionMap` 是 `键 -> 表达式` 的映射，
`defaultAppearance` 是默认外观。表达式按 `,` 分割，每一项：

| 表达式 | 含义 |
| --- | --- |
| `group>layer` | 选中：启用目标层，禁用该组内其它所有层 |
| `group+layer` | 启用目标层，不影响同组其它层 |
| `group-layer` | 禁用目标层 |
| `group+` / `group-` | 启用 / 禁用组内所有层（含嵌套子组） |
| `group>` | 选中整个组：启用组内所有层，并禁用同级邻居组 |

规则：

- 组路径用 `/` 分隔，例如 `Angle01/Head01/Facial01/Eyes01`。
- 键优先于表达式解析；键可以递归展开，例如
  `Default -> Normal1 -> Head1, ...`。
- 不含 `>`、`+`、`-` 的裸表达式不是合法组合语法，原版 Naninovel 只打日志并忽略。
- 组操作是递归的：`Angle01/Head01-` 会禁用 `Head01` 下的全部层（包括
  `Facial01/Eyes01` 等子组），而不只是直接子层。
- `group>layer` 的“禁用其它层”同样作用于该组子树。

Hiro 默认外观 `Arms1,Default` 的展开结果：

```text
Arms1
  Angle01/Arms-
  Angle01/ArmR>ArmR01
  Angle01/ArmL>ArmL01
Default
  Normal1
    Head1
      Angle01/Head02-
      Angle01/Head01+HairClippingMask01
      Angle01/Head01+HeadBase01
      Angle01/OptionB>OptionB_Head01
    Pale1-Off          -> Angle01/Head01/Facial01/Pale01-
    Sweat1-Off         -> Angle01/Head01/Facial01/Sweat01-
    Eyes1_Normal_Open1 -> Angle01/Head01/Facial01/Eyes01>Eyes01_Normal_Open
    Mouth1_Normal_Closed1 -> .../Mouth01>Mouth01_Normal_Closed
    Cheeks1-Normal     -> .../Cheeks01>Cheeks01_Normal
```

最终实际绘制 11 层（含两个混合效果层）。

## 渲染规则

1. **画布**：根 GameObject 上的 `RenderCanvas.Size/Offset` 决定世界坐标范围；
   `Hiro` 根节点在 `(0,15)`，画布为 `15x30`。
2. **变换**：逐级累乘 TRS（本项目全部为 Z 轴无旋转、pivot 0.5）。
3. **画序**：按 `SpriteRenderer.m_SortingOrder` 升序；相同值时按层级顺序。
4. **Sprite**：世界尺寸为 `m_Rect / pixelsPerUnit`（100）；`Sprite.image` 已是
   裁切后的子图。渲染时按 `--ppu` 缩放。
5. **染色**：`SpriteRenderer.m_Color` 会同时乘到 RGB 与 A 上（例如
   `Blending01` 的红色 tint 和 0.33 alpha）。
6. **混合**：
   - `Naninovel_Default`：普通 alpha 混合。
   - `Naninovel_Multiply`：`rgb = dst * lerp(1, src, src.a)`，
     `alpha = dst.a * max(src.a, 1-src.a)`。
   - `Naninovel_Overlay` / `Naninovel_Softlight`：原 shader 没有 GrabPass，
     无法读取帧缓冲，这里退化为普通 alpha 混合。这两层 alpha 很低，影响很小。
7. **Stencil 遮罩**：
   - `_StencilOp=2` 且 `_StencilRef=N` 的材质是写入者：按
     `alpha >= _Cutoff*255` 写入二值覆盖。
   - `_StencilComp=4` 且 `_StencilRef=N` 的材质是读取者：用该 ref 的遮罩乘 alpha。
   - 遮罩按参考值存放在完整画布分辨率上；多个写入者取并集（`lighter`），
     避免后画的透明区域把先前覆盖擦掉。
   - Hiro 的 `Ref1` 写入者为 `Body`、`HeadBase01`、`OptionB_Head01`，读取者为
     `Blending01/02`；`Ref2` 写入者为眼睛，读取者为 `HairClippingMask01/02`。

## 适用范围与限制

- 只处理带 `LayeredActorBehaviour` 的分层角色 bundle：
  `alisa`, `anan`, `coco`, `ema`, `hanna`, `hiro`, `leia`, `margo`, `meruru`,
  `miria`, `nanoka`, `noah`, `sherry`。
- 只生成静态帧；不处理外观过渡、动画、说话口型等运行时行为。
- 不支持 camera 渲染模式（这些角色使用默认 sprite 渲染模式）。
- 输出默认裁切到 alpha 边界；需要完整画布时加 `--keep-canvas`。

## 交叉验证

- Hiro 默认外观：11 层，输出 444x1188，画布 bbox `[209,156,653,1344]`。
- Ema 默认外观：9 层，渲染结果与之前的手工验证图一致。
- `Arms1,Smile2`：可正确从 `Head1` 切换到 `Head2`，说明递归禁用了
  `Head01` 下的旧表情层，没有残留叠脸。
