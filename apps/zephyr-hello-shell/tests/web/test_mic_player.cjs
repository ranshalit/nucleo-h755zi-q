// Headless regression test for USB microphone dashboard playback.
// Run: NODE_PATH=~/.copilot/skills/browser/node_modules node apps/zephyr-hello-shell/tests/web/test_mic_player.cjs
const fs = require('fs');
const http = require('http');
const path = require('path');
const puppeteer = require('puppeteer-core');

const root = path.resolve(__dirname, '../..');
const webAssets = fs.readFileSync(path.join(root, 'src/web_assets.c'), 'utf8');
const browserPath = process.env.BROWSER_PATH || '/usr/bin/microsoft-edge';
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));
let micStatus = { attached: false, ready: false, session: 0, reason: 'no supported USB microphone' };
let streamMode = 'stream';
let closedStreams = 0;
let dashboardServer;
let streamServer;
let dashboardPort;
let streamPort;

function fail(message) {
  throw new Error(message);
}

function cStringArray(name) {
  const match = webAssets.match(new RegExp('const uint8_t ' + name + '\\[\\] =([\\s\\S]*?);\\n'));
  if (!match) { fail('could not find ' + name + ' in web_assets.c'); }
  return Array.from(match[1].matchAll(/"(?:\\.|[^"\\])*"/g), (item) => JSON.parse(item[0])).join('');
}

function send(res, status, contentType, body) {
  res.writeHead(status, { 'Content-Type': contentType, 'Cache-Control': 'no-store' });
  res.end(body);
}

function pcmBytes() {
  const data = Buffer.alloc(480 * 2 * 2);
  for (let frame = 0; frame < 480; frame += 1) {
    const sample = Math.round(Math.sin(frame * Math.PI / 12) * 12000);
    data.writeInt16LE(sample, frame * 4);
    data.writeInt16LE(-sample, frame * 4 + 2);
  }
  return data;
}

function runStream(res, mode) {
  res.on('close', () => { closedStreams += 1; });
  if (mode === 'unavailable' || mode === 'busy') {
    res.writeHead(mode === 'unavailable' ? 503 : 409, {
      'Content-Type': 'text/plain', 'Access-Control-Allow-Origin': '*', 'Cache-Control': 'no-store',
    });
    res.end(mode === 'unavailable' ? 'no supported USB microphone' : 'another listener is active');
    return;
  }
  res.writeHead(200, {
    'Content-Type': 'application/octet-stream', 'Access-Control-Allow-Origin': '*', 'Cache-Control': 'no-store',
  });
  const header = Buffer.alloc(16);
  header.write('MIC1', 0, 'ascii');
  header.writeUInt32LE(48000, 4);
  header.writeUInt16LE(2, 8);
  header.writeUInt16LE(16, 10);
  header.writeUInt32LE(7, 12);
  const pcm = pcmBytes();
  const fragments = [header.subarray(0, 5), header.subarray(5, 9), header.subarray(9),
    pcm.subarray(0, 3), pcm.subarray(3, 119), pcm.subarray(119, 671), pcm.subarray(671)];
  let index = 0;
  const writeNext = () => {
    if (res.destroyed || res.writableEnded || index >= fragments.length) {
      if (mode === 'disconnect' && !res.destroyed && !res.writableEnded) { res.end(); }
      return;
    }
    res.write(fragments[index]);
    index += 1;
    setTimeout(writeNext, index < 3 ? 25 : 12);
  };
  writeNext();
}

function waitForPage(page, expression, message, timeout = 5000) {
  return page.waitForFunction(expression, { timeout }).catch(() => fail(message));
}

