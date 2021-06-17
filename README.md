# c-gomoku-cli

c-gomoku-cli is a command line interface for gomoku/renju engines that support [Gomocup protocol](http://petr.lastovicka.sweb.cz/protocl2en.htm). It is a derived project from [c-chess-cli]( https://github.com/lucasart/c-chess-cli), originally authored by Lucas Braesch (lucasart).

Different from computer chess community where a lot of interface tools have been developed in the passed 20 years, computer gomoku/renju is still lack of tools for engine-vs-engine matches. Most of the existing tools are GUI based, which is not suitable for distributed systems like high performance clusters. The motivation of c-gomoku-cli is to provide an open-source command line tool that can run large scale engine-vs-engine matches on multi-core workstations or cluster systems in parallel. The large-scale engine-vs-engine testing is common approach for quantifying the strength improvements of computer game engines, like chess. With this tool, engine designers can concisely measure the new engine ELO gain by letting it play with the old version for enough number of games.

Instead of sticking on the C language to avoid the dependency hell in c-chess-cli, C++ is chosen to be the primary programming language in this project, because my target is to build a usable tool and C++ contains some features that can bring more convenience to achieving this target. Similar to c-chess-cli, c-gomoku-cli is primarily developed for Unix/Linux, it should work on all POSIX operating systems, including MacOS and Android. Windows is not supported so far.


## Compiling from the Source

Under the root directory of project, run `cd src`, then run `make`.

## Usage

```
c-gomoku-cli [-each [eng_options]] -engine [eng_options] -engine [eng_options] ... [options]
```

### Example

```
c-gomoku-cli -each tc=180/30 \
    -engine name=Wine cmd=./example-engine/pbrain-wine \
    -engine name=Rapfi cmd=./example-engine/pbrain-rapfi1 \
    -rule 0 -boardsize 20 -rounds 1 -games 4000 -debug -repeat \
    -concurrency 8 -draw number=200 \
    -pgn my_games.pgn -sgf my_games.sgf \
    -openings file=openings_dir/openings_freestyle.txt order=random
```

### Options

 * `engine OPTIONS`: Add an engine defined by `OPTIONS` to the tournament.
 * `each OPTIONS`: Apply `OPTIONS` to each engine in the tournament.
 * `concurrency N`: Set the maximum number of concurrent games to N (default value 1).
 * `draw COUNT SCORE`: Adjudicate the game as a draw, if the score of both engines is within `SCORE` centipawns from zero, for at least `COUNT` consecutive moves.
 * `resign COUNT SCORE`: Adjudicate the game as a loss, if an engine's score is at least `SCORE` centipawns below zero, for at least `COUNT` consecutive moves.
 * `drawafter N`: Adjudicate the game as a draw, if the number of moves in one game reaches `N` ply. `N` must be greater then `0` to be effective.
 * `rule RULE`: Set the game rule with Gomocup rule code `RULE`.
   * `RULE=0`: Play with gomoku rule and winner wins by five or longer connection.
   * `RULE=1`: Play with gomoku rule but winner only wins by exact-5 connection (longer connections will be ignored).
   * `RULE=4`: Play with renju rule.
   * No other rule code is acceptable.
 * `boardsize SIZE`: Set the board size to `SIZE` X `SIZE`. Valid `SIZE` range is `[5..22]`. 
 * `games N`: Play N games per encounter (default value 1). This value should be set to an even number in tournaments with more than two players to make sure that each player plays an equal number of games with black and white colors.
 * `rounds N`: Multiply the number of rounds to play by `N` (default value 1). This only makes sense to use for tournaments with more than 2 engines.
 * `gauntlet`: Play a gauntlet tournament (first engine against the others). The default is to play a round-robin (plays all pairs).
   * with `n=2` engines, both gauntlet and round-robin just play the number of `-games` specified.
   * gauntlet for `n>2`: `G(e1, ..., en) = G(e1, e2) + G(e1, e3) + ... + G(e1, en)`. There are `n-1` pairs.
   * round-robin for `n>2`: `RR(e1, ..., en) = G(e1, ..., en) + RR(e2, ..., en)`. There are `n(n-1)/2` pairs.
   * using `-rounds` repeats the tournament `-rounds` times. The number of games played for each pair is therefore `-games * -rounds`.
 * `repeat`: Repeat each opening twice, with each engine playing both sides. 
 * `sprt [elo0=E0] elo1=E1 [alpha=A] [beta=B]`: Performs a Sequential Probability Ratio Test for `H1: elo=E1` vs `H0: elo=E0`, where `alpha` is the type I error probability (false positive), and `beta` is type II error probability (false negative). Default values are `elo0=0`, and `alpha=beta=0.05`. This can only be used in matches between two players.
 * `log`: Write all I/O communication with engines to file(s). This produces `c-gomoku-cli.id.log`, where `id` is the thread id (range `1..concurrency`). Note that all communications (including error messages) starting with `[id]` mean within the context of thread number `id`, which tells you which log file to inspect (id = 0 is the main thread, which does not product a log file, but simply writes to stdout).
 * `debug`: Turn on debug mode. In debug mode, more detailed information about game and engines will be printed, and `-log` will also be turned on automatically.
 * `sendbyboard`: Send full position using `BOARD` command before each move. If not specified, continuous position are sent using `TURN`. Some engines might behave differently when receiving `BOARD` rather than `TURN`.
 * `openings file=FILE [type=TYPE] [order=ORDER] [srand=N]`:
   * Read opening positions from `FILE`, in `TYPE` format. `type` can be `offset` (default value) or `pos`. See "Openings File Format" section below about details of different formats.
   * `order` can be `random` or `sequential` (default value).
   * `srand` sets the seed of the random number generator to `N`. The default value `N=0` will set the seed automatically to an unpredictable number. Any non-zero number will generate a unique, reproducible random sequence.
 * `pgn FILE`: Save a dummy game to `FILE`, in PGN format. PGN format is for chess games. We replace the moves with some random chess moves but only keep the game result and player names. This dummy PGN file can be input by [BayesianElo](https://www.remi-coulom.fr/Bayesian-Elo/) to compute ELO scores.
 * `sgf FILE`: Save a game to `FILE`, in SGF format.
 * `msg FILE`: Save engine messages to `FILE`, in TXT format. Messages in each games are grouped by game index.
 * `sample`. See below.

### Engine Options

 * `cmd=COMMAND`: Set the command to run the engine.
   
   * The current working directory will be set automatically, if a `/` is contained in `COMMAND`. For example, `cmd=../Engines/Wine2.0`, will run `./Wine2.0` from `../Engines`. If no `/` is found, the command is executed as is. Without `/`, for example `cmd= demoengine` will run `demoengine`, which only works if ` demoengine` command was in `PATH`.
   * Arguments can be provided as part of the command. For example `"cmd=../fooEngine -foo=1"`. Note that the `""` are needed here, for the command line interpreter to parse the whole string as a single token.
   
 * `name=NAME`: Set the engine's name. If omitted, the name will be taken from the `ABOUT` values sent by the engine.

 * `tc=TIMECONTROL`: Set the time control to `TIMECONTROL`. The format is `match_time/turn_time+increment` or `match_time`, where ` match_time` is the total time of this match (in seconds), ` turn_time` is the max time limit per move (in seconds), and `increment` is time increment per move (in seconds). If ` turn_time` is omitted, then it will be the same with `match_time` by default. If `increment` is omitted, then it will be set to `0` by default. If `match_time` is `0`, then there will no limit on match time and `turn_time` will be the only limitation.

 * `depth=N`: Depth limit per move. This is an extension option[^1], may not be supported by all engines.

 * `nodes=N`: Node limit per move (`N` is recommended to be have a granularity more than `1000`). This is an extension option[^1], may not be supported by all engines.

 * `maxmemory=MAXMEMORY`: Set the max memory to `MAXMEMORY` bytes. Default memory limit is 350MB (same as Gomocup) if omitted.

 * `thread=N`: Number of threads a engine can use. Default value is `1`. This is an extension option[^1], may not be supported by all engines.

 * `option.O=V`: Set a raw protocol info. Command `INFO [O] [V]` will be sent to the engine before each game starts.

   [^1]: Yixin-Board extension protocol: https://github.com/accreator/Yixin-protocol/blob/master/protocol.pdf

### Openings File Format

So far c-gomoku-cli only accept openings in plaintext format (`*.txt`). In a plaintext opening file, each line is an opening position. Currently there are two notation types for a position: `offset` and `pos`.

#### "Offset" opening notation (Gomocup opening format)

Offset opening notation is denoted by the move sequence: `<black_move>, <white_move>, <black_move>`... . Each move is separated with a comma "`,`" and a space "". The last move will not be followed by any comma or space. Each move is in format `<x-offset>,<y-offset>`, no space in between. Note that here each coordinate is the offset from the center of the board, which is different from the move coordinate of Gomocup protocol where (0,0) is up-left corner of the board. Offset value can be negative.
Assuming that a board is in size `SIZE`, the conversion between plaintext offsets `<x-offset>,<y-offset>` and Gomocup move `<x>,<y>` is:

```
HALF_SIZE = floor(SIZE / 2)
<x> = <x-offset> + HALF_SIZE
<y> = <y-offset> + HALF_SIZE
```
The following is an example position in "offset" notation:
```
8,-3, 6,-4, 5,-4, 4,-3, 2,-8, -1,-5
```
You can see more examples from [Gomocup 2020 result page](https://gomocup.org/results/gomocup-result-2020/) (download the results+openings from the link at the very bottom of that page).

#### "Pos" opening notation

Pos opening notation is denoted by a sequence of move position, with no space or any forms of delimiter in between: `<black_move><white_move><block_move>`... . Each move is in format `<x-coord><y-coord>`, while `<x-coord>` is a letter starting from "`a`", `<y-coord>` is a number starting from "`1`". Coordinate is the offset from the upper-left corner of the board. This notation is only suitable for board size less than `27`.

The following is an example position in "pos" notation:

```
b7d6e6f7h2k5
```

This notation is common among many Gomoku/Renju applications. (For example, you can acquire a pos notation text by using "getpos" command in Yixin-Board).

###Sampling (advanced)

Sampling is used to record various position and engine outputs in a game, as well as the final game result. These can be used as training data, which can be used to fit the parameters of a chess engine evaluation, otherwise known as supervised learning. Sample record is usually in binary format easy for engine to process, meanwhile a human readable and easily parsable CSV file can also be generated. Note that only game which result is not "win by time forfeit" or "win by opponent illegal move" will be recorded.

Syntax is `-sample [freq=%f] [format=csv|bin|bin_lz4] [file=%s] `. Example `-sample freq=0.25 format=csv file=out.csv `.

+ `freq` is the sampling frequency (floating point number between `0` and `1`). Defaults to `1` if omitted.
+ `file` is the name of the file where samples are written. Defaults to `sample.[csv|bin|bin.lz4]` if omitted.
+ `format` is the format in which the file is written. Defaults to `csv`, which is human readable: `Pos,Move,Result`. `Pos` is the position in "pos" notation. `Move` is the move in "pos" notation output by the engine. Values for `Result` are `0=loss`, `1=draw`, `2=win`. For binary format `bin` see the section below for details. `bin_lz4` is the same as `bin` format, but the whole file stream is compressed using [LZ4](https://github.com/lz4/lz4) to save disk space (This is suitable for huge training dataset containing millions of positions). Engines are recommended to use LZ4 "Auto Framing" API ([example](https://github.com/lz4/lz4/blob/4f0c7e45c54b7b7e42c16defb764a01129d4a0a8/examples/frameCompress.c#L171)) to decompress the training data.

#### Binary format

Binary format uses variable length encoding show below, which is easy to parse for engines. Each entry has a length of `4+ply` bytes. Position is represented by a move sequence that black plays first. Note that move sequence is always the same as the actual game records, since in Renju rule we need proper move order to find forbidden points.

```c++
struct Entry {
    uint16_t boardsize : 5;	// board size
	uint16_t ply : 9;		// current number of stones on board
    uint16_t result : 2;	// final game result: 0=loss, 1=draw, 2=win
    uint16_t move;			// move output by the engine
    uint16_t position[ply];	// move sequence that representing a position
};
```

Each move is represented by a 16bit unsigned integer, constructed using two index `x` and `y` using `uint16_t move = (x << 5) | y;`. Below is a code snippet that does transform between move and coordinate.

```c
uint16_t Move(int x, int y)    { return (x << 5) | y; }
int      CoordX(uint16_t move) { return (move >> 5); }
int      CoordY(uint16_t move) { return (move & ((1 << 5) - 1)); }
```


## Acknowledgement

Thanks to lucasart for developing the *c-chess-cli* project. His prior work provides a perfect starting point for the development of c-gomoku-cli.
