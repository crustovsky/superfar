#!/bin/sh
# README screenshots. Run as: unshare -Urm tools/screenshots.sh
# A user and mount namespace with a fresh tmpfs on /tmp, so the tree shows
# only the demo folder below. Needs build/shotcfg: a copy of your qt6ct
# config next to a superfar.ini whose [window] size is 1400x900, kept
# pristine at build/shots/pristine.ini. Output lands in docs/screenshots.
# ONLY=name renders that one shot.
set -e
cd "$(dirname "$0")/.."
mount -t tmpfs none /tmp
D=/tmp/demo
mkdir -p $D/Documents $D/Pictures $D/Music $D/Downloads $D/Projects/site $D/Backup/site
printf '# Notes\n\n- call the plumber\n- renew passport\n- book the ferry\n' > $D/Documents/notes.md
printf 'month,rent,food,travel\nJan,900,310,80\nFeb,900,290,120\nMar,900,330,60\n' > $D/Documents/budget.csv
printf 'Quarterly report\n\nRevenue grew modestly while costs stayed flat.\n' > $D/Documents/report.txt
printf 'Ideas\n\nA dual-pane file manager that stays out of the way.\n' > $D/Documents/ideas.md
for n in 1 2 3 4; do cp data/icons/hicolor/256x256/apps/superfar.png $D/Pictures/IMG_000$n.JPG; done
cp data/icons/hicolor/256x256/apps/superfar.png $D/Pictures/logo.png
for t in "01 Overture" "02 Allegro" "03 Adagio"; do head -c 3000 /dev/urandom > "$D/Music/$t.ogg"; done
head -c 2048 /dev/urandom > $D/Downloads/firmware.bin
cp dist/superfar-v*-linux-x86_64.tar.gz $D/Downloads/ 2>/dev/null || true
(cd $D && bsdtar -cf Downloads/photos.zip Pictures)
printf '<!doctype html>\n<title>Site</title>\n<h1>Hello</h1>\n' > $D/Projects/site/index.html
printf 'body { margin: 0; font: 16px sans-serif; }\n' > $D/Projects/site/style.css
printf 'console.log("hi");\n' > $D/Projects/site/app.js
printf 'User-agent: *\nAllow: /\n' > $D/Projects/site/robots.txt
cp $D/Projects/site/index.html $D/Backup/site/
printf 'body { margin: 0; }\n' > $D/Backup/site/style.css
printf 'old notes\n' > $D/Backup/site/TODO.txt
touch -d '2026-06-01 10:00' $D/Backup/site/style.css $D/Backup/site/TODO.txt
printf 'Welcome to the demo folder.\n' > $D/README.txt
cp tests/data/pages.pdf $D/Documents/manual.pdf
mkdir -p $D/Videos
ffmpeg -nostdin -v error -f lavfi -i testsrc2=duration=30:size=1280x720:rate=25 -c:v mpeg4 -q:v 4 $D/Videos/pattern.mp4 2>/dev/null || true
# Modes worth a Permissions column: setgid, sticky, a script, a private file.
mkdir -p $D/Projects/shared $D/Projects/scratch
printf '#!/bin/sh\nrsync -a site/ host:/srv/www/\n' > $D/Projects/deploy.sh
printf '# Projects\n\n- site: the landing page\n' > $D/Projects/notes.md
printf 'API_TOKEN=not-a-real-one\n' > $D/Projects/secrets.env
chmod 2775 $D/Projects/shared; chmod 1777 $D/Projects/scratch
chmod 755 $D/Projects/deploy.sh; chmod 600 $D/Projects/secrets.env
touch -d '2026-10-12 09:30' $D/Projects/* $D/Documents/* $D/Pictures/* $D/Music/* $D/Downloads/* $D/Projects/site/* $D/Videos/* $D/README.txt
export XDG_CONFIG_HOME=$PWD/build/shotcfg XDG_STATE_HOME=$PWD/build/shotstate QT_QPA_PLATFORM=offscreen
# A state that knows the version, so the about note stays out of the shots.
mkdir -p build/shotstate/superfar
printf '[about]\nlastVersion=%s\n' "$(build/src/app/superfar --version | cut -d' ' -f2)" > build/shotstate/superfar/state.ini
# The Owner column names accounts, and the shots should not name
# yours. This namespace is root (your id mapped to 0); a second one
# inside maps that to 1000, which a stand-in passwd calls sam. Shots
# that show owners run in it (AS=...), with CFG appended to the config.
printf 'root:x:0:0::/root:/bin/sh\nsam:x:1000:1000::/tmp:/bin/sh\n' > /tmp/passwd
printf 'root:x:0:\nsam:x:1000:\n' > /tmp/group
mount --bind /tmp/passwd /etc/passwd
mount --bind /tmp/group /etc/group
AS_SAM="unshare -U --map-user=1000 --map-group=1000"
COLS='\n[columns/left]\norder=name,permissions,owner,modified\nhidden=size,items\n\n[keys]\nColumns=Ctrl+K\n'
shot() {
    name=$1; keys=$2; ms=$3; shift 3
    [ -z "${ONLY:-}" ] || [ "$ONLY" = "$name" ] || return 0
    cp build/shots/pristine.ini build/shotcfg/superfar/superfar.ini
    [ -z "${CFG:-}" ] || printf '%b' "$CFG" >> build/shotcfg/superfar/superfar.ini
    SUPERFAR_GRAB=docs/screenshots/$name.png SUPERFAR_GRAB_MS=$ms SUPERFAR_DEV_KEYS="$keys" \
        ${AS:-} build/src/app/superfar "$@" > build/shots/$name.log 2>&1 || echo "$name failed"
    echo "$name: $(grep -v '^bench' build/shots/$name.log | head -1)"
}
shot dual-pane "Down;Down" 3500 /usr/share $D
shot folder-sizes "Ctrl+F3" 7000 /usr/share $D
shot preview "Ctrl+Q" 5000 $D/Pictures $D
shot viewer "F3" 3500 $D/Downloads $D
shot pdf-pages "Down;Down;Ctrl+Q;Ctrl+PgDown" 5000 $D/Documents $D
shot video "Ctrl+Q;Ctrl+P;Ctrl+PgDown" 6000 $D/Videos $D
shot help "F1" 3500 $D $D/Documents
shot go-to "Ctrl+G" 3500 $D $D/Documents
shot multi-rename "Ctrl+A;Shift+F2;Ctrl+A;I;M;G;_;*;Tab;Ctrl+A;H;O;L;I;D;A;Y;_;{;N;}" 12000 $D/Pictures $D
shot folder-sync "Shift+F5" 5000 $D/Projects/site $D/Backup/site
shot archive "Down;Return;Return" 5000 $D/Downloads $D
shot find "Alt+F7;G;T;K;Return" 7000 /usr/share/icons/hicolor $D
shot thumbnails "Ctrl+F4" 6000 /usr/share/icons/hicolor/256x256/apps $D
CFG=$COLS AS=$AS_SAM shot permissions "Down;Down;Down;F2;Tab" 4500 $D/Projects $D
CFG=$COLS AS=$AS_SAM shot columns "Ctrl+K;Down;Down" 4500 $D/Projects $D
CFG=$COLS AS=$AS_SAM shot properties "Ctrl+A;Alt+F2;Tab" 5000 $D/Projects $D
