"use strict";

const REPO = "wgd-modular/carciofo-firmware";
const FLASH_START = 0x08000000;
const ACCENTS = ["cobalt", "teal", "terracotta", "verde"];

let dfuDevice = null;

const $ = (sel, el) => (el || document).querySelector(sel);

/* ---------- release loading ---------- */

async function loadRelease() {
  const res = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`);
  if (!res.ok) throw new Error(`GitHub API: ${res.status}`);
  return res.json();
}

function firmwareName(assetName) {
  return assetName.replace(/\.bin$/, "");
}

async function fetchReadme(fw, tag) {
  for (const ref of [tag, "main"]) {
    const res = await fetch(`https://raw.githubusercontent.com/${REPO}/${ref}/src/${fw}/README.md`);
    if (res.ok) return res.text();
  }
  return null;
}

function parseReadme(md) {
  const lines = md.split("\n");
  let description = "";
  for (let i = 0; i < lines.length; i++) {
    const l = lines[i].trim();
    if (!l || l.startsWith("#")) continue;
    if (l.startsWith("|")) break;
    description = l;
    if (description.endsWith(":")) {
      for (let j = i + 1; j < lines.length; j++) {
        const f = lines[j].trim();
        if (!f || f.startsWith("```")) continue;
        description += " " + f.replace(/->/g, "\u2192");
        break;
      }
    }
    break;
  }

  const rows = [];
  const ctrlIdx = lines.findIndex(l => /^##\s+Controls/i.test(l));
  if (ctrlIdx >= 0) {
    for (let i = ctrlIdx + 1; i < lines.length; i++) {
      const l = lines[i].trim();
      if (l.startsWith("##")) break;
      if (!l.startsWith("|")) continue;
      const cells = l.split("|").slice(1, -1).map(c => c.trim());
      if (cells.every(c => /^[-: ]*$/.test(c))) continue;
      rows.push(cells);
    }
  }
  return { description, rows };
}

/* ---------- cards ---------- */

function buildCard(asset, tag, accent) {
  const fw = firmwareName(asset.name);
  const card = document.createElement("article");
  card.className = `card ${accent}`;
  card.innerHTML = `
    <div class="card-head">
      <svg class="fw-mark" viewBox="0 0 33 33" aria-hidden="true"><use href="pattern.svg#c" fill="currentColor"/></svg>
      <h2>${fw.replace(/-/g, " ")}</h2>
      <span class="ver">${tag}</span>
    </div>
    <div class="card-body">
      <p class="desc">Loading description…</p>
      <div class="controls" hidden><h3>Controls</h3><table></table></div>
      <p class="status-line" hidden></p>
      <div class="progress" hidden><div class="bar"></div><span class="ptext"></span></div>
      <div class="card-foot">
        <a class="readme-link" target="_blank" rel="noopener"
           href="https://github.com/${REPO}/blob/${tag}/src/${fw}/README.md">Full guide ↗</a>
        <button class="btn flash-btn">Flash</button>
      </div>
    </div>`;

  $(".flash-btn", card).addEventListener("click", () => flashAsset(asset, card));

  fetchReadme(fw, tag).then(md => {
    if (!md) {
      $(".desc", card).textContent = "No description available.";
      return;
    }
    const { description, rows } = parseReadme(md);
    $(".desc", card).textContent = description || "No description available.";
    if (rows.length) {
      const table = $(".controls table", card);
      const [head, ...body] = rows;
      const tr = h => `<tr>${h}</tr>`;
      table.innerHTML =
        tr(head.map(c => `<th>${c}</th>`).join("")) +
        body.map(r => tr(r.map(c => `<td>${c}</td>`).join(""))).join("");
      $(".controls", card).hidden = false;
    }
  });

  return card;
}

async function render() {
  if (!("usb" in navigator)) {
    $("#unsupported").hidden = false;
    $("#connect").disabled = true;
  }
  try {
    const release = await loadRelease();
    const date = new Date(release.published_at).toLocaleDateString("en-GB",
      { day: "numeric", month: "long", year: "numeric" });
    $("#release-chip").textContent = `latest release ${release.tag_name} · ${date}`;

    const bins = release.assets
      .filter(a => a.name.endsWith(".bin"))
      .sort((a, b) => {
        const test = n => n.startsWith("hardware-test") ? 1 : 0;
        return test(a.name) - test(b.name) || a.name.localeCompare(b.name);
      });

    const cards = $("#cards");
    bins.forEach((asset, i) =>
      cards.appendChild(buildCard(asset, release.tag_name, ACCENTS[i % ACCENTS.length])));
  } catch (err) {
    $("#release-chip").textContent = "release list unavailable";
    const box = $("#load-error");
    box.textContent = `Couldn’t load the release list from GitHub (${err.message}). ` +
      "You can still flash a local .bin below.";
    box.hidden = false;
  }
}

/* ---------- DFU ---------- */

async function getTransferSize(device) {
  try {
    const data = await device.readConfigurationDescriptor(0);
    const config = dfu.parseConfigurationDescriptor(data);
    for (const desc of config.descriptors) {
      if (desc.bDescriptorType === 0x21 && desc.hasOwnProperty("wTransferSize")) {
        return desc.wTransferSize;
      }
    }
  } catch (e) { /* fall through */ }
  return 1024;
}

async function connectDevice() {
  const usbDevice = await navigator.usb.requestDevice({
    filters: [{ vendorId: 0x0483, productId: 0xdf11 }]
  });

  let interfaces = dfu.findDeviceDfuInterfaces(usbDevice);
  if (!interfaces.length) throw new Error("no DFU interface — is the Seed in bootloader mode?");

  await usbDevice.open();
  const probe = new dfu.Device(usbDevice, interfaces[0]);
  const names = await probe.readInterfaceNames();
  for (const intf of interfaces) {
    if (intf.name === null) {
      const c = intf.configuration.configurationValue;
      intf.name = names[c]?.[intf.interface.interfaceNumber]?.[intf.alternate.alternateSetting];
    }
  }

  let chosen = interfaces.find(i => i.name && i.name.includes("Internal Flash")) || interfaces[0];
  const device = new dfuse.Device(usbDevice, chosen);
  await device.open();
  if (!device.memoryInfo) throw new Error("device did not report a DfuSe memory map");
  return device;
}

async function ensureDevice() {
  if (dfuDevice && dfuDevice.device_.opened) return dfuDevice;
  dfuDevice = await connectDevice();
  const status = $("#device-status");
  status.textContent = `connected: ${dfuDevice.device_.productName || "STM32 bootloader"}`;
  status.classList.add("ok");
  navigator.usb.addEventListener("disconnect", e => {
    if (dfuDevice && e.device === dfuDevice.device_) {
      dfuDevice = null;
      status.textContent = "device disconnected";
      status.classList.remove("ok");
    }
  });
  return dfuDevice;
}

async function flashBuffer(buffer, ui) {
  const device = await ensureDevice();

  if (buffer.byteLength > 128 * 1024) {
    throw new Error("file is larger than the Seed's 128 KB flash");
  }

  ui.status("Preparing…");
  let state = await device.getStatus();
  if (state.state === dfu.dfuERROR) await device.clearStatus();

  device.startAddress = FLASH_START;
  device.logProgress = (done, total) => {
    if (total) ui.progress(done / total, `${Math.round(done / total * 100)}%`);
  };
  device.logInfo = msg => {
    if (/erase/i.test(msg)) ui.status("Erasing…");
    else if (/download|copying|wrote/i.test(msg)) ui.status("Writing…");
  };
  device.logWarning = () => {};
  device.logDebug = () => {};
  device.logError = () => {};

  const transferSize = await getTransferSize(device);
  await device.do_download(transferSize, buffer, false);
  ui.progress(1, "100%");
  ui.done("Flashed! The module restarts on its own — if not, tap RESET.");
}

function cardUi(card) {
  const line = $(".status-line", card);
  const prog = $(".progress", card);
  const btn = $(".flash-btn", card) || $("#local-flash");
  return {
    start() { btn.disabled = true; line.hidden = false; line.className = "status-line"; prog.hidden = false; },
    status(t) { line.textContent = t; },
    progress(f, t) { prog.style.setProperty("--p", `${f * 100}%`); $(".ptext", prog).textContent = t; },
    done(t) { line.textContent = t; line.className = "status-line ok"; btn.disabled = false; },
    fail(t) { line.textContent = t; line.className = "status-line err"; prog.hidden = true; btn.disabled = false; }
  };
}

async function flashAsset(asset, card) {
  const ui = cardUi(card);
  ui.start();
  try {
    ui.status("Downloading firmware…");
    const res = await fetch(asset.browser_download_url);
    if (!res.ok) throw new Error(`download failed (${res.status})`);
    const buffer = await res.arrayBuffer();
    await flashBuffer(buffer, ui);
  } catch (err) {
    ui.fail(friendly(err));
  }
}

function friendly(err) {
  const msg = err && err.message ? err.message : String(err);
  if (/No device selected/i.test(msg)) return "No device selected.";
  if (/no DFU interface|bootloader/i.test(msg)) return msg;
  return `Something went wrong: ${msg}`;
}

/* ---------- wiring ---------- */

$("#connect").addEventListener("click", async () => {
  try { await ensureDevice(); }
  catch (err) {
    const status = $("#device-status");
    status.textContent = friendly(err);
    status.classList.remove("ok");
  }
});

let localBuffer = null;
$("#local-file").addEventListener("change", async e => {
  const file = e.target.files[0];
  if (!file) return;
  localBuffer = await file.arrayBuffer();
  $("#local-name").textContent = `${file.name} · ${(file.size / 1024).toFixed(1)} KB`;
  $("#local-flash").disabled = false;
});

$("#local-flash").addEventListener("click", async () => {
  const prog = $("#local-progress");
  const line = document.createElement("p");
  const holder = $(".local-body");
  let ui = {
    start() { $("#local-flash").disabled = true; prog.hidden = false; },
    status(t) { $(".ptext", prog).textContent = t; },
    progress(f, t) { prog.style.setProperty("--p", `${f * 100}%`); $(".ptext", prog).textContent = t; },
    done(t) { $(".ptext", prog).textContent = "done"; line.className = "status-line ok"; line.textContent = t; holder.appendChild(line); $("#local-flash").disabled = false; },
    fail(t) { line.className = "status-line err"; line.textContent = t; holder.appendChild(line); $("#local-flash").disabled = false; }
  };
  ui.start();
  try { await flashBuffer(localBuffer, ui); }
  catch (err) { ui.fail(friendly(err)); }
});

render();
