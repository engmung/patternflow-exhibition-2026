// Browser entry for flasher.html: esptool-js (the engine ESP Web Tools runs
// on) and an MD5 for the post-write check, bundled by build.mjs into the page
// so it works offline from a double-clicked file.
import { ESPLoader, Transport } from "esptool-js";
import MD5 from "crypto-js/md5";
import Latin1 from "crypto-js/enc-latin1";

function md5hex(bytes) {
  // crypto-js hashes a WordArray; Latin1 maps each byte to one char.
  let s = "";
  const step = 0x8000;
  for (let i = 0; i < bytes.length; i += step) {
    s += String.fromCharCode.apply(null, bytes.subarray(i, i + step));
  }
  return MD5(Latin1.parse(s)).toString();
}

window.PFFlash = { ESPLoader, Transport, md5hex };
