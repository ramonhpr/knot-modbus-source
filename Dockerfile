FROM solita/ubuntu-systemd:latest

# build arguments
ARG LIBELL_VERSION=0.17

# install dependencies
RUN apt-get update \
 && apt-get install -y \
       curl wget apt-transport-https \
       pkg-config autoconf automake libtool

RUN apt-get install -y \
      dbus libdbus-1-dev docker.io

WORKDIR /usr/local

# install libmodbus depencency
RUN mkdir -p /usr/local/libmodbus
RUN wget -q -O- https://libmodbus.org/releases/libmodbus-3.1.4.tar.gz | tar xz -C /usr/local/libmodbus --strip-components=1
RUN cd libmodbus && ./configure --prefix=/usr && make install


# install libell
RUN mkdir -p /usr/local/ell
RUN wget -q -O- https://mirrors.edge.kernel.org/pub/linux/libs/ell/ell-$LIBELL_VERSION.tar.gz|tar xz -C /usr/local/ell --strip-components=1
RUN cd ell && ./configure --prefix=/usr && make install

# copy files to source
# TODO: just export what it needs
COPY ./ ./

# slaves conf file
#COPY src/slaves.conf /etc/

# dbus conf files
COPY ./src/modbus.conf /etc/dbus-1/system.d
COPY ./docker/system.conf /usr/share/dbus-1/system.conf
RUN mkdir -p /var/run/dbus

# generate Makefile
RUN ./bootstrap-configure

# build
RUN make install

# expose dbus port
EXPOSE 55556

CMD ["sh","-c","dbus-daemon --system && modbusd"]
