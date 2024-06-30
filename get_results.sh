scp lab-sgx:potr/results/* results/
scp tagus:potr/results/* results/
scp netherlands:potr/results/* results/

ssh lab-sgx "rm potr/results/*"
ssh tagus "rm potr/results/*"
ssh netherlands "rm potr/results/*"
