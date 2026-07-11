#pragma once

// Print-a-pane support (the `print_pane` GUI action): crop a captured frame
// to the focused pane, compose a single-page A4 PDF with the image
// aspect-fit inside margins (auto landscape for wide panes), and present
// the system print dialog for it (preview, printer choice, and
// auto-rotation of landscape pages onto portrait paper).

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace draxul
{

struct CroppedImage
{
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;
};

// Crops tightly-packed RGBA pixels. The requested rectangle is clamped to
// the source; returns an empty image when nothing remains.
CroppedImage crop_rgba(const std::vector<uint8_t>& rgba, int width, int height,
    int crop_x, int crop_y, int crop_w, int crop_h);

// Snaps near-white pixels (every channel >= threshold) to pure white.
// Screen-tuned paper tints (ScoreView's warm off-white sheet) print as a
// faint stipple on real paper; ink and antialiased edges stay untouched.
void snap_paper_white(CroppedImage& image, uint8_t threshold = 240);

// Writes a one-page A4 PDF with the image centered and aspect-fit inside a
// small margin. Landscape when the image is wider than tall. macOS-only
// (CoreGraphics); other platforms return false with an explanatory error.
bool write_rgba_pdf_a4(const uint8_t* rgba, int width, int height,
    const std::filesystem::path& pdf_path, std::string& error);

enum class PrintDialogResult
{
    Printed, // the user confirmed the dialog and the job was spooled
    Canceled, // the user dismissed the dialog — not an error
    Failed,
};

// Presents the native print dialog (with preview) for a PDF, modal on the
// main thread — the same pattern as other native dialogs over the SDL
// loop. Landscape pages auto-rotate onto the selected paper. macOS-only;
// other platforms return Failed with an explanatory error.
PrintDialogResult present_print_dialog_for_pdf(
    const std::filesystem::path& pdf_path, std::string& error);

} // namespace draxul
