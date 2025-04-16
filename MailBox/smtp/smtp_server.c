// mailbox_smtp_server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <libpq-fe.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

#define PORT 2525
#define BUFFER_SIZE 1024
#define DATA_BUFFER_SIZE 8192
#define IV_SIZE 16

PGconn *conn = NULL;
unsigned char session_key[32] = {0};

void trim_newline(char *str) {
    int len = strlen(str);
    while (len > 0 && (str[len - 1] == '\n' || str[len - 1] == '\r')) {
        str[len - 1] = '\0';
        len--;
    }
}

char *get_or_create_user(const char *email) {
    char query[512];
    snprintf(query, sizeof(query), "SELECT id FROM users WHERE email='%s'", email);
    PGresult *res = PQexec(conn, query);

    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        char *user_id = strdup(PQgetvalue(res, 0, 0));
        PQclear(res);
        return user_id;
    }
    PQclear(res);

    snprintf(query, sizeof(query),
             "INSERT INTO users (email, username, password_hash) "
             "VALUES ('%s', 'external', '') RETURNING id;", email);
    res = PQexec(conn, query);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return NULL;
    }

    char *new_id = strdup(PQgetvalue(res, 0, 0));
    PQclear(res);
    return new_id;
}

void store_sent_email(const char *sender_email, const char *recipient_email, const char *subject, const char *content) {
    char *sender_id = get_or_create_user(sender_email);
    char *recipient_id = get_or_create_user(recipient_email);
    if (!sender_id || !recipient_id) {
        fprintf(stderr, "❌ Could not get or create sender/recipient for logging sent email.\n");
        free(sender_id); free(recipient_id);
        return;
    }

    char *s_id = PQescapeLiteral(conn, sender_id, strlen(sender_id));
    char *r_id = PQescapeLiteral(conn, recipient_id, strlen(recipient_id));
    char *subj = PQescapeLiteral(conn, subject, strlen(subject));
    char *body = PQescapeLiteral(conn, content, strlen(content));

    char query[8192];
    snprintf(query, sizeof(query),
             "INSERT INTO emails (from_user_id, to_user_id, subject, content, created_at, read) "
             "VALUES (%s, %s, %s, %s, NOW(), false);", s_id, r_id, subj, body);

    PGresult *res = PQexec(conn, query);
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
        fprintf(stderr, "❌ Failed to store sent mail: %s\n", PQerrorMessage(conn));
    else
        printf("📤 Sent mail logged successfully.\n");

    PQclear(res);
    PQfreemem(s_id); PQfreemem(r_id); PQfreemem(subj); PQfreemem(body);
    free(sender_id); free(recipient_id);
}

void store_email(const char *sender_email, const char *recipient_email, const char *subject, const char *content) {
    if (!strstr(recipient_email, "@mailbox.com")) {
        printf("❌ Email not for @mailbox.com, ignoring.\n");
        return;
    }

    char *sender_id = get_or_create_user(sender_email);
    char *recipient_id = get_or_create_user(recipient_email);
    if (!sender_id || !recipient_id) {
        fprintf(stderr, "❌ Could not get or create user IDs.\n");
        free(sender_id); free(recipient_id);
        return;
    }

    char *s_id = PQescapeLiteral(conn, sender_id, strlen(sender_id));
    char *r_id = PQescapeLiteral(conn, recipient_id, strlen(recipient_id));
    char *subj = PQescapeLiteral(conn, subject, strlen(subject));
    char *body = PQescapeLiteral(conn, content, strlen(content));

    char query[8192];
    snprintf(query, sizeof(query),
             "INSERT INTO emails (from_user_id, to_user_id, subject, content, created_at, read) "
             "VALUES (%s, %s, %s, %s, NOW(), false);", s_id, r_id, subj, body);

    PGresult *res = PQexec(conn, query);
    if (PQresultStatus(res) != PGRES_COMMAND_OK)
        fprintf(stderr, "❌ DB Insert failed: %s\n", PQerrorMessage(conn));
    else
        printf("✅ Email stored.\n");

    PQclear(res);
    PQfreemem(s_id); PQfreemem(r_id); PQfreemem(subj); PQfreemem(body);
    free(sender_id); free(recipient_id);
}

int aes_encrypt_data(const unsigned char *plaintext, int len,
                     const unsigned char *key, const unsigned char *iv,
                     unsigned char *ciphertext) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int outlen, total = 0;

    EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv);
    EVP_EncryptUpdate(ctx, ciphertext, &outlen, plaintext, len);
    total += outlen;
    EVP_EncryptFinal_ex(ctx, ciphertext + total, &outlen);
    total += outlen;

    EVP_CIPHER_CTX_free(ctx);
    return total;
}

int aes_decrypt_data(const unsigned char *ciphertext, int len,
                     const unsigned char *key, const unsigned char *iv,
                     unsigned char *plaintext) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    int outlen, total = 0;

    EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key, iv);
    EVP_DecryptUpdate(ctx, plaintext, &outlen, ciphertext, len);
    total += outlen;
    EVP_DecryptFinal_ex(ctx, plaintext + total, &outlen);
    total += outlen;

    EVP_CIPHER_CTX_free(ctx);
    return total;
}

