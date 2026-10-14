// Run: NODE_PATH=~/.copilot/skills/browser/node_modules node apps/zephyr-hello-shell/tests/web/test_speech_card.cjs
const assert = require('assert');
const fs = require('fs');
const http = require('http');
const path = require('path');
const puppeteer = require('puppeteer-core');
const source = fs.readFileSync(path.join(__dirname, '../../src/web_assets.c'), 'utf8');
function asset(name) {
  const match = source.match(new RegExp('const uint8_t ' + name + '\\[\\] =([\\s\\S]*?);\\n'));
  assert(match);
  return [...match[1].matchAll(/"(?:\\.|[^"\\])*"/g)].map(x => JSON.parse(x[0])).join('');
}
let speech = {state: 'unavailable', reason: 'no ready USB microphone', label: null, score: null, last_event: null};
let failSpeech = false;
let ledRequests = 0;
const server = http.createServer((req, res) => {
  res.setHeader('Cache-Control', 'no-store');
  if (req.url === '/') { res.setHeader('Content-Type', 'text/html'); res.end(asset('board_http_index_html')); }
  else if (/\.css$/.test(req.url)) { res.setHeader('Content-Type', 'text/css'); res.end(asset('board_http_styles_css')); }
  else if (/\.js$/.test(req.url)) { res.setHeader('Content-Type', 'text/javascript'); res.end(asset('board_http_app_js')); }
  else {
    res.setHeader('Content-Type', 'application/json');
    if (req.url === '/api/v1/leds') { ledRequests++; }
    if (req.url === '/api/v1/speech') { res.statusCode = failSpeech ? 503 : 200; res.end(JSON.stringify(speech)); }
    else if (req.url === '/api/v1/mic') { res.end(JSON.stringify({attached: false, ready: false})); }
    else if (req.url === '/api/v1/mouse') { res.end(JSON.stringify({attached: false})); }
    else { res.end(JSON.stringify({board: {}, network: {}, io: {leds: [], button: {}}})); }
  }
});
async function main() {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const browser = await puppeteer.launch({executablePath: process.env.BROWSER_PATH || '/usr/bin/microsoft-edge',
    headless: false, args: ['--no-sandbox', '--disable-dev-shm-usage']});
  try {
    const page = await browser.newPage();
    await page.goto(`http://127.0.0.1:${server.address().port}`);
    const waitText = (id, text) => page.waitForFunction((id, text) =>
      document.getElementById(id)?.textContent === text, {timeout: 5000}, id, text);
    await waitText('speech-state', 'unavailable');
    speech = {...speech, state: 'warming-up', reason: 'collecting audio'};
    await waitText('speech-state', 'warming-up');
    speech = {state: 'running', reason: 'recognizing English yes/no/up/down/left/right', label: 'yes', score: .91,
      last_event: {label: 'yes', sequence: 1, uptime_ms: 12000},
      session: 3, inference_us: 1500, max_processing_us: 2400, windows: 3, gaps: 0, drops: 0};
    await waitText('speech-label', 'yes');
    await waitText('speech-score', 'Model score: 91.0%');
    await waitText('speech-event', 'Detected: yes');
    await page.evaluate(() => {
      window.speechHighlights = 0;
      new MutationObserver((records) => {
        window.speechHighlights += records.filter(r => r.attributeName === 'data-event-sequence').length;
      }).observe(document.getElementById('speech-event'), {attributes: true});
    });
    await new Promise(r => setTimeout(r, 1200));
    assert.equal(await page.evaluate(() => window.speechHighlights), 0, 'same event highlighted twice');
    speech.last_event = {label: 'no', sequence: 2, uptime_ms: 14000};
    speech.label = 'no';
    await waitText('speech-event', 'Detected: no');
    assert.equal(await page.evaluate(() => window.speechHighlights), 1);
    speech.last_event = {label: 'left', sequence: 3, uptime_ms: 16000};
    speech.label = 'left';
    await waitText('speech-label', 'left');
    await waitText('speech-event', 'Detected: left');
    assert.equal(await page.evaluate(() => window.speechHighlights), 2);
    assert.match(await page.evaluate(() => document.body.textContent), /yes, no, up, down, left, right/);
    speech = {state: 'unavailable', reason: 'unplugged', label: null, score: null, last_event: null};
    await waitText('speech-label', '--');
    await waitText('speech-event', 'No accepted keyword');
    failSpeech = true;
    await waitText('speech-state', 'error');
    failSpeech = false;
    await waitText('speech-state', 'unavailable');
    assert.equal(ledRequests, 0, 'speech changed LED state');
    console.log('PASS: speech card states, events, direction labels, polling failure and unchanged LEDs');
  } finally {
    await browser.close();
    await new Promise(resolve => server.close(resolve));
  }
}
main().catch(error => { console.error(error); server.close(); process.exitCode = 1; });
