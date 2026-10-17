#!/usr/bin/env bash
# Real end-to-end experiments of V0.8 (27 tune runs + holdout evaluations). Requires build/nova and tables in benchmarks/results.
cd "$(dirname "$0")/.."
E=state/evidence/V0.8
H=benchmarks/history/V0.8-tuning-history.json
for k in stencil vector_add matmul; do
  for m in random adaptive adaptive-no-features; do
    for sd in 1 2 3; do
      id=V0.8-real-$k-$m-s$sd-B6
      case $m in
        random) opt="--strategy=random" ;;
        adaptive) opt="--strategy=adaptive --history=$H --history-exclude-same" ;;
        adaptive-no-features) opt="--strategy=adaptive --ablation=no-features --history=$H --history-exclude-same" ;;
      esac
      ./build/nova.exe tune $k --budget=6 --seed=$sd --reps=15 --warmup=2 --affinity=10 --high-priority $opt --id=$id > $E/31_$id.log 2>&1
      echo "$id tune exit $?" >> $E/30_real_done.log
      ./build/nova.exe evaluate benchmarks/results/$id --id=$id-holdout --out=benchmarks/results/$id-holdout --affinity=10 --high-priority > $E/32_$id-holdout.log 2>&1
      echo "$id evaluate exit $?" >> $E/30_real_done.log
    done
  done
done
echo ALLDONE >> $E/30_real_done.log
