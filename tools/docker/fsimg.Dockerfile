# Werkzeuge für tools/make_disk_image.py (M16): FAT32, NTFS und ext4 als Partitions-Images.
# Nur für lokale Smokes; Images werden nie eingecheckt.
FROM debian:stable-slim
RUN apt-get update -qq \
 && apt-get install -y -qq --no-install-recommends dosfstools mtools ntfs-3g e2fsprogs >/dev/null \
 && rm -rf /var/lib/apt/lists/*
