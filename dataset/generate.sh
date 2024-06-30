# Generates 150 files with 1GB each
mkdir -p data
cd data
seq -w 0 150 | xargs -n1 -I% sh -c 'dd if=/dev/urandom of=file%.dat bs=1000000 count=1024'
