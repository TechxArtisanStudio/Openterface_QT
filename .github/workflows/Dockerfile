# Use ARM64 base image (Debian Bookworm for Raspberry Pi OS compatibility)
FROM --platform=linux/arm64 debian:bookworm-slim AS build

# Install build tools
RUN apt-get update && apt-get install -y \
    build-essential cmake git \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# Set working directory
WORKDIR /app

# Copy source code into container
COPY . .

# Build your application (adjust commands as needed)
RUN cmake -B build -S . && cmake --build build --config Release

# Final lightweight runtime image
FROM --platform=linux/arm64 debian:bookworm-slim AS runtime

# Install runtime dependencies (if any)
RUN apt-get update && apt-get install -y \
    libstdc++6 \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy binary from build stage
COPY --from=build /app/build/myapp /app/myapp

# Run the application
CMD ["./myapp"]
