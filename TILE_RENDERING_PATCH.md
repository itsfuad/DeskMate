# Tile-rendering patch

This branch replaces visible full-screen clears in the ticker, usage, radar, and
mascot-entry paths with a 40x40 RGB565 tile backbuffer.

## Memory

- One static tile buffer: `40 * 40 * 2 = 3,200` bytes.
- No frame-sized allocation.
- No allocation during rendering.

## Rendering model

For every 40x40 tile, the complete current scene is rendered into RAM using
full-screen coordinates. The tile canvas clips every primitive and text glyph to
that tile. Only after the final pixels are ready is the tile copied to the
ST7789. The old display contents therefore remain visible until their completed
replacement arrives.

This correctly handles circles, triangles, text, lines crossing tile borders,
and moving objects because every tile is a clipped viewport into the same full
scene. It does not erase only an object's new bounding box.

## Files

- `src/TileRenderer.h`
- `src/TileRenderer.cpp`
- `src/features/ticker/TickerMode.cpp`
- `src/features/usage/UsageMode.cpp`
- `src/features/radar/RadarMode.cpp`
- `platformio.ini`

## Build

```bash
pio run -e smalltv
```

The patch adds an explicit dependency on Adafruit GFX for the RAM tile canvas.
Arduino_GFX remains the physical ST7789 output driver.
