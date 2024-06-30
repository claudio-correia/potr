all: src/audited/audited src/auditor/auditor src/reader/reader src/remote-storage/remote-storage

src/audited/audited:
	make -C src/audited/ audited

src/auditor/auditor:
	make -C src/auditor auditor

src/reader/reader:
	make -C src/reader reader

src/remote-storage/remote-storage:
	make -C src/remote-storage remote-storage

clean:
	-make -C src/audited/ clean
	-make -C src/auditor clean
	-make -C src/reader clean
	-make -C src/remote-storage clean

.PHONY: all clean
