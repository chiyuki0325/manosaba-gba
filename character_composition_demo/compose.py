#!/usr/bin/env python3
"""Render a static frame of a Naninovel layered character from its Unity bundle.

Composition expressions, sorting order, tinting, multiply blending and the
stencil-based clipping masks used by the original materials are reproduced.
See README.md for the rules and for the two approximations (Overlay/Softlight).
"""
from __future__ import annotations

import argparse
import json
import math
import re
import sys
from dataclasses import dataclass
from pathlib import Path

import UnityPy
from PIL import Image, ImageChops

ROOT = Path(__file__).resolve().parents[1]
BUNDLE_DIR = (
    ROOT
    / "original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64"
    / "naninovel-characters_assets_naninovel/characters"
)
OUTPUT_DIR = Path(__file__).resolve().parent / "output"
OPS = ">+-"
WHITE = (255, 255, 255)


@dataclass
class Layer:
    name: str
    path: tuple[str, ...]
    tr_id: int
    renderer: object
    sprite: object
    material: object | None
    order: int
    enabled: bool
    world: tuple[float, float, float, float, float, float] = (1, 0, 0, 1, 0, 0)

    @property
    def group(self):
        return "/".join(self.path[:-1])


@dataclass
class MaterialInfo:
    stencil_ref: int
    stencil_comp: int
    stencil_op: int
    cutoff: float
    blend: str


def deref(objects, pptr):
    return objects.get(pptr.m_PathID) if pptr and pptr.m_PathID else None


def component(objects, game_object, type_name):
    for entry in game_object.m_Component:
        obj = deref(objects, entry.component)
        if obj is not None and obj.type.name == type_name:
            return obj.read()
    return None


def load_scene(character):
    path = BUNDLE_DIR / f"{character}.bundle"
    if not path.exists():
        raise SystemExit(f"character bundle not found: {path}")

    env = UnityPy.load(str(path))
    objects = {obj.path_id: obj for obj in env.objects}

    main = canvas = None
    for obj in env.objects:
        if obj.type.name != "MonoBehaviour":
            continue
        data = obj.read()
        if "compositionMap" in data.__dict__:
            main = data
        if "Size" in data.__dict__ and "Offset" in data.__dict__:
            canvas = data
    if main is None or canvas is None:
        raise SystemExit(f"{path.name} is not a layered character bundle")

    game_objects = {
        obj.path_id: obj.read() for obj in env.objects if obj.type.name == "GameObject"
    }
    transforms = {
        obj.path_id: obj.read() for obj in env.objects if obj.type.name == "Transform"
    }
    transform_to_go = {
        tr_id: tr.m_GameObject.m_PathID for tr_id, tr in transforms.items()
    }

    roots = [
        tr_id
        for tr_id, tr in transforms.items()
        if deref(objects, tr.m_Father) is None
    ]
    if len(roots) != 1:
        raise SystemExit(f"expected one root transform in {path.name}, found {len(roots)}")

    return env, objects, main, canvas, game_objects, transforms, transform_to_go, roots[0]


def collect_layers(objects, game_objects, transforms, transform_to_go, root_id):
    layers: list[Layer] = []
    order = 0

    def walk(tr_id, path):
        nonlocal order
        game_object = game_objects[transform_to_go[tr_id]]
        renderer = component(objects, game_object, "SpriteRenderer")
        if renderer is not None:
            material = None
            if renderer.m_Materials:
                mat_obj = deref(objects, renderer.m_Materials[0])
                if mat_obj is not None:
                    material = mat_obj.read()
            layers.append(
                Layer(
                    name=game_object.m_Name,
                    path=tuple(path),
                    tr_id=tr_id,
                    renderer=renderer,
                    sprite=renderer.m_Sprite.read(),
                    material=material,
                    order=order,
                    enabled=bool(renderer.m_Enabled),
                )
            )
            order += 1
        for child in transforms[tr_id].m_Children:
            child_go = game_objects[transform_to_go[child.m_PathID]]
            walk(child.m_PathID, path + [child_go.m_Name])

    for child in transforms[root_id].m_Children:
        child_go = game_objects[transform_to_go[child.m_PathID]]
        walk(child.m_PathID, [child_go.m_Name])
    return layers


def matrix_of(transform):
    q = transform.m_LocalRotation
    p = transform.m_LocalPosition
    s = transform.m_LocalScale
    angle = 2 * math.atan2(q.z, q.w)
    c, n = math.cos(angle), math.sin(angle)
    return (c * s.x, n * s.x, -n * s.y, c * s.y, p.x, p.y)


