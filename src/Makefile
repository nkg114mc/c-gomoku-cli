CC=g++
#CXXFLAGS+= -g -DDEBUG
CXXFLAGS+= -DNDEBUG
CXXFLAGS+= -std=c++17 -O2 -fno-exceptions -fno-rtti -msse4.2 -pthread -lm -m64 -DIS_64BIT -flto -Wall -Wcast-qual -pedantic -Wextra -Wshadow -fstrict-aliasing -Wno-attributes


OBJFOLD=obj

OBJ = $(OBJFOLD)/engine.o \
	$(OBJFOLD)/jobs.o \
	$(OBJFOLD)/main.o \
	$(OBJFOLD)/openings.o \
	$(OBJFOLD)/options.o \
	$(OBJFOLD)/seqwriter.o \
	$(OBJFOLD)/sprt.o \
	$(OBJFOLD)/util.o \
	$(OBJFOLD)/workers.o \
	$(OBJFOLD)/position.o \
	$(OBJFOLD)/game.o

OBJ_EXT = $(OBJFOLD)/extern_lz4.o \
	$(OBJFOLD)/extern_lz4frame.o \
	$(OBJFOLD)/extern_lz4hc.o \
	$(OBJFOLD)/extern_xxhash.o \

EXE = c-gomoku-cli

$(EXE): mkfolders $(OBJ) $(OBJ_EXT)
	$(CC) $(CXXFLAGS) $(DEFINES) $(LDFLAGS) $(OBJ) $(OBJ_EXT) -o $(EXE) -lm -pthread

$(OBJFOLD)/%.o: %.cpp
	$(CC) $(CXXFLAGS) $(DEFINES) -c $*.cpp -o $(OBJFOLD)/$*.o

$(OBJFOLD)/extern_%.o: extern/%.c
	$(CC) $(CXXFLAGS) $(DEFINES) -c extern/$*.c -o $(OBJFOLD)/extern_$*.o

clean:
	rm -rf $(OBJFOLD)

mkfolders: makeobj

makeobj:
	mkdir -p $(OBJFOLD)

format:
	find . -maxdepth 1 -name '*.h' -or -name '*.hpp' -or -name '*.cpp' | xargs clang-format -i -style=file $1