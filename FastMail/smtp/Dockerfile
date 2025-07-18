# Use official gcc image
FROM gcc:latest

# Set working directory
WORKDIR /app

# Copy everything in this folder
COPY . .

# Build your SMTP server
RUN gcc -o smtp_server smtp_server.c

# Expose the port used by your SMTP server
EXPOSE 2526

# Start the server
CMD ["./smtp_server"]
