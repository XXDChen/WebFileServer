TARGET = server
SOURCES = $(wildcard *.cpp ./sql/*.cpp ./log/*.cpp)

$(TARGET): $(SOURCES)
	g++ $^ -o $@ -pthread -lmysqlclient
	
.PHONY:clean
clean:
	rm -f $(TARGET)