def matrix_mul(a, b):
    return (
        a[0] * b[0] + a[2] * b[1],
        a[1] * b[0] + a[3] * b[1],
        a[0] * b[2] + a[2] * b[3],
        a[1] * b[2] + a[3] * b[3],
        a[0] * b[4] + a[2] * b[5] + a[4],
        a[1] * b[4] + a[3] * b[5] + a[5],
    )


def matrix_point(m, x, y):
    return m[0] * x + m[2] * y + m[4], m[1] * x + m[3] * y + m[5]


def assign_world_matrices(transforms, objects, layers):
    parents = {}
    for tr_id, tr in transforms.items():
        father = deref(objects, tr.m_Father)
        if father is not None:
            parents[tr_id] = father.path_id

    cache = {}

    def world(tr_id):
        if tr_id not in cache:
            local = matrix_of(transforms[tr_id])
            parent = parents.get(tr_id)
            cache[tr_id] = matrix_mul(world(parent), local) if parent else local
        return cache[tr_id]

    for layer in layers:
        layer.world = world(layer.tr_id)


def apply_composition(layers, composition, composition_map):
    for item in composition.split(","):
        item = item.strip()
        if not item:
            continue
        if item in composition_map:
            apply_composition(layers, composition_map[item], composition_map)
            continue

        op_index = next((i for i, ch in enumerate(item) if ch in OPS), None)
        if op_index is None:
            print(
                f"warning: unrecognized layered actor composition expression: {item!r}",
                file=sys.stderr,
            )
            continue

        group_path = tuple(part for part in item[:op_index].strip().split("/") if part)
        op = item[op_index]
        name = item[op_index + 1 :].strip()

        def in_group(layer):
            return layer.path[: len(group_path)] == group_path

        group_layers = [layer for layer in layers if in_group(layer)]

        if not name:
            if op == ">":
                # Select: enable the group, disable its siblings under the parent.
                parent_path = group_path[:-1]
                for layer in layers:
                    if in_group(layer):
                        layer.enabled = True
                    elif (
                        group_path
                        and layer.path[: len(parent_path)] == parent_path
                    ):
                        layer.enabled = False
            else:
                value = op == "+"
                for layer in group_layers:
                    layer.enabled = value
            continue

        direct = [
            layer
            for layer in group_layers
            if layer.path[:-1] == group_path and layer.name == name
        ]
        if not direct:
            print(
                f"warning: unknown layer {name!r} in composition group {'/'.join(group_path)!r}",
                file=sys.stderr,
            )
            continue

        if op == ">":
            target = direct[0]
            for layer in group_layers:
                layer.enabled = layer is target
        else:
            value = op == "+"
            for layer in direct:
                layer.enabled = value


def material_info(material):
    if material is None:
        return MaterialInfo(0, 8, 0, 0.0, "default")
    floats = dict(material.m_SavedProperties.m_Floats)
    name = material.m_Name
    if "Multiply" in name:
        blend = "multiply"
    elif "Overlay" in name or "Softlight" in name:
        blend = "approximate"
    else:
        blend = "default"
    return MaterialInfo(
        stencil_ref=int(floats.get("_StencilRef", 0)),
        stencil_comp=int(floats.get("_StencilComp", 8)),
        stencil_op=int(floats.get("_StencilOp", 0)),
        cutoff=float(floats.get("_Cutoff", 0.0)),
        blend=blend,
    )


def tinted(image, color):
    rgb = (color.r, color.g, color.b)
    if rgb != (1, 1, 1):
        red, green, blue, alpha = image.split()
        red = red.point(lambda v, k=rgb[0]: round(v * k))
        green = green.point(lambda v, k=rgb[1]: round(v * k))
        blue = blue.point(lambda v, k=rgb[2]: round(v * k))
        image = Image.merge("RGBA", (red, green, blue, alpha))
    if color.a != 1:
        alpha = image.getchannel("A").point(lambda v, k=color.a: round(v * k))
        image.putalpha(alpha)
    return image


def union_paste(mask, patch, position):
    temporary = Image.new("L", mask.size, 0)
    temporary.paste(patch, position)
    return ImageChops.lighter(mask, temporary)


