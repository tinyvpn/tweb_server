CC = g++
CFLAGS=
TARGET = tweb_server
SRCS := $(wildcard *.cpp) 
OBJS := $(patsubst %cpp, %o, $(SRCS)) 

all: $(TARGET) 

$(TARGET): $(OBJS) 
	$(CC) $(CFLAGS) -o $@ $^ -lpthread -lrt -lz -lm -lssl -lcrypto `mysql_config --cflags --libs`

%.o:%.cpp
	$(CC) $(CFLAGS) -std=c++11 -DLINUX_PLATFORM -c $<

clean: 
	rm -f *.o $(TARGET)
