#include <draxul/pane_print.h>

#include <algorithm>
#include <cstdio>

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#endif

namespace draxul
{

CroppedImage crop_rgba(const std::vector<uint8_t>& rgba, int width, int height,
    int crop_x, int crop_y, int crop_w, int crop_h)
{
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<size_t>(width) * static_cast<size_t>(height) * 4u)
        return {};
    const int x0 = std::clamp(crop_x, 0, width);
    const int y0 = std::clamp(crop_y, 0, height);
    const int x1 = std::clamp(crop_x + crop_w, x0, width);
    const int y1 = std::clamp(crop_y + crop_h, y0, height);
    CroppedImage out;
    out.width = x1 - x0;
    out.height = y1 - y0;
    if (out.width <= 0 || out.height <= 0)
        return {};
    out.rgba.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height) * 4u);
    for (int row = 0; row < out.height; ++row)
    {
        const uint8_t* src = rgba.data() + (static_cast<size_t>(y0 + row) * static_cast<size_t>(width) + static_cast<size_t>(x0)) * 4u;
        std::copy_n(src, static_cast<size_t>(out.width) * 4u,
            out.rgba.data() + static_cast<size_t>(row) * static_cast<size_t>(out.width) * 4u);
    }
    return out;
}

void snap_paper_white(CroppedImage& image, uint8_t threshold)
{
    for (size_t at = 0; at + 3 < image.rgba.size(); at += 4)
    {
        if (image.rgba[at] >= threshold && image.rgba[at + 1] >= threshold && image.rgba[at + 2] >= threshold)
        {
            image.rgba[at] = 255;
            image.rgba[at + 1] = 255;
            image.rgba[at + 2] = 255;
        }
    }
}

#ifdef __APPLE__

bool write_rgba_pdf_a4(const uint8_t* rgba, int width, int height,
    const std::filesystem::path& pdf_path, std::string& error)
{
    if (rgba == nullptr || width <= 0 || height <= 0)
    {
        error = "empty pane capture";
        return false;
    }

    // A4 in PDF points; rotate the page for wide panes so the image gets
    // the larger side.
    constexpr double kA4Short = 595.276;
    constexpr double kA4Long = 841.89;
    constexpr double kMargin = 24.0;
    const bool landscape = width > height;
    const double page_w = landscape ? kA4Long : kA4Short;
    const double page_h = landscape ? kA4Short : kA4Long;

    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
    CGContextRef bitmap = CGBitmapContextCreate(nullptr, static_cast<size_t>(width),
        static_cast<size_t>(height), 8, static_cast<size_t>(width) * 4, color_space,
        kCGImageAlphaNoneSkipLast); // pane pixels are opaque; drop alpha
    if (bitmap == nullptr)
    {
        CGColorSpaceRelease(color_space);
        error = "could not create bitmap context";
        return false;
    }
    // CGImage row 0 draws at the TOP of the destination rect, matching the
    // capture's top-down rows — copy straight through.
    auto* dst = static_cast<uint8_t*>(CGBitmapContextGetData(bitmap));
    const size_t stride = CGBitmapContextGetBytesPerRow(bitmap);
    for (int row = 0; row < height; ++row)
    {
        std::copy_n(rgba + static_cast<size_t>(row) * static_cast<size_t>(width) * 4u,
            static_cast<size_t>(width) * 4u, dst + static_cast<size_t>(row) * stride);
    }
    CGImageRef image = CGBitmapContextCreateImage(bitmap);
    CGContextRelease(bitmap);
    CGColorSpaceRelease(color_space);
    if (image == nullptr)
    {
        error = "could not create image";
        return false;
    }

    const std::string path_string = pdf_path.string();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
        reinterpret_cast<const UInt8*>(path_string.c_str()),
        static_cast<CFIndex>(path_string.size()), false);
    CGRect media_box = CGRectMake(0, 0, page_w, page_h);
    CGContextRef pdf = url != nullptr ? CGPDFContextCreateWithURL(url, &media_box, nullptr) : nullptr;
    if (url != nullptr)
        CFRelease(url);
    if (pdf == nullptr)
    {
        CGImageRelease(image);
        error = "could not create PDF at " + path_string;
        return false;
    }

    const double avail_w = page_w - 2.0 * kMargin;
    const double avail_h = page_h - 2.0 * kMargin;
    const double scale = std::min(avail_w / width, avail_h / height);
    const double draw_w = width * scale;
    const double draw_h = height * scale;
    const CGRect draw_rect = CGRectMake(
        (page_w - draw_w) * 0.5, (page_h - draw_h) * 0.5, draw_w, draw_h);

    CGPDFContextBeginPage(pdf, nullptr);
    CGContextSetInterpolationQuality(pdf, kCGInterpolationHigh);
    CGContextDrawImage(pdf, draw_rect, image);
    CGPDFContextEndPage(pdf);
    CGPDFContextClose(pdf);
    CGContextRelease(pdf);
    CGImageRelease(image);
    return true;
}

#else

bool write_rgba_pdf_a4(const uint8_t*, int, int, const std::filesystem::path&, std::string& error)
{
    error = "pane printing is not supported on this platform yet";
    return false;
}

#endif

#ifndef __APPLE__

PrintDialogResult present_print_dialog_for_pdf(
    const std::filesystem::path& /*pdf_path*/, std::string& error)
{
    error = "pane printing is not supported on this platform yet";
    return PrintDialogResult::Failed;
}

#endif

} // namespace draxul
