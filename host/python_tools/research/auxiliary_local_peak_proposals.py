"""One auxiliary spatial local maximum per TF zone, no source-count input.

Original 480 global-winner observations and their weights are untouched.
This proposal-only change is not a new physical propagation law or a proven
novel method. Auxiliary peaks need not be actual sources in reverberation.
"""
import numpy as np
from scipy.ndimage import maximum_filter, minimum_filter

VERSION='C16_original_winner_plus_one_auxiliary_local_peak_20261014'


def auxiliary_votes(maps, primary_votes, grid, allowed):
    maps=np.asarray(maps);primary=np.asarray(primary_votes,float);grid=np.asarray(grid,float)
    if maps.shape!=(20,24,13,25) or primary.shape!=(480,9) or grid.shape!=(325,2) or np.shape(allowed)!=(325,325):
        raise ValueError('fixed20x24x13x25 spatial maps and480 original votes required')
    if not np.isfinite(maps).all() or not np.isfinite(primary).all():
        raise ValueError('finite maps/votes required')
    # 网格边界闭合，方位角不环绕；平坦常数图不增加候选。
    high=maximum_filter(maps,size=(1,1,3,3),mode='nearest')
    low=minimum_filter(maps,size=(1,1,3,3),mode='nearest')
    local=(maps==high)&(maps>low)
    extra=[];indices=[]
    for t in range(20):
        for z in range(24):
            score=maps[t,z].ravel();winner=int(np.argmax(score))
            row=primary[t*24+z]
            if row[0]!=t or row[1]!=z or not np.array_equal(row[4:6],grid[winner]):
                raise ValueError('original winner does not match the map/grid')
            ids=np.flatnonzero(local[t,z].ravel()&allowed[winner])
            if not len(ids):continue
            # argmax同值时保留最早网格索引，此规则事前固定。
            second=int(ids[np.argmax(score[ids])]);new=row.copy();new[4:6]=grid[second]
            new[7]=float(score[second])
            # 保留既有全局竞争对比度定义，获胜候选自身
            # 也是合格竞争者，所以该对比度通常为0。
            new[8]=max(0.,float(score[second])-float(np.max(score[allowed[second]])))
            extra.append(new);indices.append([t,z,winner,second])
    extras=np.asarray(extra,float).reshape(-1,9)
    combined=np.concatenate((primary,extras),axis=0)
    assert np.array_equal(combined[:480],primary) and len(extras)<=480
    return dict(votes=combined,auxiliary_votes=extras,
        diagnostics=dict(auxiliary_vote_count=len(extras),primary_vote_count=480,
            total_votes=len(combined),original_primary_rows_unchanged=True,
            secondary_peak_contrasts_zero=bool(np.all(extras[:,8]==0)),
            auxiliary_peaks_are_sources=False,known_source_count_used=False),indices=indices)

