CC      = gcc
CFLAGS  = -std=gnu11 -Wall -Wextra -O2 -DUNICODE -D_UNICODE \
           -I src -I third_party/sqlite
DEPFLAGS = -MMD -MP
LDFLAGS = -mwindows \
           -Wl,--dynamicbase,--nxcompat \
           -lcomctl32 -lcomdlg32 -luxtheme \
           -lshell32 -luser32 -lgdi32 -lkernel32

TARGET  = KeyboardSim.exe
SRCS    = src/main.c src/ui.c src/worker.c src/config.c src/database.c src/qa_ui.c \
           src/mem.c src/textfile.c third_party/sqlite/sqlite3.c
OBJS    = $(SRCS:.c=.o)
DEPS    = $(OBJS:.o=.d)
RES     = res/app.res
TEST_TARGET = tests/database_worker_test.exe

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(OBJS) $(RES)
	$(CC) $(OBJS) $(RES) -o $@ $(LDFLAGS)
	@echo Build OK: $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) $(DEPFLAGS) -c $< -o $@

$(RES): res/app.rc res/app.manifest res/app.ico src/resource.h
	windres -I src -I res res/app.rc -O coff -o $(RES)

test: $(TEST_TARGET)
	$(TEST_TARGET)

$(TEST_TARGET): tests/database_worker_test.c src/database.c src/database.h src/worker.c src/worker.h src/mem.c src/mem.h src/textfile.c src/textfile.h third_party/sqlite/sqlite3.c third_party/sqlite/sqlite3.h
	$(CC) $(CFLAGS) tests/database_worker_test.c src/database.c src/worker.c src/mem.c src/textfile.c third_party/sqlite/sqlite3.c -o $@ -luser32 -lkernel32

clean:
	-del /Q src\*.o src\*.d third_party\sqlite\*.o third_party\sqlite\*.d res\app.res $(TARGET) $(TEST_TARGET) 2>nul

-include $(DEPS)
