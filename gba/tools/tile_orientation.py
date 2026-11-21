"""Hardware tile orientations shared by the CG encoder (identity, H, V, HV)."""
def orient(tiles,turn):
    result=tiles.reshape(-1,8,8)
    if turn&1:result=result[:,:,::-1]
    if turn&2:result=result[:,::-1,:]
    return result.reshape(-1,64)
