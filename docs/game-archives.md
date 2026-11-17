# The names from the game archives

Wheelstar reads the names of cities, countries, companies and cargoes from the game's own archives through
[istar-truckdata](https://github.com/Istarlabs/istar-truckdata); the archive format and what is read from it are in its
[game-archives.md](https://github.com/Istarlabs/istar-truckdata/blob/master/docs/game-archives.md). What follows is how Wheelstar keeps them.

## In the database

| Table | Content |
|---|---|
| `game_locale` | `game`, `kind`, `locale_id`, `translations`. One row per thing; `translations` is `{"native": "Wrocław", "de_de": "Breslau", "pl_pl": "Wrocław", …}`, `jsonb` on PostgreSQL, JSON text on SQLite |
| `game_locale_sources` | `game`, `fingerprint`. What the archives were when the names were read (their names, sizes and times, hashed) |
| `game_names` (view) | `game`, `kind`, `locale_id`, `language`, `name`. The translations as rows (`jsonb_each_text`, `json_each`) |

At start, with the plugin installed, the application compares the fingerprint of each installed game's archives with
the stored one. Only after an update of the game or a DLC does it read the names again, about half a second, and
replace the game's rows in one transaction.

`SessionStore::jobs()` asks the database for the names of a job's cities, companies and cargo. The rules, in order:

1. `game_names` in the application's language, exactly (`de_de`) or any region of it (`de_…`)
2. The name the telemetry recorded with the job
3. The thing's own name from its definition (`native`)

Where none is known, the application makes a name of the id, as before.
