CC=arm-linux-gnueabihf-gcc
#CC=gcc
OBJ=main.o gpio.o dht11.o
TARGET=collector

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(OBJ) -o $(TARGET) -lpthread

%.o:%.c
	$(CC) -c $< -o $@

clean:
	rm -rf *.o $(TARGET)
