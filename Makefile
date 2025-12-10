CC = gcc
CFLAGS = -Wall -g -pthread
LIBS = -lrt
TARGET = procx
SRC = procx.c
KEY_FILE = procx_mq_key

all: check_key $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)
	@echo ">>> Derleme Basarili! Calistirmak icin: ./$(TARGET)"

check_key:
	@if [ ! -f $(KEY_FILE) ]; then \
		touch $(KEY_FILE); \
		echo ">>> IPC anahtar dosyasi ($(KEY_FILE)) olusturuldu."; \
	fi

clean:
	rm -f $(TARGET)
	@echo ">>> Derleme dosyalari temizlendi."

run: all
	./$(TARGET)