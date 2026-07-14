CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -DUNICODE -D_UNICODE \
          -I src -I third_party/sqlite
LDFLAGS = -mwindows \
          -lcomctl32 -lcomdlg32 -luxtheme \
          -lshell32 -luser32 -lgdi32 -lkernel32

TARGET  = KeyboardSim.exe
SRCS    = src/main.c src/ui.c src/worker.c src/config.c src/database.c src/qa_ui.c third_party/sqlite/sqlite3.c
OBJS    = $(SRCS:.c=.o)
RES     = res/app.res

all: $(TARGET)

$(TARGET): $(OBJS) $(RES)
	$(CC) $(OBJS) $(RES) -o $@ $(LDFLAGS)
	@echo Build OK: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

$(RES): res/app.rc res/app.manifest
	windres res/app.rc -O coff -o $(RES)

clean:
	-del /Q src\*.o res\app.res $(TARGET) 2>nul
