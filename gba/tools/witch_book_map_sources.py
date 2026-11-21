"""Inspect original book floors, versioned areas and all story map contexts.

Extracted source metadata for the GBA book-map converter; imports no pixels.
"""
import json
from graphics_project import GBA,ROOT, ContentCache, digest, write_changed


def extract():
    cache = ContentCache()
    bundle = ROOT/'original_game/manosaba_Data/StreamingAssets/aa/StandaloneWindows64/naninovel-ui_assets_all.bundle'
    key = digest(dict(step='witch-book-map-source-v1', source=cache.file_key(bundle)))
    floors = cache.get_json(key)
    if floors is None:
        import UnityPy
        env = UnityPy.load(str(bundle))
        objects = {obj.path_id: obj for obj in env.objects}
        trees = {key: obj.read_typetree() for key, obj in objects.items()
                 if obj.type.name in ('GameObject', 'RectTransform', 'MonoBehaviour')}

        def pointer(value):
            return value['m_PathID']

        def components(go):
            return [(objects[pointer(c['component'])].type.name, trees.get(pointer(c['component']), {}))
                    for c in trees[go]['m_Component']]

        def transform(go):
            return next(data for kind, data in components(go) if kind == 'RectTransform')

        def subtree(go):
            rect = transform(go)
            selected = ('m_Sprite', '_vertexes', '_triangles', 'key', 'defaultValue',
                        'm_Color', '_canvasGroup', '_focusButton', '_areaLabel', '_areaKind')
            return dict(name=trees[go]['m_Name'],
                        rect={k: rect[k] for k in ('m_AnchoredPosition', 'm_SizeDelta', 'm_AnchorMin',
                                                   'm_AnchorMax', 'm_Pivot', 'm_LocalScale')},
                        components=[{k: data[k] for k in selected if k in data}
                                    for kind, data in components(go)
                                    if kind == 'MonoBehaviour' and any(k in data for k in selected)],
                        children=[subtree(pointer(trees[pointer(child)]['m_GameObject']))
                                  for child in rect['m_Children']])

        floors = []
        for component in trees.values():
            if '_idAndVersion' in component:
                floors.append(dict(pair=component['_idAndVersion'],
                                   areas=[trees[pointer(area)]['_areaKind'] for area in component['_mapAreas']],
                                   tree=subtree(pointer(component['m_GameObject']))))
        cache.put_json(key, floors)

    contexts = []
    for path in sorted((ROOT/'story/zh-Hans').rglob('*.json')):
        if 'Debug' in path.parts:
            continue
        events = json.loads(path.read_text())['events']
        for index, event in enumerate(events):
            op, params = event['op'], event.get('params', {})
            spawn = params.get('Path', '')
            if (op == 'ChoiceMapArea' or
                    op == 'UpdateWitchBook' and params.get('Category') == 'Map' or
                    op in ('SpawnExtended', 'DestroySpawnedExtended') and
                    isinstance(spawn, str) and spawn.startswith('Map_')):
                contexts.append(dict(path=str(path.relative_to(ROOT)), event=event,
                                     before=events[max(0, index-2):index], after=events[index+1:index+3]))
    result = dict(source=str(bundle.relative_to(ROOT)), source_hash=cache.file_key(bundle),
                  floors=floors, contexts=contexts)
    write_changed(GBA/'assets/source-metadata/book-maps.json', json.dumps(result, ensure_ascii=False, indent=2)+'\n')
    print('Original map research:', len(floors), 'floor versions;',
          sum(c['event']['op'] == 'ChoiceMapArea' for c in contexts), 'place selections;',
          len(contexts), 'source contexts; cache hits', cache.hits)
    return result


if __name__ == '__main__':
    extract()
