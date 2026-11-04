"""One isolated candidate-order ablation; observed clusters only, fixed C16."""
from audit_pre_gate_candidate_coverage import fixed_bounded_pool


def weight_first_pool(raw_clusters):
    """Keep original centroids, 10-degree NMS and cap16; change only priority.

    weight_sum already exists in the original clustering code: positive peak
    response * positive peak contrast * positive excess, summed over votes.
    This is a physical heuristic, not a learned likelihood or calibrated SNR.
    Python's stable ordering resolves exact ties in original cluster order.
    """
    ranked = sorted(raw_clusters, key=lambda c: (
        c['weight_sum'], c['frames'], c['bands'], c['votes']), reverse=True)
    return fixed_bounded_pool(ranked)
