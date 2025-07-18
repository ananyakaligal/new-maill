const net = require('net');
const crypto = require('crypto');

const sessionKey = Buffer.alloc(32, 0x01); // MUST match the server

const SMTP_HOST = process.env.SMTP_HOST || '127.0.0.1';
const SMTP_PORT = parseInt(process.env.SMTP_PORT || '2526', 10);

function sendEmailSMTP({ from, to, subject, message }) {
  return new Promise((resolve, reject) => {
    const client = new net.Socket();
    let step = 0;

    client.connect(SMTP_PORT, SMTP_HOST, () => {
      console.log(`📡 Connected to SMTP server at ${SMTP_HOST}:${SMTP_PORT}`);
    });

    client.on('data', (data) => {
      const response = data.toString();
      console.log('📨 Server:', response.trim());

      switch (step) {
        case 0:
          if (response.includes('220')) {
            client.write(`MAIL FROM:<${from}>\r\n`);
            step++;
          }
          break;
        case 1:
          if (response.includes('250')) {
            client.write(`RCPT TO:<${to}>\r\n`);
            step++;
          }
          break;
        case 2:
          if (response.includes('250')) {
            client.write('DATA\r\n');
            step++;
          }
          break;
        case 3:
          if (response.includes('354')) {
            const fullMessage = `${subject}\n${message}`;
            const iv = crypto.randomBytes(16);
            const cipher = crypto.createCipheriv('aes-256-cbc', sessionKey, iv);
            let encrypted = cipher.update(fullMessage, 'utf8');
            encrypted = Buffer.concat([encrypted, cipher.final()]);
            const payload = Buffer.concat([iv, encrypted]);

            client.write(payload);
            client.write('\r\n.\r\n');
            step++;
          }
          break;
        case 4:
          if (response.includes('250')) {
            client.write('QUIT\r\n');
            step++;
          }
          break;
        case 5:
          if (response.includes('221')) {
            console.log('👋 Closing connection');
            client.destroy();
            resolve('✅ Email sent successfully!');
          }
          break;
        default:
          console.warn('⚠️ Unexpected response or step mismatch');
      }
    });

    client.on('close', () => {
      console.log('🚪 Connection closed');
    });

    client.on('error', (err) => {
      console.error('🚨 Error:', err.message);
      reject(err);
    });
  });
}

module.exports = { sendEmailSMTP };
