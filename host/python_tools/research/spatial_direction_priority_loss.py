"""Single-factor auxiliary-count coefficient; unchanged map BCE definition."""
from research.spatial_spectrum_joint import joint_loss
MAP_COEFFICIENT = .5
COUNT_COEFFICIENT = 1/18

def direction_priority_loss(output, map_labels, count_labels):
    _, parts = joint_loss(output, map_labels, count_labels)
    return MAP_COEFFICIENT*parts['map_BCE'] + COUNT_COEFFICIENT*parts['count_CE'], parts
