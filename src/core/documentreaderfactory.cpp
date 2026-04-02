#include "core/documentreaderfactory.h"

#include "core/czireader.h"
#include "core/nd2reader.h"

#include <QFileInfo>

std::unique_ptr<DocumentReader> createDocumentReaderForPath(const QString &path, QString *errorMessage)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("nd2")) {
        return std::make_unique<Nd2Reader>();
    }

    if (suffix == QStringLiteral("czi")) {
        return std::make_unique<CziReader>();
    }

    if (errorMessage) {
        *errorMessage = QStringLiteral("Unsupported file type '%1'. Only ND2 and CZI files are supported.")
                            .arg(suffix.isEmpty() ? QStringLiteral("(no extension)") : suffix);
    }

    return {};
}
