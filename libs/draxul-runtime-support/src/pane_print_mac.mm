#include <draxul/pane_print.h>

#import <AppKit/AppKit.h>
#import <PDFKit/PDFKit.h>

namespace draxul
{

PrintDialogResult present_print_dialog_for_pdf(
    const std::filesystem::path& pdf_path, std::string& error)
{
    @autoreleasepool
    {
        NSString* path = [NSString stringWithUTF8String:pdf_path.string().c_str()];
        PDFDocument* document =
            [[PDFDocument alloc] initWithURL:[NSURL fileURLWithPath:path]];
        if (document == nil)
        {
            error = "could not load the composed PDF";
            return PrintDialogResult::Failed;
        }

        NSPrintInfo* print_info = [[NSPrintInfo sharedPrintInfo] copy];
        // autoRotate is the load-bearing part: a landscape A4 page rotates
        // to fit portrait paper instead of printing sideways-and-shrunk.
        NSPrintOperation* operation =
            [document printOperationForPrintInfo:print_info
                                     scalingMode:kPDFPrintPageScaleDownToFit
                                      autoRotate:YES];
        if (operation == nil)
        {
            error = "could not create a print operation";
            return PrintDialogResult::Failed;
        }
        [operation setShowsPrintPanel:YES];
        [operation setShowsProgressPanel:YES];

        // Modal on the main thread, like every other native dialog over the
        // SDL loop. Returns NO for both cancel and failure; treat it as the
        // user's decision — the dialog surfaces its own errors.
        const BOOL confirmed = [operation runOperation];
        return confirmed ? PrintDialogResult::Printed : PrintDialogResult::Canceled;
    }
}

} // namespace draxul
