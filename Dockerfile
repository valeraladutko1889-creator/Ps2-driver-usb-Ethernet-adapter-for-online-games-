FROM ps2dev/ps2sdk:latest
WORKDIR /src
COPY . .
CMD ["sh", "-c", "make clean && make"]
