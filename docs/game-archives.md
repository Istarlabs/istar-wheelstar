# The game archives and the names in them

A job from the save game only has the game's ids: `wroclaw`, `polarislines`, `sandwch_pnls`. What the game shows
for them, "Breslau", "Polaris Lines", "Sandwichpaneele" in German, is in its own archives. `src/gamedata/` reads it
from there, and the database matches it to the jobs. As for the save game (see [save-game-format.md](save-game-format.md)),
SCS publishes no specification. The layout follows the community's
[Archive::SCS::HashFS2](https://metacpan.org/pod/Archive::SCS::HashFS2) and was checked against ETS2 1.5x.

## Where

`findGames()` (`savegamelocator.cpp`) looks for `steamapps/common/Euro Truck Simulator 2` and
`steamapps/common/American Truck Simulator` with a `def.scs` in every Steam library. Every `*.scs` in that folder is
an archive: `def.scs` and `locale.scs` of the base game, `dlc_*.scs` of the DLCs, and others (`base.scs`, models,
textures) that are opened but have nothing read from them.

## HashFS version 2

All numbers little-endian.

### The header, 52 bytes

| Offset | Size | Content |
|---|---|---|
| 0 | 4 | `SCS#` |
| 4 | 2 | Version, 2 |
| 6 | 2 | Salt, 0 |
| 8 | 4 | Hash method, `CITY` |
| 12 | 4 | Number of entries |
| 16 | 4 | Size of the entry table, compressed |
| 20 | 4 | Size of the descriptor table, inflated, in 4-byte words |
| 24 | 4 | Size of the descriptor table, compressed |
| 28 | 8 | Where the entry table starts |
| 36 | 8 | Where the descriptor table starts |
| 44 | 8 | A signature's offset, not used here |

Both tables are zlib streams. Their inflated sizes are known: 16 bytes per entry, and the words of the descriptor table.

### An entry, 16 bytes

| Offset | Size | Content |
|---|---|---|
| 0 | 8 | CityHash64 of the path |
| 8 | 4 | Its first descriptor, a word index into the descriptor table |
| 12 | 2 | How many descriptors it has |
| 14 | 2 | Flags: `0x1` a directory |

There are no names in the archive, only their hashes. A file is found by hashing its path; what a directory holds is
in its listing (below).

### Descriptors

A descriptor is one word: 3 bytes of offset (a word index into the descriptor table again) and a byte of kind. The
kind with `0x80` set is the data part, whose 16 bytes at that offset say where the data is:

| Offset | Size | Content |
|---|---|---|
| 0 | 3 | Size as stored |
| 3 | 1 | Flags: `& 0xF0 == 0x10` is zlib |
| 4 | 3 | Size inflated |
| 7 | 1 | Flags, not used here |
| 8 | 4 | Not used here |
| 12 | 4 | Where the data is, in units of 16 bytes |

Descriptors of other kinds belong to textures (their headers) and are skipped. Other packings than zlib (GDeflate, for
textures) are not inflated; nothing read here is packed that way.

### Directory listings

A directory's data is a `u32` count, a byte per name with its length, then the names back to back. A name with a
leading `/` is a subdirectory: the listing of `def` holds `/city`, `/cargo`, …, `city.sii`, ….

### Paths and the hash

A path has no leading slash and no trailing one: `def/city/berlin.sui`, the root is the empty path. The hash is
CityHash64 **as of CityHash 1.0.3**. Version 1.1 changed the hash of strings up to 64 bytes. A current CityHash finds
the root (the empty string hashes alike) and almost nothing else. `cityhash.cpp` is a port of 1.0.3. The tests check
one path of each of its length classes against hashes the real archive finds.

## What is read

### Definitions

Every archive may define things in `def/<kind>/<id>.sui`. These are SII text fragments without the
`SiiNunit { }` around them. `def/<kind>.sii` includes them, and `namesFrom()` wraps them itself. The braces of a unit
are on a line of their own, and values may carry a `#` comment; `parseText()` reads both.

| Kind | Unit | Its own name | Key of the translation |
|---|---|---|---|
| `city` | `city_data : city.<id>` | `city_name: "Wrocław"` | `city_name_localized: "@@wroclaw@@"` |
| `country` | `country_data : country.data.<id>` | `name: "Polska"` | `name_localized: "@@poland@@"` |
| `company` | `company_permanent : company.permanent.<id>` | `name: "Polaris Lines"` | None; company names are not translated |
| `cargo` | `cargo_data : cargo.<id>` | None | `name: "@@cn_sandwich_panels@@"` |

The map DLCs bring their cities and companies (`dlc_east.scs` Breslau, Danzig, …). When an archive defines a thing
again, the later one wins: the base game first, then the DLCs in the order of their names.

### Translations

`locale.scs` has a folder per language, `locale/de_de/`, `locale/pl_pl/` … (43 in ETS2 1.5x). In each,
`localization.sui` is the inside of a `localization_db` unit: pairs of `key[]: "wroclaw"` and `val[]: "Breslau"`. The
DLCs bring no translations of their own; their cities are in the base game's.

Like the save game, the files may be `ScsC`-encrypted. `decrypt()` takes care of that before they are parsed.

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
