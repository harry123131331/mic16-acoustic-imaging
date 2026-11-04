"""Expose the same verified replay API for this bounded repair run."""
from types import SimpleNamespace
import train_multisource_basic_repair_one_20261009 as repair
run = SimpleNamespace(**vars(repair))
run.base = repair.data.base
run.load_arrays = repair.data.load_arrays
run.setup = repair.data.setup
run.batch = repair.data.batch
run.THRESHOLDS = repair.data.THRESHOLDS
