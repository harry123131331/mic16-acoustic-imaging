"""Filter search eligibility AFTER scores from the full original C16 context.

Never recompute competition/neighborhood features using a pruned search pool.
Only selects an original scored direction covered by an observed proposal.
This cannot recover a missing original direction or change a low original score.
"""
import numpy as np
from audit_pre_gate_candidate_coverage import distances
VERSION='canonical_C16_score_then_observed_proposal_filter_20261015'
def filter_scored_pool(anchor_directions,anchor_valid,anchor_logits,proposal_directions,proposal_valid):
    d=np.asarray(anchor_directions,float);v=np.asarray(anchor_valid,bool);z=np.asarray(anchor_logits)
    q=np.asarray(proposal_directions,float);u=np.asarray(proposal_valid,bool)
    if d.shape!=(16,2) or v.shape!=(16,) or z.shape!=(16,) or q.shape!=(16,2) or u.shape!=(16,):raise ValueError('fixed C16 observed arrays required')
    if not np.isfinite(d).all() or not np.isfinite(z).all() or not np.isfinite(q).all():raise ValueError('finite input required')
    qualified=np.any(distances(d,q[u])<=10.,axis=1) if u.any() else np.zeros(16,bool)
    return dict(directions=d.copy(),valid=v&qualified,logits=z.copy(),
        canonical_scores_unchanged=True,original_candidate_context_preserved=True,proposal_match_radius_deg=10.)
