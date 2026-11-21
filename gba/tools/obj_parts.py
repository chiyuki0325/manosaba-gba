"""Keep OBJ output boxes shorter than the 96px vertical offscreen gap."""


def split_tall_parts(parts):
    # A 64px source becomes a 128px affine output box. At Y=139 it also
    # appears at Y=0..10 after the 8-bit coordinate wraps. Two 32px sources
    # have at most 64px output boxes, so neither can bridge 160..255.
    # Only tile references change: retain the original palette and pixels.
    result=[]
    for part in parts:
        if part['height']!=64:
            result.append(part)
            continue
        count=part['width']//8*4
        for half in range(2):
            result.append(dict(part,y=part['y']+half*32,height=32,
                               tiles=part['tiles'][half*count:(half+1)*count]))
    return result
