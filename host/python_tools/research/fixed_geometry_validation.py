"""New geometry validation, reused CAL assets; original RESERVE remains sealed."""
from research.ordinary_meeting_room_research_dataset import OrdinaryRoomResearchDataset

class FixedGeometryValidationDataset(OrdinaryRoomResearchDataset):
    def __init__(self,config_path):
        super().__init__(config_path)
        self.allowed_indices=set(range(192,256))
        for i in self.allowed_indices:
            self.plan[i]['scene_id']='geoval_20261014_'+self.plan[i]['scene_id']
            self.plan[i]['evaluation_role']='NEW_GEOMETRY_FIXED_VALIDATION_REUSED_CAL_ASSETS'

    def prepare_source_crops(self,role):
        if role!='CAL':raise ValueError('Only reusedCAL dry crops for geometric validation are permitted')
        return super().prepare_source_crops(role)

    def generate_state_rirs(self,index):
        if index not in (12,13,14,15):raise ValueError('Only four new geometry validation poses may generate RIRs')
        return super().generate_state_rirs(index)

    def sample(self,index):
        if index not in self.allowed_indices:raise ValueError('TRAIN and RESERVE acoustic generation forbidden in this validation')
        return super().sample(index)
