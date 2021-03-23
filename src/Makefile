CC=g++
CXXFLAGS+= -g -DDEBUG
#CXXFLAGS+= -DNDEBUG
CXXFLAGS+= -std=c++17 -O3 -finline-functions -funroll-all-loops -fno-rtti -msse4.2 -pthread -lm -m64 -DIS_64BIT -flto -Wall -Wcast-qual -pedantic -Wextra -Wshadow -fstrict-aliasing


OBJFOLD=obj

OBJ = $(OBJFOLD)/engine.o \
	$(OBJFOLD)/jobs.o \
	$(OBJFOLD)/main.o \
	$(OBJFOLD)/openings.o \
	$(OBJFOLD)/options.o \
	$(OBJFOLD)/seqwriter.o \
	$(OBJFOLD)/sprt.o \
	$(OBJFOLD)/str.o \
	$(OBJFOLD)/util.o \
	$(OBJFOLD)/vec.o \
	$(OBJFOLD)/workers.o \
	$(OBJFOLD)/position.o \
	$(OBJFOLD)/game.o

EXE = c-gomoku-cli

$(EXE): mkfolders $(OBJ)
	$(CC) $(CXXFLAGS) $(DEFINES) $(LDFLAGS) $(OBJ) -o $(EXE) -lm -pthread

$(OBJFOLD)/%.o: %.cpp
	$(CC) $(CXXFLAGS) $(DEFINES) -c $*.cpp -o $(OBJFOLD)/$*.o

clean:
	rm -rf $(OBJFOLD)

mkfolders: makeobj

makeobj:
	mkdir -p $(OBJFOLD)
