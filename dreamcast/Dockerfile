#
# Dreamcast Linux Dockerfile
# Author: Anders Evenrud <andersevenrud@gmail.com>
#
FROM debian:10

RUN mkdir -p /usr/src/dreamcast

# Dependencies
RUN apt-get update

RUN apt-get install -y \
      genisoimage texinfo bc flex wget gawk bison python3 \
      build-essential gcc-multilib \
      libc-dev libmpc-dev libncurses5-dev rsync

RUN rm -rf /var/lib/apt/lists/*

# Sources
COPY build-dreamcast.sh \
  kernel.config \
  busybox.config \
  sh-boot-20010831-1455.diff \
  sh-boot-20010831-1455-sh4.diff \
  hello sh-boot-20010831-1455.tar.gz \
  /usr/src/
WORKDIR /usr/src

# Runtime
CMD ["./build-dreamcast.sh"]
