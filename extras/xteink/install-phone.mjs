// SPDX-License-Identifier: Apache-2.0
// Send each complete PBW to the selected reader through CoreApp Developer Connection.
import {readFile} from "node:fs/promises";
import process from "node:process";

async function install(url, file) {
  const pbw = await readFile(file);
  const packet = Buffer.concat([Buffer.from([0x04]), pbw]);
  await new Promise((resolve, reject) => {
    const socket = new WebSocket(url);
    socket.binaryType = "arraybuffer";
    const timer = setTimeout(() => {
      reject(new Error("timed out waiting for install status"));
      socket.close();
    }, 120_000);
    socket.onopen = () => socket.send(packet);
    socket.onmessage = ({data}) => {
      const status = Buffer.from(data);
      if (status[0] !== 0x05) return;
      if (status.length === 5 && status.readInt32BE(1) === 0) resolve();
      else reject(new Error(`install status ${status.toString("hex")}`));
      socket.close();
    };
    socket.onerror = () => reject(new Error("developer connection failed"));
    socket.onclose = () => {
      clearTimeout(timer);
      reject(new Error("connection closed before install status"));
    };
  });
}

const [url, ...files] = process.argv.slice(2);
if (!url || !files.length) {
  console.error("Usage: node install-phone.mjs ws://PHONE:9000/ FILE.pbw [FILE.pbw ...]");
  process.exitCode = 2;
} else {
  for (const file of files) {
    try {
      await install(url, file);
      console.log(`Installed ${file}`);
    } catch (error) {
      console.error(`${file}: ${error.message}`);
      process.exitCode = 1;
      break;
    }
  }
}
