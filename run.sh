set -e

tmux new-session -d -s potr
tmux rename-window -t potr:0 'near'
tmux split-window -h -t potr:0.0
tmux send -t potr:0.0 'cd src/remote-storage; sleep 1; clear' C-m
tmux send -t potr:0.1 'cd src/reader; sleep 1; clear' C-m

tmux new-window -t potr
tmux rename-window -t potr:1 'sgx'
tmux split-window -h -t potr:1.0
tmux send -t potr:1.0 'ssh lab-sgx' C-m 'cd potr/src/audited; sleep 1; clear' C-m
tmux send -t potr:1.1 'ssh lab-sgx' C-m 'cd potr/src/reader; sleep 1; clear' C-m

tmux new-window -t potr
tmux rename-window -t potr:2 'tagus'
tmux split-window -h -t potr:2.0
tmux send -t potr:2.0 'ssh tagus' C-m 'cd potr/src/remote-storage; sleep 1; clear' C-m
tmux send -t potr:2.1 'ssh tagus' C-m 'cd potr/src/reader; sleep 1; clear' C-m

tmux new-window -t potr
tmux rename-window -t potr:3 'netherlands'
tmux split-window -h -t potr:3.0
tmux split-window -h -t potr:3.1
tmux send -t potr:3.0 'ssh netherlands' C-m 'cd potr/src/auditor; sleep 1; clear' C-m
tmux send -t potr:3.1 'ssh netherlands' C-m 'cd potr/src/remote-storage; sleep 1; clear' C-m
tmux send -t potr:3.2 'ssh netherlands' C-m 'cd potr/src/reader; sleep 1; clear' C-m

tmux send -t potr:0.0 'pkill remote-storage; ./remote-storage' C-m
tmux send -t potr:0.1 'pkill reader; ./reader' C-m
tmux send -t potr:1.0 'pkill audited; make run' C-m
tmux send -t potr:1.1 'pkill reader; ./reader' C-m
tmux send -t potr:2.0 'pkill remote-storage; ./remote-storage' C-m
tmux send -t potr:2.1 'pkill reader; ./reader' C-m
tmux send -t potr:3.1 'pkill remote-storage; ./remote-storage' C-m
tmux send -t potr:3.2 'pkill reader; ./reader' C-m

tmux new-window -t potr
tmux rename-window -t potr:4 'proxy'
tmux split-window -h -t potr:4.0
tmux split-window -h -t potr:4.0
tmux split-window -h -t potr:4.1
tmux send -t potr:4.0 'sleep 1; clear' C-m 'ssh -NT -R 4305:localhost:4302 luisfonseca@51.136.20.214' C-m # netherlands -> near
tmux send -t potr:4.1 'ssh lab-sgx' C-m 'sleep 1; clear' C-m 'ssh -NT -R 4300:localhost:4300 -R 4304:localhost:4302 luisfonseca@51.136.20.214' C-m # netherlands -> sgx
tmux send -t potr:4.2 'ssh tagus' C-m 'sleep 1; clear' C-m 'ssh -NT -R 4303:localhost:4302 luisfonseca@51.136.20.214' C-m # netherlands -> tagus
tmux send -t potr:4.3 'ssh tagus' C-m 'sleep 1; clear' C-m 'ssh -NT -R 4301:localhost:4301 intelnuc6@146.193.41.170' C-m # sgx -> tagus

sleep 5

tmux select-window -t potr:3
tmux send -t potr:3.0 'pkill auditor; ./auditor; ../../notify_done.sh' C-m
tmux a -t potr
