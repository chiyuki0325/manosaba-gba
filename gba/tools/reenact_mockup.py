"""Original rigid-sprite scene preview, fixed crop and one 4bpp palette.

This preview uses the unskinned six-part weapon scene; SpriteSkin scenes need
their bone poses baked before using the same crop and quantizer.
"""
from build_paths import generated_path
from pathlib import Path
import argparse
import json, math, struct
import numpy as np
from PIL import Image
import UnityPy
from graphics_project import GBA, ROOT, write_changed
from image_encoding import tiles, rgb555


def matrix(tr):
    p, q, s = tr.m_LocalPosition, tr.m_LocalRotation, tr.m_LocalScale
    angle = math.atan2(2*(q.w*q.z+q.x*q.y), 1-2*(q.y*q.y+q.z*q.z))
    c, sn = math.cos(angle), math.sin(angle)
    return np.array([[c*s.x, -sn*s.y, p.x], [sn*s.x, c*s.y, p.y], [0, 0, 1]])


def render(env, name, config):
    pointer = next(p for key, p in env.container.items() if key.endswith(name+'.prefab'))
    parts = []
    def visit(ptr, parent, path='', active=True):
        go = ptr.read()
        path = (path+'/' if path else '')+go.m_Name
        components = [c.component for c in go.m_Component]
        tr = next(c.read() for c in components if c.type.name=='Transform')
        world = parent @ matrix(tr)
        active = active and go.m_IsActive
        if '/Story/' in path and active:
            assert not any(c.type.name=='MonoBehaviour' and 'm_BoneTransforms' in c.read_typetree() for c in components), 'Bake SpriteSkin before rigid preview'
            for component in components:
                if component.type.name=='SpriteRenderer':
                    renderer = component.read()
                    if renderer.m_Enabled and renderer.m_Sprite.m_PathID:
                        parts.append((renderer.m_SortingOrder, path, world, renderer))
        for child in tr.m_Children:
            visit(child.read().m_GameObject, world, path, active)
    visit(pointer, np.eye(3))
    width, height = config['source_canvas']
    scale = width/config['world_width']
    screen = np.array([[scale,0,width/2],[0,-scale,height/2],[0,0,1]])
    canvas = Image.new('RGBA', (width,height), (0,0,0,255))
    names = []
    for _, path, world, renderer in sorted(parts, key=lambda p:p[0]):
        sprite = renderer.m_Sprite.read()
        image = sprite.image.convert('RGBA')
        rect, offset = sprite.m_Rect, sprite.m_RD.textureRectOffset
        ppu = sprite.m_PixelsToUnits
        local = np.array([[1/ppu,0,(offset.x-sprite.m_Pivot.x*rect.width)/ppu],
                          [0,-1/ppu,(offset.y+image.height-sprite.m_Pivot.y*rect.height)/ppu],[0,0,1]])
        if renderer.m_FlipX:local[0]*=-1
        if renderer.m_FlipY:local[1]*=-1
        transform = screen @ world @ local
        inverse = np.linalg.inv(transform)
        tint = renderer.m_Color
        rgba = np.asarray(image).copy().astype(np.float32)
        rgba *= np.array([tint.r,tint.g,tint.b,tint.a])
        image = Image.fromarray(np.clip(rgba,0,255).astype('uint8'))
        projected = image.transform(canvas.size, Image.Transform.AFFINE,
                                    tuple(inverse[:2].reshape(-1)), Image.Resampling.BICUBIC)
        canvas.alpha_composite(projected)
        names.append(path)
    return canvas.convert('RGB').crop(tuple(config['crop'])).resize(tuple(config['output']),Image.Resampling.LANCZOS), names


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene',default='Reenact_Ch01_E01_S04')
    parser.add_argument('--name',default='spear')
    args = parser.parse_args()
    config = json.loads((GBA/'assets/reenact-layout.json').read_text())
    env = UnityPy.load(str(ROOT/'original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64/naninovel-spawn_assets_all.bundle'))
    name = args.scene
    source, parts = render(env, name, config)
    trained = source.quantize(colors=15, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    raw = trained.getpalette()[:45]
    colors = [(0,0,0)]+[tuple((v+4)//8*8 if v<248 else 248 for v in raw[i:i+3]) for i in range(0,45,3)]
    rgb = np.asarray(source).astype(np.int32)
    distance = ((rgb[:,:,None,:]-np.array(colors[1:])[None,None,:,:])**2).sum(axis=3)
    pixels = (distance.argmin(axis=2)+1).astype('uint8')
    image = Image.fromarray(pixels, 'P')
    image.putpalette(sum((list(c) for c in colors),[])+[0]*720)
    out = generated_path('design/reenact')
    out.mkdir(parents=True,exist_ok=True)
    source.save(out/f'{args.name}-source-240x160.png')
    image.save(out/f'{args.name}-4bpp-240x160.png',bits=4)
    image.convert('RGB').resize((960,640),Image.Resampling.NEAREST).save(out/f'{args.name}-4bpp-4x.png')
    side = Image.new('RGB',(480,160))
    side.paste(source,(0,0));side.paste(image.convert('RGB'),(240,0))
    side.resize((1440,480),Image.Resampling.NEAREST).save(out/f'{args.name}-comparison-3x.png')
    encoded = tiles(image,4)
    write_changed(out/f'{args.name}.4bpp',encoded)
    write_changed(out/f'{args.name}.gbapal',struct.pack('<16H',*(rgb555(c) for c in colors)))
    assert len(encoded)==19200 and pixels.max()<16
    report = dict(scene=name,layout=config,source_parts=parts,colors_used=len(np.unique(pixels)),
                  palette_colors=16,transparent_index=0,tile_bytes=len(encoded),palette_bytes=32,
                  note='15 opaque colors plus transparent slot; RGB555; no dithering; no stage frame')
    write_changed(out/f'{args.name}-preview.json',json.dumps(report,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(report,ensure_ascii=False))


if __name__=='__main__':main()
