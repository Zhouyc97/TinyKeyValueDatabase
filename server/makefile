server: ./command/command.cpp ./locker/locker.cpp ./skiplist/skiplist.hpp ./threadpool/threadpool.hpp server.cpp server_main.cpp ./log/block_que.hpp ./log/log.cpp
	$(CXX) $^ -o $@ -w -pthread -std=c++11

clean:
	rm  -rf server 

