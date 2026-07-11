// fetch-quotes.mjs — cash.ch -> SmallTV webhook JSON, one file per listing key.
//
// Runs on a GitHub Actions runner (full modern TLS, so it reaches cash.ch's
// ECDHE-only CDN that the ESP8266 cannot). Reads quotes-config.json, fetches a
// quote + a daily-close series per symbol, and writes out/quotes/<key>.json in
// the exact JSON the firmware's webhook parser already accepts. The device then
// reads those files from raw.githubusercontent.com over its static-RSA HTTPS.
//
// No dependencies: Node 20+ global fetch.
import { readFile, mkdir, writeFile } from 'node:fs/promises';

const GQL = 'https://www.cash.ch/_/api/graphql/prod?query=';
const UA = 'Mozilla/5.0 (smalltv-quotes)';

// Daily closes to request per timeframe (mirrors cashRangeDays in the firmware).
const RANGE_DAYS = { '1d': 2, '5d': 5, '1mo': 22, '3mo': 65, '6mo': 130, ytd: 130, '1y': 260, '2y': 400, '5y': 400, max: 400 };

const CUR = { USD: '$', EUR: '€', GBP: '£', JPY: '¥', CHF: 'CHF ' };

const num = (s) => {
  if (s === null || s === undefined) return null;
  const v = Number(String(s).replace(/[\s']/g, '').replace(',', '.'));
  return Number.isFinite(v) ? v : null;
};

function downsample(arr, n) {
  if (n <= 0 || arr.length <= n) return arr;
  const out = [];
  const step = (arr.length - 1) / (n - 1);
  for (let i = 0; i < n; i++) out.push(arr[Math.round(i * step)]);
  return out;
}

async function gql(query) {
  const r = await fetch(GQL + encodeURIComponent(query), { headers: { 'User-Agent': UA } });
  if (!r.ok) throw new Error(`cash.ch HTTP ${r.status}`);
  return r.json();
}

async function fetchQuote(key) {
  const q = `query{quoteList(listingKeys:"${key}"){quoteList{edges{node{...on Instrument{lval iNetVperprV perfPercentage mCur mShortName}}}}}}`;
  const d = await gql(q);
  return d?.data?.quoteList?.quoteList?.edges?.[0]?.node ?? null;
}

async function fetchChart(key, range, points) {
  const days = RANGE_DAYS[range] ?? 130;
  const q = `query{integration{solid{chart(listingKey:"${key}" frequency:"1d" from:"2020-01-01" to:"2100-01-01" max:"${days}"){timeserie{prices{close}}}}}}`;
  const d = await gql(q);
  const prices = d?.data?.integration?.solid?.chart?.timeserie?.prices ?? [];
  const closes = prices.map((p) => p.close).filter((v) => Number.isFinite(v));
  return downsample(closes, points).map((v) => Math.round(v * 10000) / 10000);
}

async function main() {
  const cfg = JSON.parse(await readFile(new URL('../../quotes-config.json', import.meta.url)));
  const range = (cfg.range || '6mo').toLowerCase();
  const points = Math.min(60, Math.max(0, cfg.points || 48));
  await mkdir('out/quotes', { recursive: true });

  let ok = 0, fail = 0;
  for (const s of cfg.symbols) {
    const key = s.key;
    const r = (s.range || range).toLowerCase();
    const p = s.points || points;
    try {
      const node = await fetchQuote(key);
      const price = num(node?.lval);
      if (price === null) throw new Error('no price');
      let spark = [];
      try { spark = await fetchChart(key, r, p); } catch (e) { console.log(`  chart failed for ${key}: ${e.message}`); }
      const out = {
        symbol: key,
        name: s.name || node?.mShortName || key,
        price,
        currency: node?.mCur ? (CUR[node.mCur] || node.mCur + ' ') : '',
        change: num(node?.iNetVperprV),
        changePct: num(node?.perfPercentage),
        spark,
        range: r.toUpperCase(),
        ok: true,
        updated: process.env.RUN_STAMP || '',
      };
      await writeFile(`out/quotes/${key}.json`, JSON.stringify(out));
      console.log(`  ${key}  ${out.currency}${price}  (${spark.length} pts)`);
      ok++;
    } catch (e) {
      console.log(`  ${key}  FAILED: ${e.message}`);
      fail++;
    }
  }
  console.log(`done: ${ok} ok, ${fail} failed`);
  if (ok === 0) process.exit(1);   // a run with zero successes fails loudly in Actions
}

main().catch((e) => { console.error(e); process.exit(1); });
