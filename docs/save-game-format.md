# Save games

Wheelstar reads the save games through [istar-truckdata](https://github.com/Istarlabs/istar-truckdata); the format,
byte by byte, and what is read from which unit are in its
[save-game-format.md](https://github.com/Istarlabs/istar-truckdata/blob/master/docs/save-game-format.md). `src/savegame/saveimport.cpp` turns its jobs into Wheelstar's job
records. What follows is Wheelstar's own.

## The telemetry and the save game together

A job the telemetry plugin recorded and the same job from the save game are one row. The database refuses a second
row with the same game, game time at the end, companies and cargo (a bus job has no companies and no cargo: its end
alone tells it apart). `newDeliveries()` also matches jobs whose game time is up to 60 in-game minutes apart, since
the plugin and the save do not always note the same minute.

## The repairs

The player's own profit log books every repair as an entry of its own, with upkeep and no kilometres - and the game
keeps that log for a few in-game days only. Whenever Wheelstar reads the save game (the career and the company page
do), `repairsFrom()` takes the repairs out of it and the database keeps them:

| Table | Content |
|---|---|
| `repairs` | `game`, `game_day` (the first day being day 1), `amount`, `occurrence`; unique over all four |

The game books a repair by day and amount only. `occurrence` counts the equal bills of one day, so two repairs of the
same price on one day stay two rows, while the same bill read again from the next save is the same row. The company
page's *Finances* adds them up since the first one kept. A repair the game forgot before Wheelstar read the save is
not there; reading it every few in-game days misses none. Like the jobs, the rows are per game, not per profile.
