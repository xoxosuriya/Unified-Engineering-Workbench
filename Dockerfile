# =========================================================================
# Unified Engineering Workbench - Multi-Stage Production Dockerfile
# Target: Portable Linux C++17 REST Backend for Cloud Hosting (Render / Docker)
# Candidate: Suriya Prakash S (RA2311047010144) - PASS Science Works R&D
# =========================================================================

# -------------------------------------------------------------------------
# STAGE 1: Build & Verification Environment
# -------------------------------------------------------------------------
FROM debian:bookworm-slim AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    libeigen3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy dependency configuration and source trees
COPY CMakeLists.txt ./
COPY src ./src
COPY tests ./tests
COPY data ./data

# Configure and compile with standard C++17 and optimizations
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_STANDARD=17 \
    && cmake --build build --target workbench_backend workbench_tests -j$(nproc)

# Run automated C++ test suite (all 53 tests must pass during build)
RUN ./build/workbench_tests

# -------------------------------------------------------------------------
# STAGE 2: Minimal Production Runtime
# -------------------------------------------------------------------------
FROM debian:bookworm-slim AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    && rm -rf /var/lib/apt/lists/*

# Security: Create non-root user
RUN useradd -m -u 10001 -U workbench

WORKDIR /home/workbench/app

# Copy compiled backend binary from builder
COPY --from=builder /app/build/workbench_backend ./workbench_backend
COPY --from=builder /app/build/workbench_tests ./workbench_tests

# Copy runtime assets and static frontend
COPY --chown=workbench:workbench data ./data
COPY --chown=workbench:workbench src/frontend ./src/frontend

# Create logs directory with proper ownership
RUN mkdir -p logs && chown -R workbench:workbench /home/workbench/app

USER workbench

# Default environment configuration
ENV HOST=0.0.0.0
ENV PORT=8080
ENV WORKBENCH_ENV=production
ENV DISABLE_SHUTDOWN=1

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=5s --retries=3 \
    CMD curl -f http://127.0.0.1:${PORT}/api/thermal3d/health || exit 1

ENTRYPOINT ["./workbench_backend"]
CMD []
