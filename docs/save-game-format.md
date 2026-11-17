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
