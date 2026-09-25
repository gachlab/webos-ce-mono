#!/bin/bash
# Product-shaped engine A/B: real WebAppMgr card stack + expensive HTTPS pages.
#   tools/engine-stack-bench.sh wpe|qtwebengine [n]
#
# Requires: assembled rootfs, graphical session env (Wayland), /etc/palm mountpoint.
# Tears the session down on exit. Samples WebAppMgr process-tree RSS while cards live.

set -euo pipefail
R="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE="${1:?usage: $0 wpe|qtwebengine [n]}"
N="${2:-8}"
ROOTFS="${WEBOS_ROOTFS:-$R/build/rootfs}"
LOGDIR="${WEBOS_LOGDIR:-/tmp/webos-stack-bench-$ENGINE}"
STAGING="${WEBOS_STAGING:-$R/build/staging}"

URLS=(
  "https://www.youtube.com/"
  "https://www.youtube.com/watch?v=jNQXAC9IVRw"
  "https://www.cnn.com/"
  "https://www.reddit.com/"
  "https://www.nytimes.com/"
  "https://maps.google.com/"
  "https://www.twitch.tv/"
  "https://www.amazon.com/"
  "https://www.facebook.com/"
  "https://www.instagram.com/"
  "https://www.wikipedia.org/wiki/WebOS"
  "https://github.com/"
  "https://stackoverflow.com/"
  "https://www.bbc.com/"
  "https://www.netflix.com/"
  "https://www.spotify.com/"
)

tree_rss_kb() {
  local root=$1 sum=0 pid
  # Descendants of root via ps --forest is brittle; walk /proc ppid chains.
  local pids=("$root")
  local grew=1
  while [ "$grew" = 1 ]; do
    grew=0
    for cand in /proc/[0-9]*; do
      pid=${cand#/proc/}
      [ -r "$cand/stat" ] || continue
      ppid=$(awk '{print $4}' "$cand/stat" 2>/dev/null || echo)
      for k in "${pids[@]}"; do
        if [ "$ppid" = "$k" ]; then
          local known=0
          for x in "${pids[@]}"; do [ "$x" = "$pid" ] && known=1 && break; done
          if [ "$known" = 0 ]; then
            pids+=("$pid")
            grew=1
          fi
          break
        fi
      done
    done
  done
  for pid in "${pids[@]}"; do
    if [ -r "/proc/$pid/status" ]; then
      kb=$(awk '/^VmRSS:/{print $2; exit}' "/proc/$pid/status")
      sum=$((sum + ${kb:-0}))
    fi
  done
  echo "$sum"
  echo "${#pids[@]}" >"$LOGDIR/last-procs.txt"
}

say() { printf '%s\n' "$*" >&2; }

mkdir -p "$LOGDIR"
export XDG_RUNTIME_DIR="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
export WAYLAND_DISPLAY="${WAYLAND_DISPLAY:-wayland-0}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-wayland}"
export WEBOS_ROOTFS="$ROOTFS"
export WEBOS_LOGDIR="$LOGDIR"

# Install the right WebAppMgr into staging + rootfs copy.
case "$ENGINE" in
  wpe)
    say "building WebAppMgr (WPE)…"
    cmake -S "$R/components/webappmanager" -B "$R/build/webappmanager-wpe" \
      -DCMAKE_BUILD_TYPE=Release -DWEBOS_WEB_ENGINE=wpe \
      -DCMAKE_INSTALL_PREFIX="$STAGING" >/dev/null
    cmake --build "$R/build/webappmanager-wpe" --target install -j"$(nproc)" >/dev/null
    ;;
  qtwebengine)
    say "building WebAppMgr (QtWebEngine)…"
    cmake -S "$R/components/webappmanager" -B "$R/build/webappmanager-qt" \
      -DCMAKE_BUILD_TYPE=Release -DWEBOS_WEB_ENGINE=qtwebengine \
      -DCMAKE_INSTALL_PREFIX="$STAGING" >/dev/null
    cmake --build "$R/build/webappmanager-qt" --target install -j"$(nproc)" >/dev/null
    ;;
  *) say "engine must be wpe or qtwebengine"; exit 2 ;;
esac

WEBOS_STAGING="$STAGING" "$R/tools/assemble-rootfs.sh" >"$LOGDIR/assemble.log" 2>&1

# Distinct apps so ApplicationManager cannot relaunch a single browser card.
# Each card is a one-liner stage that navigates to a heavy HTTPS page.
BENCH_DIR="$ROOTFS/usr/palm/applications"
for i in $(seq 0 $((N - 1))); do
  id="com.gachlab.bench.heavy$i"
  url="${URLS[$((i % ${#URLS[@]}))]}"
  app="$BENCH_DIR/$id"
  mkdir -p "$app"
  cat >"$app/appinfo.json" <<JSON
{"id":"$id","version":"1.0.0","vendor":"Gachlab","type":"web","main":"index.html","title":"Heavy $i","icon":"icon.png","uiRevision":2}
JSON
  # 1x1 png
  printf '\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x02\x00\x00\x00\x90wS\xde\x00\x00\x00\x0cIDATx\x9cc\xf8\x0f\x00\x00\x01\x01\x00\x05\x18\xd8N\x00\x00\x00\x00IEND\xaeB`\x82' >"$app/icon.png"
  cat >"$app/index.html" <<HTML
