from quintana/asterisk:latest

MAINTAINER Sylvain Boily <sylvain@wazo.io>

# Install Asterisk ARI stream module
WORKDIR /usr/src
ADD . /usr/src/asterisk-ari-stream-module
WORKDIR /usr/src/asterisk-ari-stream-module
RUN make
RUN make install

WORKDIR /root
