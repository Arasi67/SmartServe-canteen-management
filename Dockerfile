# Build the C backend inside Linux (the same environment used by Render).
FROM gcc:14-bookworm AS builder
WORKDIR /app
COPY . .
RUN gcc -std=c11 -O2 -Wall -Wextra -pthread server.c -o server

# Keep only the compiled server and frontend files in the final image.
FROM debian:bookworm-slim
WORKDIR /app
COPY --from=builder /app/server ./server
COPY index.html style.css script.js ./
COPY images ./images
ENV PORT=10000
EXPOSE 10000
CMD ["./server"]