(async () => {
  dashboardServer = http.createServer((req, res) => {
    if (req.url === '/favicon.ico') { res.writeHead(204); return res.end(); }
    if (req.url.startsWith('/?')) { return send(res, 200, 'text/html', cStringArray('board_http_index_html')); }
    if (req.url === '/') { return send(res, 200, 'text/html', cStringArray('board_http_index_html')); }
    if (req.url === '/dashboard.css') { return send(res, 200, 'text/css', cStringArray('board_http_styles_css')); }
    if (req.url === '/dashboard.js') { return send(res, 200, 'application/javascript', cStringArray('board_http_app_js')); }
    if (req.url === '/api/v1/status') {
      return send(res, 200, 'application/json', JSON.stringify({ board: 'test', http: { port: 80 }, network: {}, uptime_ms: 1 }));
    }
    if (req.url === '/api/v1/io') {
      return send(res, 200, 'application/json', JSON.stringify({ button: {}, leds: [], mouse: { connected: false } }));
    }
    if (req.url === '/api/v1/mic') { return send(res, 200, 'application/json', JSON.stringify(micStatus)); }
    return send(res, 404, 'text/plain', 'not found');
  });
  streamServer = http.createServer((req, res) => runStream(res, streamMode));
  await new Promise((resolve) => dashboardServer.listen(0, '127.0.0.1', resolve));
  dashboardPort = dashboardServer.address().port;
  await new Promise((resolve) => streamServer.listen(0, '127.0.0.1', resolve));
  streamPort = streamServer.address().port;

  const browser = await puppeteer.launch({
    executablePath: browserPath,
    headless: 'new',
    args: ['--no-sandbox', '--autoplay-policy=no-user-gesture-required'],
  });
  try {
    const page = await browser.newPage();
    await page.goto(`http://127.0.0.1:${dashboardPort}/?micPort=${streamPort}`, { waitUntil: 'load' });
    await page.waitForSelector('#mic-toggle', { timeout: 3000 }).catch(() => fail('mic card/toggle missing'));
    await waitForPage(page,
      () => document.getElementById('mic-state').textContent.toLowerCase().includes('disconnected'),
      'initial microphone state should be disconnected');
    if (!(await page.$eval('#mic-toggle', (button) => button.disabled))) {
      fail('Start listening must be disabled when no microphone is attached');
    }
    console.log('initial disconnected + disabled: PASS');

    micStatus = {
      attached: true, ready: true, session: 7,
      format: { sample_rate_hz: 48000, channels: 2, bits_per_sample: 16 },
      level: 4, packets: 10, drops: 0, errors: 0, missed: 0, reason: 'streaming',
    };
    await waitForPage(page,
      () => !document.getElementById('mic-toggle').disabled,
      'Start listening should enable when microphone becomes ready');
    streamMode = 'stream';
    await page.click('#mic-toggle');
    await waitForPage(page,
      () => document.getElementById('mic-state').textContent.toLowerCase().includes('playing'),
      'split MIC1 header and fragmented PCM should enter playing state');
    const live = await page.evaluate(() => ({
      format: document.getElementById('mic-format').textContent,
      bytes: window.__micPlayerTest && window.__micPlayerTest.bytesReceived,
      buffered: window.__micPlayerTest && window.__micPlayerTest.bufferedAhead,
      clip: window.__micPlayerTest && window.__micPlayerTest.clipBufferedAhead(2, 0, 0.25),
    }));
    if (!/48000\s*Hz\s*[,/ ]\s*2\s*ch\s*[,/ ]\s*16-bit/i.test(live.format)) {
      fail('format text should show 48000 Hz, 2 ch, 16-bit; got: ' + live.format);
    }
    if (!(live.bytes > 0)) { fail('fragmented PCM bytes were not consumed'); }
    if (!(live.buffered < 1 && live.clip < 1)) { fail('playback queue must remain below one second'); }
    console.log('split header + PCM playback, format, bounded queue:', live, 'PASS');

    const closedBeforeStop = closedStreams;
    await page.click('#mic-toggle');
    await waitForPage(page,
      () => document.getElementById('mic-state').textContent.toLowerCase().includes('stopped'),
      'Stop should show stopped state');
    const closeDeadline = Date.now() + 3000;
    while (closedStreams <= closedBeforeStop && Date.now() < closeDeadline) { await sleep(20); }
    if (closedStreams <= closedBeforeStop) { fail('server did not observe request close after Stop'); }
    console.log('Stop aborts and releases listener: PASS');

    streamMode = 'unavailable';
    await page.click('#mic-toggle');
    await waitForPage(page,
      () => document.getElementById('mic-state').textContent.toLowerCase().includes('unavailable'),
      'HTTP 503 should show unavailable');
    console.log('503 unavailable state: PASS');

    streamMode = 'busy';
    await page.click('#mic-toggle');
    await waitForPage(page,
      () => document.getElementById('mic-state').textContent.toLowerCase().includes('busy'),
      'HTTP 409 should show busy');
    console.log('409 busy state: PASS');

    streamMode = 'disconnect';
    await page.click('#mic-toggle');
    await waitForPage(page,
      () => /disconnected|ended/i.test(document.getElementById('mic-state').textContent),
      'mid-stream disconnect should show explicit disconnected/ended state');
    console.log('mid-stream disconnect state: PASS');
    console.log('PASS');
  } finally {
    await browser.close();
    await Promise.all([new Promise((resolve) => streamServer.close(resolve)),
      new Promise((resolve) => dashboardServer.close(resolve))]);
  }
})().catch((error) => {
  console.error('FAIL:', error.stack || error);
  if (streamServer && streamServer.listening) { streamServer.close(); }
  if (dashboardServer && dashboardServer.listening) { dashboardServer.close(); }
  process.exitCode = 1;
});
