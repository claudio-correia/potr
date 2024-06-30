set -e

#              4300                      4301          4302
#             audited    auditor    remote-storage    reader 
# 1 local                                  X
# 2 lab-sgx      X                         3*           X
# 3 tagus                                  X
# 4 netherlands  2*         X              X            2*
# 5 australia               X
# (* ssh reverse proxy)

make clean || true

ssh lab-sgx "mkdir -p potr/src potr/dataset potr/results"
ssh tagus "mkdir -p potr/src potr/dataset potr/results"
ssh netherlands "mkdir -p potr/src potr/dataset potr/results"
ssh australia "mkdir -p potr/src potr/results"

scp dataset/generate.sh lab-sgx:potr/dataset
scp dataset/generate.sh tagus:potr/dataset
scp dataset/generate.sh netherlands:potr/dataset

scp -r src/common lab-sgx:potr/src/
scp -r src/audited lab-sgx:potr/src/
scp -r src/reader lab-sgx:potr/src/
ssh lab-sgx "cd potr/src/audited; make clean; make"
ssh lab-sgx "cd potr/src/reader; make clean; make"

scp -r src/common tagus:potr/src/
scp -r src/remote-storage/ tagus:potr/src/
scp -r src/reader tagus:potr/src/
ssh tagus "cd potr/src/remote-storage; make clean; make"
ssh tagus "cd potr/src/reader; make clean; make"

scp ./notify_done.sh netherlands:potr/
scp -r src/common netherlands:potr/src/
scp -r src/auditor netherlands:potr/src/
scp -r src/remote-storage/ netherlands:potr/src/
scp -r src/reader netherlands:potr/src/
ssh netherlands "cd potr/src/auditor; make clean; make"
ssh netherlands "cd potr/src/remote-storage; make clean; make"
ssh netherlands "cd potr/src/reader; make clean; make"

scp ./notify_done.sh australia:potr/
scp -r src/common australia:potr/src/
scp -r src/auditor australia:potr/src/
ssh australia "cd potr/src/auditor; make clean; make"

cd src/remote-storage
make
cd ../reader
make