def multiply_blend(destination, source, position):
    width, height = source.size
    x, y = position
    region = destination.crop((x, y, x + width, y + height))
    alpha = source.getchannel("A")
    factor = Image.composite(
        source.convert("RGB"), Image.new("RGB", source.size, WHITE), alpha
    )
    color = ImageChops.multiply(region.convert("RGB"), factor)
    keep = ImageChops.lighter(alpha, alpha.point(lambda v: 255 - v))
    merged = color.convert("RGBA")
    merged.putalpha(ImageChops.multiply(region.getchannel("A"), keep))
    destination.paste(merged, position)


def render(character, appearance=None, ppu=50.0, keep_canvas=False, *, scene=None,
           enabled=None, save=True):
    (
        _,
        objects,
        main,
        canvas,
        game_objects,
        transforms,
        transform_to_go,
        root_id,
    ) = scene if scene is not None else load_scene(character)
    layers = collect_layers(
        objects, game_objects, transforms, transform_to_go, root_id
    )
    if not layers:
        raise SystemExit(f"{character} has no SpriteRenderer layers")

    composition_map = {item.Key: item.Composition for item in main.compositionMap}
    if enabled is not None:
        for layer in layers:
            layer.enabled = enabled[layer.tr_id]
    if appearance is None:
        appearance = main.defaultAppearance or ""
        slug = "default"
    else:
        slug = re.sub(r"[^0-9A-Za-z]+", "_", appearance).strip("_") or "appearance"

    if not main.renderOnly:
        apply_composition(layers, appearance, composition_map)
    assign_world_matrices(transforms, objects, layers)

    size = canvas.Size
    offset = canvas.Offset
    root_position = transforms[root_id].m_LocalPosition
    left = root_position.x + offset.x - size.x / 2
    top = root_position.y + offset.y + size.y / 2

    width = round(size.x * ppu)
    height = round(size.y * ppu)
    destination = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    stencil: dict[int, Image.Image] = {}
    drawn = []

    active = [layer for layer in layers if layer.enabled]
    active.sort(
        key=lambda layer: (layer.renderer.m_SortingOrder, layer.order),
        reverse=bool(main.reversed),
    )

    for layer in active:
        sprite = layer.sprite
        rect = sprite.m_Rect
        if rect.width <= 0 or rect.height <= 0:
            continue

        image = sprite.image.convert("RGBA")
        image = tinted(image, layer.renderer.m_Color)
        if layer.renderer.m_FlipX:
            image = image.transpose(Image.FLIP_LEFT_RIGHT)
        if layer.renderer.m_FlipY:
            image = image.transpose(Image.FLIP_TOP_BOTTOM)

        pixels_per_unit = sprite.m_PixelsToUnits
        pivot = sprite.m_Pivot
        world_width = rect.width / pixels_per_unit
        world_height = rect.height / pixels_per_unit
        x0 = -pivot.x * world_width
        x1 = (1 - pivot.x) * world_width
        y0 = -pivot.y * world_height
        y1 = (1 - pivot.y) * world_height

        corners = [
            matrix_point(layer.world, x, y) for x in (x0, x1) for y in (y0, y1)
        ]
        min_x = min(point[0] for point in corners)
        max_x = max(point[0] for point in corners)
        min_y = min(point[1] for point in corners)
        max_y = max(point[1] for point in corners)

        pixel_left = round((min_x - left) * ppu)
        pixel_top = round((top - max_y) * ppu)
        pixel_width = max(1, round((max_x - min_x) * ppu))
        pixel_height = max(1, round((max_y - min_y) * ppu))
        position = (pixel_left, pixel_top)

        scaled = image
        if scaled.size != (pixel_width, pixel_height):
            scaled = image.resize((pixel_width, pixel_height), Image.LANCZOS)

        info = material_info(layer.material)
        is_writer = info.stencil_op == 2 and info.stencil_ref != 0
        is_reader = info.stencil_comp == 4 and info.stencil_ref != 0

        if is_reader:
            mask = stencil.get(info.stencil_ref)
            if mask is None:
                continue
            source = mask.crop(
                (
                    position[0],
                    position[1],
                    position[0] + pixel_width,
                    position[1] + pixel_height,
                )
            )
            scaled.putalpha(ImageChops.multiply(scaled.getchannel("A"), source))

        if info.blend == "multiply":
            multiply_blend(destination, scaled, position)
        else:
            temporary = Image.new("RGBA", destination.size, (0, 0, 0, 0))
            temporary.paste(scaled, position)
            destination = Image.alpha_composite(destination, temporary)

        if is_writer:
            threshold = info.cutoff * 255
            coverage = scaled.getchannel("A").point(
                lambda value: 255 if value >= threshold else 0
            )
            mask = stencil.get(info.stencil_ref)
            if mask is None:
                mask = Image.new("L", destination.size, 0)
            stencil[info.stencil_ref] = union_paste(mask, coverage, position)

        drawn.append(
            {
                "layer": f"{layer.group}/{layer.name}".lstrip("/"),
                "sorting_order": layer.renderer.m_SortingOrder,
                "material": layer.material.m_Name if layer.material else None,
                "stencil_write": info.stencil_ref if is_writer else None,
                "stencil_read": info.stencil_ref if is_reader else None,
                "blend": info.blend,
            }
        )

    bbox = destination.getbbox()
    if bbox is None:
        raise SystemExit(f"appearance {appearance!r} of {character} rendered nothing")

    if not save:
        return destination, {layer.tr_id: layer.enabled for layer in layers}, bbox

    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    suffix = "_canvas" if keep_canvas else ""
    png_path = OUTPUT_DIR / f"{character}_{slug}{suffix}.png"
    preview_path = OUTPUT_DIR / f"{character}_{slug}{suffix}_white.png"
    json_path = OUTPUT_DIR / f"{character}_{slug}{suffix}.json"

    saved = destination if keep_canvas else destination.crop(bbox)
    saved.save(png_path)

    preview = Image.new("RGBA", saved.size, WHITE + (255,))
    preview.alpha_composite(saved)
    preview.convert("RGB").save(preview_path)

    metadata = {
        "character": character,
        "appearance": appearance,
        "composition_map_entries": len(composition_map),
        "canvas": {
            "size": [size.x, size.y],
            "offset": [offset.x, offset.y],
            "root_position": [root_position.x, root_position.y],
            "render_pixels": [width, height],
            "pixels_per_unit": ppu,
        },
        "image": {
            "transparent": png_path.name,
            "white_background": preview_path.name,
            "canvas_bbox": list(bbox),
            "size": list(saved.size),
            "world_bbox": {
                "left": left + bbox[0] / ppu,
                "right": left + bbox[2] / ppu,
                "bottom": top - bbox[3] / ppu,
                "top": top - bbox[1] / ppu,
            },
        },
        "drawn_layers": drawn,
        "notes": [
            "Naninovel_Overlay and Naninovel_Softlight are approximated with normal alpha compositing; their shaders cannot read the framebuffer.",
            "Multiply layers use dst * lerp(1, src, src.a) with alpha dst.a * max(src.a, 1-src.a).",
            "Stencil masks are unioned per reference value at canvas resolution.",
        ],
    }
    json_path.write_text(
        json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8"
    )

    return png_path, preview_path, json_path, metadata


