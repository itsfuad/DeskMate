#include "PreviewApi.h"

#include "FirmwareUi.h"
#include "TileRenderer.h"

void previewRenderFirmware(GfxFirmwareState state, const char* artifact,
                           uint32_t writtenBytes, uint32_t totalBytes,
                           const char* detail) {
  FirmwareUi::Context context;
  context.state = state;
  context.written = writtenBytes;
  context.total = totalBytes;
  strlcpy(context.artifact,
          artifact && artifact[0] ? artifact : "DeskMate firmware",
          sizeof(context.artifact));
  strlcpy(context.detail, detail ? detail : "", sizeof(context.detail));
  gfxRenderTiled(FirmwareUi::render, &context, C_UI_BG);
}
