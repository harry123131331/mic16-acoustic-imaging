"""CAL-only release-candidate guard; neither labels nor counts enter inference."""
VERSION = 'mic16_basic_release_guard_v1_20261009'
THRESHOLD = .65
BASIC_SCENARIOS = ('one', 'two')


def evaluation(arrays, ids, logits, metric_function):
    _, table, predictions = metric_function(arrays, ids, logits)
    row = next(r for r in table if r['threshold'] == THRESHOLD)
    entries = predictions[str(THRESHOLD)]
    basic = [r for r in entries if arrays['records'][r['index']]['scenario'] in BASIC_SCENARIOS]
    return dict(metrics=row['metrics'], threshold=THRESHOLD,
                basic_windows=len(basic), basic_tp=sum(r['metrics']['tp'] for r in basic),
                basic_fp=sum(r['metrics']['fp'] for r in basic),
                basic_fn=sum(r['metrics']['fn'] for r in basic),
                basic_error_sum=sum(r['metrics']['fp']+r['metrics']['fn'] for r in basic),
                basic_no_FP_FN_windows=sum(r['metrics']['fp']==r['metrics']['fn']==0 for r in basic))


def eligible(row, baseline):
    m, b = row['metrics'], baseline['metrics']
    # 这是开发检查，不是独立验收结论。
    return (row['threshold'] == baseline['threshold'] == THRESHOLD
            and row['basic_windows'] == baseline['basic_windows']
            and row['basic_error_sum'] < baseline['basic_error_sum']
            and m['f1'] > b['f1'] and m['recall'] >= b['recall']
            and m['fp'] <= b['fp']
            and m['clear_zero_nonempty_windows'] <= b['clear_zero_nonempty_windows'])


def key(row, epoch):
    m = row['metrics']
    return (-row['basic_error_sum'], m['f1'], -m['fp'], m['recall'], -epoch)
