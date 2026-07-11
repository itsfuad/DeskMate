---
title: Stock and crypto ticker
description: Show live prices, change, and a sparkline for up to 8 rotating symbols.
---

The ticker is the default mode. It shows one symbol at a time and rotates through your list on a timer. For each symbol it draws the price, the absolute change, the percent change with an up or down arrow, and a small sparkline chart.

## What it shows

- The current price, in the symbol's currency.
- Absolute change and percent change with an arrow, coloured green for up and red for down.
- A sparkline over the selected timeframe.
- Optional extras you toggle in the Display tab: the name, the timeframe label, an "updated N s ago" line, and rotation dots.

The **Change & % basis** setting in the Ticker tab picks what the change measures. The default, *Chart timeframe*, computes it over the same span the sparkline shows (live price versus the first charted point) and appends the live price as the chart's newest point, so the number, arrow, colours, and chart agree. Three caveats: it needs chart data, so with fewer than 2 chart points, a webhook that sends no `spark` series, or a failed chart fetch, the device falls back to the 1-day change until the data is there; at the 1-day timeframe the reference is the session's first data point, so an overnight gap is not part of the number; and GitHub-source tickers chart the span baked into `quotes-config.json`, so their change covers that span rather than the device timeframe. *1 day* shows the classic change since the previous close instead; a stock can be up on the day but down over a longer chart, so with this basis the number and the chart can legitimately point in opposite directions.

Non-USD currencies show as their ISO code, for example `CHF 79.73`, because the built-in bitmap font has no glyph for symbols like the euro sign.

## Symbols

Add up to 8 tickers in the **Ticker** tab. Each row has a symbol, an optional name that overrides the source's own, and its own data source, so Yahoo, cash.ch, and webhook tickers mix in one rotation. Yahoo examples that work:

| What | Examples |
|------|----------|
| US and global stocks and ETFs | `AAPL`, `MSFT`, `VOO` |
| Swiss and European stocks | `NESN.SW`, `ROG.SW`, `UBSG.SW`, `BMW.DE` |
| Crypto | `BTC-USD`, `ETH-EUR` |
| FX | `EURUSD=X`, `EURCHF=X` |

With the cash.ch source the `symbol` field takes a cash.ch listing key instead (`valor-marketId-currencyId`, e.g. `147478611-246-333`), which covers Swiss structured products and AMCs that Yahoo does not list. The built-in finder in the Ticker tab turns a cash.ch link, ISIN, or name into the key; [Data sources](/smalltv-mod/reference/data-sources/) has the details.

## Positions and the portfolio page

Give a ticker a `qty` and a per-unit `cost` and it becomes a position: its page shows a P/L line (absolute and percent versus your cost basis), and a portfolio summary page joins the rotation with one row per position and a total per currency. The "Position P/L & portfolio page" toggle in the Ticker tab turns both off. Cost is per unit in the instrument's own currency; totals are kept per currency and are not converted.

## Timing and data

Two intervals control the display: how often each symbol is shown (rotation) and how often data is refreshed (poll). Both are set in the Display tab. The default poll of 120 seconds is fine for 8 symbols.

Where the prices come from is chosen per ticker. By default a ticker fetches Yahoo Finance directly over HTTPS with no backend; cash.ch works the same way for Swiss instruments, GitHub is a serverless cash.ch proxy that needs nothing of yours running, and a webhook ticker calls your own endpoint. All four are covered in [Data sources](/smalltv-mod/reference/data-sources/).