<!doctype html><html><head><meta charset="utf-8"><title>heavy-$i</title></head>
<body style="margin:0;background:#111;color:#fff;font:20px sans-serif">
<div id="s">loading…</div>
<script>
  if (window.PalmSystem) PalmSystem.stageReady();
  var u = "$url";
  document.getElementById("s").textContent = u;
  // Full navigation — this is the expensive page, not a chrome shell around it.
  location.replace(u);
</script>
</body></html>
HTML
done

ldd "$ROOTFS/usr/lib/luna/WebAppMgr" | grep -iE 'wpe|WebEngineCore' | head -3 | tee "$LOGDIR/wam-libs.txt" >&2

"$R/tools/webos-session.sh" --down >/dev/null 2>&1 || true
rm -f "$LOGDIR/session.out"
nohup "$R/tools/webos-session.sh" >"$LOGDIR/session.out" 2>&1 &
spid=$!

up=0
for i in $(seq 1 90); do
  if pgrep -x LunaSysMgr >/dev/null && pgrep -x WebAppMgr >/dev/null; then
    up=1
    say "session up at ${i}s"
    break
  fi
  if ! kill -0 "$spid" 2>/dev/null; then
    say "session died early"; tail -40 "$LOGDIR/session.out" >&2 || true
    exit 1
  fi
  sleep 1
done
[ "$up" = 1 ] || { say "timeout waiting for session"; exit 1; }
sleep 3

LS="$STAGING/usr/bin/luna-send"
[ -x "$LS" ] || LS="$ROOTFS/usr/bin/luna-send"
[ -x "$LS" ] || LS="$(command -v luna-send)"
export LD_LIBRARY_PATH="$STAGING/lib:$STAGING/usr/lib:${LD_LIBRARY_PATH:-}"

wam=$(pgrep -nx WebAppMgr)
say "WebAppMgr pid=$wam — launching $N browser cards"

peak=0
ok=0
fail=0
start=$(date +%s%3N)

for i in $(seq 0 $((N - 1))); do
  id="com.gachlab.bench.heavy$i"
  url="${URLS[$((i % ${#URLS[@]}))]}"
  payload=$(printf '{"id":"%s"}' "$id")
  say "  launch $id -> $url"
  if "$LS" -n 1 palm://com.palm.applicationManager/launch "$payload" >"$LOGDIR/launch-$i.out" 2>&1; then
    ok=$((ok + 1))
  else
    fail=$((fail + 1))
    say "  launch failed: $(cat "$LOGDIR/launch-$i.out")"
  fi
  # Heavy pages need wall-clock to pull JS/media before the next card.
  sleep 8
  if pgrep -x WebAppMgr >/dev/null; then
    wam=$(pgrep -nx WebAppMgr)
    rss=$(tree_rss_kb "$wam")
    [ "$rss" -gt "$peak" ] && peak=$rss
    say "  after $((i + 1)): tree_rss_mb=$((rss / 1024)) procs=$(cat "$LOGDIR/last-procs.txt")"
  else
    say "  WebAppMgr died after launch $i"
    fail=$((fail + 1))
    break
  fi
done

# Settle: keep sampling for 30s while heavy pages finish loading.
say "settling 60s…"
for _ in $(seq 1 30); do
  sleep 2
  if pgrep -x WebAppMgr >/dev/null; then
    wam=$(pgrep -nx WebAppMgr)
    rss=$(tree_rss_kb "$wam")
    [ "$rss" -gt "$peak" ] && peak=$rss
  else
    break
  fi
done

end=$(date +%s%3N)
wall=$((end - start))
end_rss=0
procs=0
if pgrep -x WebAppMgr >/dev/null; then
  wam=$(pgrep -nx WebAppMgr)
  end_rss=$(tree_rss_kb "$wam")
  procs=$(cat "$LOGDIR/last-procs.txt")
fi

cards=$(grep -c 'APP START appid: com.gachlab.bench.heavy' /tmp/webos/WebAppMgr.log 2>/dev/null || echo 0)
say "WebAppMgr APP START heavy cards seen: $cards"
printf 'engine=%s n=%d launch_ok=%d launch_fail=%d cards_started=%s wall_ms=%s peak_tree_rss_mb=%.1f end_tree_rss_mb=%.1f procs=%s\n' \
  "$ENGINE" "$N" "$ok" "$fail" "$cards" "$wall" \
  "$(echo "$peak" | awk '{printf "%.1f", $1/1024}')" \
  "$(echo "$end_rss" | awk '{printf "%.1f", $1/1024}')" \
  "$procs" | tee "$LOGDIR/result.txt"

"$R/tools/webos-session.sh" --down >/dev/null 2>&1 || true
wait "$spid" 2>/dev/null || true