int forward_email_to_fastmail(const char *sender, const char *recipient, const char *subject, const char *message) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(2526);  // FastMail port
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("Forwarding connect failed");
        close(sock);
        return -1;
    }

    char buffer[1024];
    recv(sock, buffer, sizeof(buffer) - 1, 0);

    snprintf(buffer, sizeof(buffer), "MAIL FROM:<%s>\r\n", sender);
    send(sock, buffer, strlen(buffer), 0); recv(sock, buffer, sizeof(buffer) - 1, 0);

    snprintf(buffer, sizeof(buffer), "RCPT TO:<%s>\r\n", recipient);
    send(sock, buffer, strlen(buffer), 0); recv(sock, buffer, sizeof(buffer) - 1, 0);

    send(sock, "DATA\r\n", 6, 0); recv(sock, buffer, sizeof(buffer) - 1, 0);

    unsigned char iv[IV_SIZE];
    RAND_bytes(iv, IV_SIZE);

    char combined[8192];
    snprintf(combined, sizeof(combined), "%s\n%s", subject, message);

    unsigned char encrypted[8192];
    int encrypted_len = aes_encrypt_data((unsigned char *)combined, strlen(combined), session_key, iv, encrypted);

    unsigned char payload[8192];
    memcpy(payload, iv, IV_SIZE);
    memcpy(payload + IV_SIZE, encrypted, encrypted_len);

    send(sock, payload, IV_SIZE + encrypted_len, 0);
    send(sock, "\r\n.\r\n", 5, 0);
    recv(sock, buffer, sizeof(buffer) - 1, 0);
    send(sock, "QUIT\r\n", 6, 0);

    close(sock);
    return 0;
}

void *handle_client(void *arg) {
    int client_fd = *((int *)arg);
    free(arg);

    char buffer[BUFFER_SIZE];
    char sender[256] = {0}, recipient[256] = {0};
    send(client_fd, "220 MailBox SMTP Ready\r\n", 24, 0);

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytes = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
        if (bytes <= 0) break;
        buffer[bytes] = '\0';
        trim_newline(buffer);
        printf("Client: %s\n", buffer);

        if (strncasecmp(buffer, "MAIL FROM:", 10) == 0) {
            sscanf(buffer, "MAIL FROM:<%255[^>]>", sender);
            send(client_fd, "250 OK\r\n", 8, 0);
        } else if (strncasecmp(buffer, "RCPT TO:", 8) == 0) {
            sscanf(buffer, "RCPT TO:<%255[^>]>", recipient);
            send(client_fd, "250 OK\r\n", 8, 0);
        } else if (strncasecmp(buffer, "DATA", 4) == 0) {
            send(client_fd, "354 End data with <CR><LF>.<CR><LF>\r\n", 38, 0);
            unsigned char data_buffer[DATA_BUFFER_SIZE] = {0};
            int total = 0, termination_found = 0;

            while (!termination_found) {
                int b = recv(client_fd, buffer, BUFFER_SIZE, 0);
                if (b <= 0) break;
                memcpy(data_buffer + total, buffer, b);
                total += b;
                for (int i = 0; i <= total - 5; i++) {
                    if (memcmp(data_buffer + i, "\r\n.\r\n", 5) == 0) {
                        total = i;
                        termination_found = 1;
                        break;
                    }
                }
            }

            if (total < IV_SIZE) {
                send(client_fd, "550 Invalid data\r\n", 18, 0);
                continue;
            }

            unsigned char iv[IV_SIZE];
            memcpy(iv, data_buffer, IV_SIZE);
            unsigned char *ciphertext = data_buffer + IV_SIZE;
            int cipher_len = total - IV_SIZE;

            unsigned char decrypted[DATA_BUFFER_SIZE] = {0};
            int dec_len = aes_decrypt_data(ciphertext, cipher_len, session_key, iv, decrypted);
            decrypted[dec_len] = '\0';

            char *subject = strtok((char *)decrypted, "\n");
            char *body = strtok(NULL, "");

            if (!subject || !body) {
                send(client_fd, "550 Invalid message format\r\n", 29, 0);
            } else {
                store_sent_email(sender, recipient, subject, body);  // Always store sent

                if (strstr(recipient, "@fastmail.com")) {
                    forward_email_to_fastmail(sender, recipient, subject, body);
                    send(client_fd, "250 Email forwarded to FastMail\r\n", 33, 0);
                } else if (strstr(recipient, "@mailbox.com")) {
                    store_email(sender, recipient, subject, body);
                    send(client_fd, "250 Email stored in MailBox\r\n", 30, 0);
                } else {
                    send(client_fd, "550 Unknown domain\r\n", 21, 0);
                }
            }
        } else if (strncasecmp(buffer, "QUIT", 4) == 0) {
            send(client_fd, "221 Bye\r\n", 9, 0);
            break;
        } else {
            send(client_fd, "500 Command unrecognized\r\n", 27, 0);
        }
    }

    close(client_fd);
    pthread_exit(NULL);
}

void start_server() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 10);
    printf("📡 SMTP Server running on port %d...\n", PORT);

    while (1) {
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, NULL, NULL);
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client_fd);
        pthread_detach(tid);
    }
}

int main() {
    char *dbname = getenv("MAILBOX_DBNAME");
    char *user = getenv("MAILBOX_DBUSER");
    char *pass = getenv("MAILBOX_DBPASSWORD");
    char *host = getenv("MAILBOX_DBHOST");
    char *port = getenv("MAILBOX_DBPORT");

    if (!dbname || !user || !pass || !host || !port) {
        fprintf(stderr, "❌ Missing DB environment variables\n");
        exit(EXIT_FAILURE);
    }

    char conninfo[512];
    snprintf(conninfo, sizeof(conninfo),
             "dbname=%s user=%s password=%s host=%s port=%s",
             dbname, user, pass, host, port);

    conn = PQconnectdb(conninfo);
    if (PQstatus(conn) != CONNECTION_OK) {
        fprintf(stderr, "❌ Database connection failed: %s\n", PQerrorMessage(conn));
        exit(EXIT_FAILURE);
    }

    printf("✅ Connected to database successfully.\n");
    memset(session_key, 0x01, sizeof(session_key));
    start_server();
    PQfinish(conn);
    return 0;
}
