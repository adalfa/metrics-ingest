# perfparse

A lightweight C daemon that reads pipe-delimited performance metrics from stdin and stores them in Redis lists.

## Input format

Each line must contain exactly three `|`-separated fields:

```
<key>|<timestamp>|<value>
```

Fields are validated before storage:
- `key` — only characters `[A-Za-z0-9_.-]` are accepted; invalid keys are skipped with a WARN log
- `timestamp` — must be a non-negative integer; invalid timestamps are skipped with a WARN log
- `value` — stored as-is (string, up to 511 bytes)

Example:

```
cpu|1700000000|42
mem|1700000001|1024
disk.io|1700000002|88
```

## Redis data model

For each record, two Redis lists are updated:

| Key | Content | Max length |
|-----|---------|------------|
| `<key>` | values (newest first) | 1000 |
| `<key>.time` | timestamps (newest first) | 1000 |

Both lists are trimmed to the last 1000 entries via `LTRIM` after each push.

## Dependencies

- [hiredis](https://github.com/redis/hiredis) — C client for Redis

The Makefile expects the hiredis source tree at `/usr/local/src/antirez-hiredis-d5d8843` with `libhiredis.a` already built. Adjust the `HIREDIS` variable if your path differs.

## Build

```sh
make
```

## Usage

```sh
./perfparse [-v] [host [port]]
```

| Argument | Default | Description |
|----------|---------|-------------|
| `host` | `172.20.1.23` | Redis host |
| `port` | `6379` | Redis port (must be 1–65535) |
| `-v` | off | Enable DEBUG logging |

Feed records via stdin:

```sh
echo "cpu|$(date +%s)|42" | ./perfparse 127.0.0.1 6379
```

Or pipe a continuous stream:

```sh
my-metrics-collector | ./perfparse 127.0.0.1 6379
```

Exits cleanly on EOF.

## Logging

All log output goes to stderr. Default level is INFO.

```
[INFO] Connected to Redis at 127.0.0.1:6379
[WARN] Invalid input (got 1 tokens, expected key|timestamp|value): badline
[INFO] EOF — shutting down
```

Pass `-v` to see per-record DEBUG traces.

## Reliability

- Connects with a 1.5 s timeout.
- On connection loss, retries up to 5 times with exponential back-off (1 s → 2 s → 4 s → 8 s → 16 s).
- Each Redis command is retried once after an automatic reconnect before giving up.

## Tests

Requires `redis-server` and `redis-cli` (checked in PATH and under `/usr/local/src`).

```sh
make test
```

Covers: happy path, multi-record ordering, malformed input (skipped, no crash), empty input (clean exit).
