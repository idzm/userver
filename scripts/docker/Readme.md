* Build image with all the libraries for the databases installed. No database servers installed:
```
docker build -t ghcr.io/userver-framework/ubuntu-22.04-userver-base:latest -f scripts/docker/base-ubuntu-22.04.dockerfile .
```

* Build image with all the libraries for the databases installed and additional set of compilers, database servers and workarounds for CI:
```
docker build -t ghcr.io/userver-framework/ubuntu-22.04-userver-base-ci:latest -f scripts/docker/base-ubuntu-22.04-ci.dockerfile .
```