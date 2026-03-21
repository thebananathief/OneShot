using OneShot.Models;

namespace OneShot.Services;

public interface IMarkupImageExportService
{
    CapturedImage Export(CapturedImage source, IReadOnlyList<MarkupPrimitive> primitives);
}
