# Password-KDF benchmark

This is an optional, disposable benchmark, not an authentication implementation
or an encryption-cipher benchmark. It never reads shadow files or real passwords.
Inputs and salts are fixed synthetic test data; production salts must be unique
and generated with a cryptographic random source.

`build-password-bench.sh` builds a static big-endian MIPS executable using the
pinned upstream Argon2 reference implementation, the BSP's staged libxcrypt
(bcrypt), and OpenSSL (PBKDF2-HMAC-SHA256). Libraries must already be cross-built.
The benchmark includes known-answer self-tests. Build artifacts remain ignored.

Transfer `build/crypto-bench/password-bench` and `run-password-bench.sh` into a
private `/run/password-bench` directory on the board. Name the executable
`password-bench`, and run the script there. It deliberately does not install
anything into the firmware or alter existing credentials.

Profiles run serially at reduced scheduler priority. Each records three trials
with wall/user/system time, process CPU utilization, total/per-VPE busy time,
peak process RSS, sampled minimum available memory, SoC temperature, page faults
and context switches. Argon2 also compares lane counts and two simultaneous
requests. The latter is a concurrency measurement, not per-login latency.

Conventions and limits:

- One fully busy logical VPE is 100% process CPU; four VPEs can reach 400%.
  Machine-busy percentages use 0–100% across the system and include other work.
- RSS includes the benchmark and library overhead, not only Argon2's workspace.
- `ru_maxrss` is the process high-water mark, including earlier trials of that
  profile. Separate profiles execute in separate processes.
- Available memory/temperature are sampled at 250 ms intervals and may miss
  short transients. Temperature is read from the board's identified SoC sensor.
- A profile requires its requested workspace plus 32 MiB available memory.
  The sampler aborts below 24 MiB or at a conservative 78 C test guard. This is
  not a claim about the chip's rated maximum operating temperature.
- Address space is capped at 160 MiB; each profile has a 90-second timeout.
- The 100,000-iteration PBKDF2 profile is a timing reference, not the proposed
  production setting. Security work factors must not be selected by speed alone.
- A provider inventory and AF_ALG socket probe distinguish the currently exposed
  kernel interface from these CPU-based library measurements. This does not
  benchmark the wireless chip's separate CCMP/GCMP encryption engine or prove
  that an unported SoC crypto peripheral is absent.
- Ping responsiveness during the test is not a forwarding-throughput benchmark.
  No electrical power draw is measured without a real power sensor/meter.

Fetch the private result files, then summarize them with:

```sh
python3 tools/summarize-password-bench.py "$PRIVATE_RESULTS_JSONL"
```

The current Dropbear password-verification path uses libxcrypt's `crypt()`.
Argon2id in a new web-login service needs an explicit verifier integration; an
Argon2 PHC string is not a drop-in replacement for every existing shadow backend.
Do not change the SSH hash format based solely on benchmark results.
