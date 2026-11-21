"""Group equal-shaped binary alpha blocks; color indices remain unchanged."""
from collections import Counter


def silhouette_groups(parts,payload,offsets,alpha_tiles=None):
    if alpha_tiles is None:alpha_tiles={}
    def alpha(ref):
        if ref not in alpha_tiles:
            tile=payload[offsets[ref]:offsets[ref]+32]
            alpha_tiles[ref]=bytes(bool(v&15) | (bool(v&240)<<4) for v in tile)
        return alpha_tiles[ref]
    signatures=[(p['width'],p['height'],b''.join(alpha(t) for t in p['tiles'])) for p in parts]
    counts=Counter(signatures);first={}
    for i,signature in enumerate(signatures):first.setdefault(signature,i+1)
    return [first[s] if counts[s]>1 else 0 for s in signatures]
