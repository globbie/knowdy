FROM alpine:3.8 as builder
RUN apk --update add \
    go \
    dep \
    cmake \
    make \
    check \
    check-dev \
    readline-dev \
    subunit-dev \
    git \
    musl-dev
RUN apk --update add \
    util-linux-dev

WORKDIR /tmp
COPY . .

WORKDIR build/
RUN cmake ../ -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=`go env CC` && make && cd ../service && go build -o knd-service -ldflags "-linkmode external -extldflags '-static' -s -w" server.go knowdy.go

RUN addgroup -S knowdy && adduser -S knowdy -G knowdy

FROM scratch

COPY --from=builder /etc/passwd /etc/passwd
COPY --from=builder /etc/group /etc/group
USER knowdy

COPY ./etc/knowdy/shard.gsl /etc/knowdy/shard.gsl
COPY ./etc/knowdy/service.json /etc/knowdy/service.json
COPY --from=builder /tmp/service/knd-service /usr/bin/knd-service

EXPOSE 8080
CMD ["/usr/bin/knd-service", "--listen-address=0.0.0.0:8080", "--config-path=/etc/knowdy/service.json"]
