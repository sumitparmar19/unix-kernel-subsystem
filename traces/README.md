# Kernel traces

Sample kernel event traces, for https://ui.perfetto.dev (drag the `.json` in).
Produced by running Pintos with the `-trace` kernel option and converting the

| File | What it shows |
|---|---|
| `priority-donation.json` | `priority-donate-chain`: a low-priority holder receiving donated priority up a chain of locks, then each thread finishing in priority order. Look for the `donates priority N` instants and the `lock wait` slices. |
| `pipeline.json` | `minish cat echo \| upper \| wc`: three user processes connected by two pipes. Look for the `running` slices switching between processes and the pipe activity. |

Regenerate, e.g.:

```sh
docker run --rm -v "$PWD:/pintos" --entrypoint bash pintos-kernel -c \
  'cd /pintos-src/threads/build && pintos -v -k -T 60 --qemu -- -q -trace \
     run priority-donate-chain' > traces/priority-donation.log
python3 scripts/trace2perfetto.py traces/priority-donation.log -o traces/priority-donation.json
```
