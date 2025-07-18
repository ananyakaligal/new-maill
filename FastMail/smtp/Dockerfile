FROM ubuntu:22.04

# Install dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    libpq-dev \
    libssl-dev \
    openssl \
    netcat \
    git \
    curl

# Add your code
WORKDIR /app
COPY . .

# Compile the C server
RUN gcc -o smtp smtp.c -lpq -lssl -lcrypto -pthread

# Expose your custom SMTP port
EXPOSE 2526

# Run the server
CMD ["./smtp"]
