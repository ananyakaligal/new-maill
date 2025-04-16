const net = require('net');
const crypto = require('crypto');

const sessionKey = Buffer.alloc(32, 0x01); // MUST match the server

function sendEmailSMTP({ from, to, subject, message }) {
  return new Promise((resolve, reject) => {
    const client = new net.Socket();
    let step = 0;

    client.connect(2526, '127.0.0.1', () => {
      console.log('📡 Connected to SMTP server');
    });

    client.on('data', (data) => {
      const response = data.toString();
      console.log('📨 Server:', response);

      if (response.includes('220') && step === 0) {
        client.write(`MAIL FROM:<${from}>\r\n`);
        step = 1;
      } else if (response.includes('250') && step === 1) {
        client.write(`RCPT TO:<${to}>\r\n`);
        step = 2;
      } else if (response.includes('250') && step === 2) {
        client.write('DATA\r\n');
        step = 3;
      } else if (response.includes('354') && step === 3) {
        // ✅ Combine subject and message into one payload
        const fullMessage = `${subject}\n${message}`;
        const iv = crypto.randomBytes(16);
        const cipher = crypto.createCipheriv('aes-256-cbc', sessionKey, iv);
        let encrypted = cipher.update(fullMessage, 'utf8');
        encrypted = Buffer.concat([encrypted, cipher.final()]);
        const payload = Buffer.concat([iv, encrypted]);

        client.write(payload);
        client.write('\r\n.\r\n');
        step = 4;
      } else if (response.includes('250') && step === 4) {
        client.write('QUIT\r\n');
        step = 5;
      } else if (response.includes('221') && step === 5) {
        console.log('👋 Closing connection');
        client.destroy();
        resolve('✅ Email sent successfully!');
      }
    });

    client.on('close', () => {
      console.log('🚪 Connection closed');
    });

    client.on('error', (err) => {
      console.error('🚨 Error:', err);
      reject(err);
    });
  });
}

module.exports = { sendEmailSMTP };
