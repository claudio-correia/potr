remote-storage:
	g++ -std=c++11 server.cpp ../common/communication.cpp ../common/storage.cpp -lssl -lcrypto -lpthread -o remote-storage

.PHONY: clean
clean:
	rm remote-storage