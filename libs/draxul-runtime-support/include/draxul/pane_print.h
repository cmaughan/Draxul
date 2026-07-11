#pragma once

// Print-a-pane support (the `print_pane` GUI action): crop a captured frame
// to the focused pane, compose a single-page A4 PDF with the image
// aspect-fit inside margins (auto landscape for wide panes), and hand the
// file to the system print spooler (`lpr`).

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

// Writes a one-page A4 PDF with the image centered and aspect-fit inside a
// small margin. Landscape when the image is wider than tall. macOS-only
// (CoreGraphics); other platforms return false with an explanatory error.
bool write_rgba_pdf_a4(const uint8_t* rgba, int width, int height,
    const std::filesystem::path& pdf_path, std::string& error);

// Submits a file to the default system printer via `lpr`. Returns false
// with lpr's complaint (e.g. no default printer) in `error`.
bool submit_pdf_to_printer(const std::filesystem::path& pdf_path, std::string& error);

} // namespace draxul
