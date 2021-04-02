# c-gomoku-cli

c-gomoku-cli is a command line interface for gomoku/renju engines that support [Gomocup protocol](http://petr.lastovicka.sweb.cz/protocl2en.htm). It is a derived project from [c-chess-cli]( https://github.com/lucasart/c-chess-cli), originally authored by Lucas Braesch (lucasart).

## Compiling from the Source

// TODO

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
 * `rule RULE`: Set the game rule with Gomocup rule code `RULE`.
   * `RULE=0`: Play with gomoku rule and winner wins by five or longer connection.
   * `RULE=1`: Play with gomoku rule but winner only wins by exact-5 connection (longer connections will be ignored).
   * `RULE=4`: Play with renju rule.
   * No other rule code is acceptable.
 * `boardsize SIZE`: Set the board size to `SIZE` X `SIZE`. Valid `SIZE` range is `[5..32]`. 
 * `games N`: Play N games per encounter (default value 1). This value should be set to an even number in tournaments with more than two players to make sure that each player plays an equal number of games with black and white colors.
 * `rounds N`: Multiply the number of rounds to play by `N` (default value 1). This only makes sense to use for tournaments with more than 2 engines.
 * `gauntlet`: Play a gauntlet tournament (first engine against the others). The default is to play a round-robin (plays all pairs).
   * with `n=2` engines, both gauntlet and round-robin just play the number of `-games` specified.
   * gauntlet for `n>2`: `G(e1, ..., en) = G(e1, e2) + G(e2, ..., en)`. There are `n-1` pairs.
   * round-robin for `n>2`: `RR(e1, ..., en) = G(e1, ..., en) + RR(e2, ..., en)`. There are `n(n-1)/2` pairs.
   * using `-rounds` repeats the tournament `-rounds` times. The number of games played for each pair is therefore `-games * -rounds`.
 * `repeat`: Repeat each opening twice, with each engine playing both sides. 
 * `sprt [elo0=E0] elo1=E1 [alpha=A] [beta=B]`: Performs a Sequential Probability Ratio Test for `H1: elo=E1` vs `H0: elo=E0`, where `alpha` is the type I error probability (false positive), and `beta` is type II error probability (false negative). Default values are `elo0=0`, and `alpha=beta=0.05`. This can only be used in matches between two players.
 * `log`: Write all I/O communication with engines to file(s). This produces `c-gomoku-cli.id.log`, where `id` is the thread id (range `1..concurrency`). Note that all communications (including error messages) starting with `[id]` mean within the context of thread number `id`, which tells you which log file to inspect (id = 0 is the main thread, which does not product a log file, but simply writes to stdout).
 * `debug`: 
 * `openings file=FILE [order=ORDER] [srand=N]`:
   * Read opening positions from `FILE`, in PLAINTEXT format. See section below about what is PLAINTEXT format.
   * `order` can be `random` or `sequential` (default value).
   * `srand` sets the seed of the random number generator to `N`. The default value `N=0` will set the seed automatically to an unpredictable number. Any non-zero number will generate a unique, reproducible random sequence.
 * `pgn FILE`: Save a dummy game to `FILE`, in PGN format. PGN format is for chess games. We replace the moves with some random chess moves but only keep the game result and player names. This dummy PGN file can be input by BaysianELO to compute ELO scores.
 * `sgf FILE`: Save a game to `FILE`, in SGF format.

### Engine Options

 * `cmd=COMMAND`: Set the command to run the engine.
   * The current working directory will be set automatically, if a `/` is contained in `COMMAND`. For example, `cmd=../Engines/Wine2.0`, will run `./Wine2.0` from `../Engines`. If no `/` is found, the command is executed as is. Without `/`, for example `cmd= demoengine` will run `demoengine`, which only works if ` demoengine` command was in `PATH`.
   * Arguments can be provided as part of the command. For example `"cmd=../fooEngine -foo=1"`. Note that the `""` are needed here, for the command line interpreter to parse the whole string as a single token.
 * `name=NAME`: Set the engine's name. If omitted, the name will be taken from the `ABOUT` values sent by the engine.
 * `tc=TIMECONTROL`: Set the time control to `TIMECONTROL`. The format is `match_time/turn_time` or `match_time`, where ` match_time` is the total time of this match (in seconds), ` turn_time` is the max time limit per move (in seconds). If ` turn_time` is omitted, then it will be the same with `match_time` by default.

### Openings File Format

// TODO

## Acknowledgement

Thanks to lucasart for developing the *c-chess-cli* project. His prior work provides a perfect starting point for the development of c-gomoku-cli.
