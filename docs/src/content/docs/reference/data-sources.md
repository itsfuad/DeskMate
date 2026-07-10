---
title: Data sources
description: "Where each ticker gets its prices: Yahoo Finance, cash.ch, or your own webhook."
---

The ticker can pull prices three ways. Yahoo Finance works out of the box with no server. cash.ch covers Swiss instruments Yahoo does not carry, also with no server. A custom webhook lets you own the source. Every ticker picks its own source in the **Ticker** tab, so a rotation can mix all three freely.

## Yahoo Finance, the default

The device fetches Yahoo's public chart endpoint directly over HTTPS, one request per symbol:

```
GET https://query1.finance.yahoo.com/v8/finance/chart/AAPL?range=1d&interval=5m
```

It parses the price, the previous close (for the change and percent change), the currency, and the sparkline itself. Any Yahoo symbol works: US and global stocks and ETFs, Swiss and European stocks (the `.SW` and `.DE` suffixes), crypto (`BTC-USD`), and FX (`EURUSD=X`).

The chart timeframe (`1d`, `5d`, `1mo`, `3mo`, `6mo`, `ytd`, `1y`, `2y`, `5y`, `max`) picks the candle interval automatically.

No API key, no account, no backend. The only requirement is outbound HTTPS, which the device handles with a bounded TLS buffer since Yahoo's records are small. Yahoo's endpoint is unofficial and rate-limited, so keep the refresh interval reasonable. The default 120 seconds is fine for 8 symbols.

## cash.ch, for Swiss instruments

Yahoo does not know many Swiss-listed products: structured products, AMCs and tracker certificates, and anything quoted off-exchange. The Swiss finance portal [cash.ch](https://www.cash.ch) does. The device queries cash.ch's public GraphQL endpoint directly over HTTPS, two small requests per symbol: a ~200-byte quote (price and day change) and a slim series of daily closes for the sparkline. No API key, no account.

With this source the `symbol` field is not a ticker but a cash.ch **listing key** in the form `valor-marketId-currencyId`, for example `147478611-246-333`. The web UI's **cash.ch symbol finder** (Ticker tab) turns a pasted cash.ch link, ISIN, valor, or instrument name into one: it queries cash.ch's search from your browser and one click adds the result as a ticker. Manual fallback: open the instrument's page on cash.ch, open the browser dev tools' Network tab filtered to `graphql`, reload, and read the key from `variables.id` or `listingKeys` of any chart request.

Worth knowing:

- The change fields mean change versus the previous close, same as Yahoo.
- The sparkline uses daily closes; cash.ch keeps roughly the last 6 months, so `6mo` is the longest timeframe with full data. Sub-daily timeframes add nothing for instruments that fix once a day.
- Prices are what cash.ch shows: delayed or fixing prices depending on the venue.
- The endpoint is unofficial and unauthenticated; it can change or disappear without notice. Keep the refresh interval polite: for instruments that fix once daily, a poll of 600 s or more is plenty. If it ever breaks, the custom webhook is the escape hatch.

## Custom webhook

To own the data (other providers, caching, secrets), set a ticker's source to **Webhook**. The device calls your URL, one request per symbol:

```
GET <webhookUrl>?symbol=AAPL&range=1d&points=48
```

and expects a small JSON object back:

```json
{
  "symbol": "AAPL",
  "name": "Apple",
  "price": 234.56,
  "currency": "$",
  "change": 2.34,
  "changePct": 1.01,
  "spark": [230.1, 231.0, 229.8, 234.56],
  "range": "1D",
  "ok": true
}
```

Only `price` is required. The full field table and two ready-to-import n8n workflows (Yahoo-only, and Yahoo + cash.ch in one) are in the repo under [`n8n/`](https://github.com/giovi321/smalltv-mod/tree/main/n8n).

The device pulls rather than receives a push, so your backend never needs to know the device's IP, and it keeps working if that IP changes. Since each ticker picks its own source, webhook tickers mix freely with Yahoo and cash.ch ones in the same rotation.

## TLS on the ESP8266

HTTPS is RAM-tight on the ESP8266. It works, but for a webhook on your own LAN, plain HTTP is more reliable if you see instability. The ESP32 boards have more headroom and run HTTPS without the tuning the ESP8266 needs.