def list_characters():
    for path in sorted(BUNDLE_DIR.glob("*.bundle")):
        try:
            env = UnityPy.load(str(path))
            for obj in env.objects:
                if obj.type.name == "MonoBehaviour":
                    data = obj.read()
                    if "compositionMap" in data.__dict__:
                        print(f"{path.stem:10s} {data.defaultAppearance}")
                        break
        except Exception as exc:  # noqa: BLE001 - keep scanning the other bundles
            print(f"{path.stem:10s} <error: {exc}>", file=sys.stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("character", nargs="?", help="bundle name, e.g. hiro")
    parser.add_argument(
        "--appearance",
        help="composition expression; defaults to the actor's defaultAppearance",
    )
    parser.add_argument(
        "--ppu",
        type=float,
        default=50.0,
        help="render pixels per world unit (default: 50)",
    )
    parser.add_argument(
        "--keep-canvas",
        action="store_true",
        help="keep the full canvas instead of cropping to the sprite",
    )
    parser.add_argument("--list", action="store_true", help="list layered characters")
    parser.add_argument(
        "--list-appearances",
        metavar="CHARACTER",
        help="list composition map keys of a character",
    )
    args = parser.parse_args()

    if args.list:
        list_characters()
        return
    if args.list_appearances:
        main_data = load_scene(args.list_appearances)[2]
        for item in main_data.compositionMap:
            print(f"{item.Key} = {item.Composition}")
        return
    if not args.character:
        parser.error("character is required (or use --list)")

    png, preview, metadata_path, metadata = render(
        args.character, args.appearance, args.ppu, args.keep_canvas
    )
    print(f"appearance: {metadata['appearance']}")
    print(f"layers:     {len(metadata['drawn_layers'])}")
    print(f"size:       {metadata['image']['size']}")
    print(f"image:      {png}")
    print(f"preview:    {preview}")
    print(f"metadata:   {metadata_path}")


if __name__ == "__main__":
    main()
